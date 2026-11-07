#!/usr/bin/env python3
"""Quiet make shell and shared terminal progress for OpenSillicon builds.

The real command and its output go to a per-target log. A small flock-protected
state file coordinates one live terminal line across parallel make jobs and
nested library builds. No Makefile recipe grouping or shell semantics change.
"""
import argparse
from collections import deque
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
import time
import uuid

FRAMES = "\\|/-"
LIVE_TARGETS = {"run", "run-gui", "clean"}
SETUP = {"mkdir", "cp", "touch", "rm", "find", "tar", "printf", "install", "ln"}


def jobserver_fds():
    """Preserve GNU make's pipe jobserver when it grants this recipe access."""
    match = re.search(r"--jobserver-(?:auth|fds)=(\d+),(\d+)", os.environ.get("MAKEFLAGS", ""))
    descriptors = []
    if match:
        for item in match.groups():
            fd = int(item)
            try:
                fcntl.fcntl(fd, fcntl.F_GETFD)
            except OSError:
                continue
            descriptors.append(fd)
    return descriptors


def dry_run():
    flags = os.environ.get("MAKEFLAGS", "").split()
    return bool(flags and not flags[0].startswith("-") and "n" in flags[0]) or "--just-print" in flags or "--dry-run" in flags


def label_for(target, source):
    fixed = {
        "kernel.elf": "KERNEL link", "root.img": "ROOT filesystem",
        "opensillicon.iso": "ISO boot image", "initramfs.cpio": "INITRAMFS",
        "liblibos.a": "LIB libos", "libz.a": "LIB zlib",
        "libfontenc.a": "LIB libfontenc", "libXfont.a": "LIB libXfont",
        "Xopensillicon": "USER TinyX", ".apps-ready": "USER X11 applications",
        ".opensillicon-configured": "USER TinyX configure", "nano": "USER nano",
        "pkg-server": "HOST pkg-server", "pkg-host": "HOST pkg-client",
        "pkg-build": "HOST pkg-build", "populate_root": "HOST root image tools",
    }
    basename = Path(target).name
    if basename in fixed:
        return fixed[basename]
    if "/acpica/" in target:
        return "ACPICA " + target.split("/acpica/", 1)[1].removesuffix(".o")
    if "/cyclone/" in target:
        return "KERNEL CycloneTCP/" + target.split("/cyclone/", 1)[1].removesuffix(".o")
    for prefix, group in (("src/kernel/", "KERNEL"), ("src/user/libos/", "LIBOS"),
                          ("src/user/thirdparty/xorg/deps/", "LIB"),
                          ("src/user/", "USER"), ("src/tools/", "HOST"),
                          ("src/tests/", "TEST")):
        if source.startswith(prefix):
            name = source[len(prefix):]
            return group + " " + str(Path(name).with_suffix(""))
    if basename.endswith(".elf"):
        return "USER " + basename[:-4]
    if basename.endswith(".ko"):
        return "MODULE " + basename[:-3]
    if basename.endswith(".spkg"):
        return "PKG " + basename[:-5]
    if "test" in basename:
        return "TEST " + basename
    return "KERNEL " + basename.removesuffix(".o")


def log_name(target):
    output = Path(os.environ.get("BUILD_STATUS_OUTPUT_ROOT", "src/build")).absolute()
    try:
        relative = Path(target).absolute().relative_to(output)
    except ValueError:
        relative = Path("steps") / re.sub(r"[^a-zA-Z0-9_.+-]", "_", target)
    # Append rather than replace the suffix: foo.o and foo.elf have separate logs.
    return str(relative) + ".log"


class Display:
    def __init__(self, directory):
        inherited = os.environ.get("BUILD_STATUS_FD")
        self.fd = None
        if inherited:
            try:
                self.fd = fcntl.fcntl(int(inherited), fcntl.F_DUPFD, 20)
            except (OSError, ValueError):
                pass
        if self.fd is None:
            self.fd = fcntl.fcntl(sys.stdout.fileno(), fcntl.F_DUPFD, 20)
        os.set_inheritable(self.fd, True)
        self.stream = os.fdopen(self.fd, "w", buffering=1, closefd=False)
        self.tty = os.isatty(self.fd) and os.environ.get("TERM") != "dumb"
        self.animate = self.tty and os.environ.get("BUILD_SPINNER", "1") != "0"
        self.directory = directory
        self.lock = (directory / ".display.lock").open("a")
        self.state_path = directory / ".display.json"
        self.identity = f"{os.getpid()}-{uuid.uuid4().hex[:8]}"
        self.started = time.monotonic()

    def change(self, label=None, finished=None, diagnostic=None):
        fcntl.flock(self.lock, fcntl.LOCK_EX)
        try:
            try:
                state = json.loads(self.state_path.read_text())
            except (OSError, ValueError):
                state = {}
            # Remove entries left behind by killed processes.
            for key in list(state):
                try:
                    os.kill(state[key]["pid"], 0)
                except ProcessLookupError:
                    del state[key]
                except PermissionError:
                    pass
            if label is not None:
                state[self.identity] = {"pid": os.getpid(), "label": label, "start": self.started}
                if not self.animate:
                    self.stream.write(f"[building] building {label}\n")
            if self.animate:
                self.stream.write("\r\033[2K")
            if finished:
                entry = state.pop(self.identity, None)
                name = entry["label"] if entry else label or "build step"
                duration = time.monotonic() - self.started
                self.stream.write(f"[{finished}] {name} ({duration:.1f}s)\n")
                if diagnostic:
                    self.stream.write(diagnostic)
                    if not diagnostic.endswith("\n"):
                        self.stream.write("\n")
            if self.animate and state:
                current = max(state.values(), key=lambda item: item["start"])
                frame = FRAMES[int(time.monotonic() * 8) % len(FRAMES)]
                other = f" (+{len(state)-1} active)" if len(state) > 1 else ""
                line = f"[building] building {current['label']}{other}"
                try:
                    width = os.get_terminal_size(self.fd).columns or 100
                except OSError:
                    width = 100
                # Keep the spinner visible even for long source paths.
                self.stream.write(line[:max(1, width-3)] + " " + frame)
            self.stream.flush()
            self.state_path.write_text(json.dumps(state))
        finally:
            fcntl.flock(self.lock, fcntl.LOCK_UN)

    def close(self):
        self.lock.close()
        self.stream.close()
        os.close(self.fd)


def execute(command, target, source, label=None, quiet_setup=False):
    session = os.environ.get("BUILD_STATUS_SESSION") or uuid.uuid4().hex[:12]
    # Session comes from make or our own UUID; it must never escape the log root.
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", session):
        raise ValueError("invalid build log session")
    directory = Path(os.environ.get("BUILD_LOG_DIR", "src/build/logs")) / session
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / log_name(target)
    path.parent.mkdir(parents=True, exist_ok=True)
    display = Display(directory)
    name = label or label_for(target, source)
    environment = os.environ.copy()
    environment["BUILD_STATUS_SESSION"] = session
    environment["BUILD_STATUS_FD"] = str(display.fd)
    environment["BUILD_LOG_DIR"] = str(directory.parent.absolute())
    descriptors = tuple(set(jobserver_fds() + [display.fd]))
    child = None
    interrupted = None

    def stop(number, _frame):
        nonlocal interrupted
        interrupted = number
        if child is not None and child.poll() is None:
            try:
                os.killpg(child.pid, number)
            except ProcessLookupError:
                pass

    previous = {number: signal.signal(number, stop) for number in (signal.SIGINT, signal.SIGTERM)}
    try:
        with path.open("a") as log:
            log.write("\n$ " + shlex.join(command) + "\n")
            log.flush()
            if not quiet_setup:
                display.change(label=name)
            child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                     env=environment, pass_fds=descriptors, start_new_session=True)
            signal_time = None
            while True:
                try:
                    child.wait(timeout=0.1)
                    break
                except subprocess.TimeoutExpired:
                    pass
                if interrupted is not None:
                    if signal_time is None:
                        signal_time = time.monotonic()
                    elif time.monotonic() - signal_time > 3:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                if display.animate and not quiet_setup:
                    display.change()
            code = child.returncode
        if interrupted is not None or code in (-signal.SIGINT, -signal.SIGTERM):
            display.change(finished="interrupted", label=name if quiet_setup else None,
                           diagnostic=f"Log: {path}\n")
            return 128 + (interrupted or -code)
        if code:
            with path.open(errors="replace") as log:
                tail = "".join(deque(log, maxlen=35))
            display.change(finished="failed", label=name if quiet_setup else None,
                           diagnostic=f"Exit status: {code}\nLog: {path}\n{tail}")
        elif not quiet_setup:
            display.change(finished="done")
        return code if code >= 0 else 128-code
    except OSError as error:
        display.change(finished="failed", label=name if quiet_setup else None,
                       diagnostic=f"{error}\nLog: {path}\n")
        return 127
    finally:
        for number, handler in previous.items():
            signal.signal(number, handler)
        display.close()


def main():
    if sys.argv[1:] == ["--new-session"]:
        print(uuid.uuid4().hex[:12])
        return 0
    if sys.argv[1:3] == ["--shell", "-c"]:
        text = sys.argv[3]
        target = os.environ.get("BUILD_STATUS_TARGET", "")
        source = os.environ.get("BUILD_STATUS_SOURCE", "")
        command = (shlex.split(os.environ.get("BUILD_STATUS_REAL_SHELL", "/bin/sh")) +
                   shlex.split(os.environ.get("BUILD_STATUS_REAL_FLAGS", "-c")) + [text])
        # $(shell ...) is a make query, never a build step. Interactive apps
        # and test output remain attached to the real terminal as well.
        if not target or target in LIVE_TARGETS or target.endswith("-test") or dry_run():
            os.execvp(command[0], command)
        first = text.lstrip().split(None, 1)[0] if text.strip() else ""
        quiet_setup = first in SETUP and not any(token in text for token in ("&&", "||", ";", "\n"))
        return execute(command, target, source, quiet_setup=quiet_setup)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--step", required=True, help="component displayed in the terminal")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a command is required")
    if os.environ.get("V") == "1" or not os.environ.get("BUILD_STATUS_SESSION"):
        os.execvp(command[0], command)
    digest = hashlib.sha256(args.step.encode()).hexdigest()[:8]
    return execute(command, f"components/{args.step}-{digest}", "", label=args.step)


if __name__ == "__main__":
    sys.exit(main())
