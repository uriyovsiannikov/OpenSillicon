#!/usr/bin/env python3
"""Build the pinned musl baseline for the bounded OpenSillicon x86_64 target."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from common import MUSL_ROOT, load_manifests


REPO = MUSL_ROOT.parents[1]
BOOTSTRAP = REPO / "build" / "musl-bootstrap"
OUTPUT_ID = "OpenSillicon musl native build output v1"
TARGET = "x86_64-unknown-none-elf"
SUPPORTED_LLVM_VERSION = "22.1.8"
LLVM_TOOLS = ("clang", "ld.lld", "llvm-ar", "llvm-ranlib",
              "llvm-objdump", "llvm-nm")
FLAGS = [
    "-O2", "-pipe", "-ffreestanding", "-fno-builtin",
    "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
]
MALLOCNG_INTEGER_FLAGS = [
    "-mno-mmx", "-mno-sse", "-mno-sse2", "-msoft-float",
]
MALLOCNG_OBJECTS = [
    "aligned_alloc.o", "donate.o", "free.o", "malloc.o",
    "malloc_usable_size.o", "realloc.o",
]


def run(argv: list[str], *, cwd: Path, env: dict[str, str] | None = None,
        log: Path | None = None) -> None:
    print("+ " + " ".join(argv))
    if log is None:
        subprocess.run(argv, cwd=cwd, env=env, check=True)
        return
    with log.open("w", encoding="utf-8") as stream:
        subprocess.run(argv, cwd=cwd, env=env, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tool_version(path: Path, args: tuple[str, ...]) -> str:
    output = subprocess.check_output([str(path), *args], text=True,
                                     stderr=subprocess.STDOUT)
    match = re.search(r"\b(\d+\.\d+\.\d+)\b", output)
    if match is None:
        raise ValueError(f"could not determine version for {path}: {output.splitlines()[0]}")
    return match.group(1)


def detect_toolchain() -> dict[str, dict[str, str]]:
    """Resolve and validate the LLVM 22.1.8 tools before writing build output."""
    tools: dict[str, dict[str, str]] = {}
    requested_clang = shutil.which("clang")
    if requested_clang is None:
        raise ValueError("required tool 'clang' was not found on PATH")
    requested_path = Path(requested_clang).absolute()
    requested_version = tool_version(requested_path, ("--version",))

    # Fedora places ccache wrappers before /usr/bin on PATH. Bypass that
    # wrapper so configure cannot depend on a writable user cache.
    resolved = requested_path.resolve()
    if resolved.name == "ccache":
        real_clang = shutil.which(f"clang-{requested_version.split('.')[0]}")
        if real_clang is None:
            raise ValueError(
                f"'clang' resolves through ccache at {requested_path}, but the "
                f"matching clang-{requested_version.split('.')[0]} binary was not found")
        requested_path = Path(real_clang).absolute()

    candidates = {"clang": requested_path}
    for name in LLVM_TOOLS[1:]:
        candidate = shutil.which(name)
        if candidate is None:
            raise ValueError(f"required tool '{name}' was not found on PATH")
        candidates[name] = Path(candidate).absolute()

    version_args = {"clang": ("--version",), "ld.lld": ("--version",),
                    "llvm-ar": ("--version",), "llvm-ranlib": ("--version",),
                    "llvm-objdump": ("--version",), "llvm-nm": ("--version",)}
    for name, path in candidates.items():
        version = tool_version(path, version_args[name])
        if version != SUPPORTED_LLVM_VERSION:
            raise ValueError(
                f"unsupported {name} at {path}: found {version}; "
                f"this build is validated with LLVM {SUPPORTED_LLVM_VERSION}")
        tools[name] = {"path": str(path), "version": version}

    # Ensure Clang resolves its -fuse-ld=lld request to the validated linker.
    driver = subprocess.check_output(
        [str(candidates["clang"]), f"--target={TARGET}", "-fuse-ld=lld",
         f"-B{candidates['ld.lld'].parent}", "-###", "-nostdlib", "-static",
         "-x", "c", "/dev/null", "-o", os.devnull],
        text=True, stderr=subprocess.STDOUT)
    link_driver_match = re.search(r'^\s*"([^"\n]*/gcc)"', driver, re.MULTILINE)
    if link_driver_match is None:
        raise ValueError("could not identify the host linker driver selected by Clang")
    link_driver = Path(link_driver_match.group(1))
    if not link_driver.is_file():
        raise ValueError(f"Clang selected a missing host linker driver: {link_driver}")
    link_driver_version = subprocess.check_output(
        [str(link_driver), "--version"], text=True).splitlines()[0]

    make_path = shutil.which("make")
    if make_path is None:
        raise ValueError("required tool 'make' was not found on PATH")
    make = Path(make_path).absolute()
    make_version = subprocess.check_output([str(make), "--version"], text=True).splitlines()[0]
    tools["make"] = {"path": str(make), "version": make_version}
    tools["link_driver"] = {"path": str(link_driver), "version": link_driver_version}
    return tools


def make_mallocng_fpu_independent(build_dir: Path, build_log: Path,
                                  tools: dict[str, dict[str, str]]) -> None:
    """Build integer-only mallocng objects without using unavailable SSE state."""
    object_names = set(MALLOCNG_OBJECTS)
    compile_commands: dict[str, list[str]] = {}
    archive_objects: list[str] | None = None

    for line in build_log.read_text(encoding="utf-8").splitlines():
        if "obj/src/malloc/mallocng/" in line and " -c " in line:
            try:
                command = shlex.split(line)
            except ValueError:
                continue
            output_index = command.index("-o") + 1 if "-o" in command else -1
            if output_index > 0:
                output = command[output_index]
                name = Path(output).name
                if name in object_names and "src/malloc/mallocng/" in output:
                    compile_commands[name] = command
        try:
            command = shlex.split(line)
        except ValueError:
            command = []
        if (len(command) >= 4 and Path(command[0]).name == "llvm-ar" and
                command[1:3] == ["rc", "lib/libc.a"]):
            archive_objects = command[3:]

    if set(compile_commands) != object_names:
        missing = sorted(object_names - set(compile_commands))
        raise ValueError(f"missing mallocng compiler commands: {missing}")
    if not archive_objects:
        raise ValueError("could not find libc archive object order in build log")

    for name in sorted(object_names):
        command = compile_commands[name] + MALLOCNG_INTEGER_FLAGS
        run(command, cwd=build_dir)

    # The usual archive has duplicate basenames (for example stdlib/free.o
    # and mallocng/free.o). Rebuild using musl's recorded object order so both
    # members remain present and the selected mallocng members are replaced.
    archive = build_dir / "lib" / "libc.a"
    archive.unlink()
    run([tools["llvm-ar"]["path"], "rc", "lib/libc.a", *archive_objects], cwd=build_dir)
    run([tools["llvm-ranlib"]["path"], "lib/libc.a"], cwd=build_dir)

    disassembly = subprocess.check_output(
        [tools["llvm-objdump"]["path"], "-d", *[f"obj/src/malloc/mallocng/{name}" for name in sorted(object_names)]],
        cwd=build_dir, text=True)
    if re.search(r"\b(?:xmm|ymm|zmm|mm)[0-9]+\b", disassembly):
        raise ValueError("mallocng objects still contain SIMD register instructions")


def prepare_output(output: Path) -> None:
    marker = output / ".opensillicon-musl-output"
    if output.is_symlink():
        raise ValueError(f"refusing symlink output directory: {output}")
    if output.exists():
        if marker.is_symlink() or not marker.is_file() or marker.read_text().strip() != OUTPUT_ID:
            raise ValueError(
                f"output exists without this script's ownership marker: {output}; "
                "choose another --output"
            )
        shutil.rmtree(output)
    output.mkdir(parents=True)
    (output / ".opensillicon-musl-output").write_text(OUTPUT_ID + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=REPO / "build" / "musl-port")
    parser.add_argument("--bootstrap", type=Path, default=BOOTSTRAP)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        raise ValueError("--jobs must be positive")
    tools = detect_toolchain()
    print("validated musl toolchain:")
    for name, tool in tools.items():
        print(f"  {name}: {tool['path']} ({tool['version']})")
    output_arg = args.output.expanduser()
    if not output_arg.is_absolute():
        output_arg = REPO / output_arg
    # Do not resolve symlinks before prepare_output checks them. Resolving here
    # would let an output symlink bypass that guard and target unrelated files.
    output = Path(os.path.abspath(output_arg))
    bootstrap = args.bootstrap.resolve()
    source_manifest, patch_manifest = load_manifests()
    archive = bootstrap / "downloads" / source_manifest["archive_name"]
    if not archive.is_file():
        raise ValueError(
            f"pinned musl archive is missing: {archive}; "
            "run native-musl-verify or provide it in the bootstrap downloads directory")
    run([sys.executable, str(MUSL_ROOT / "scripts" / "verify.py"),
         "--output", str(bootstrap)], cwd=REPO)
    run([sys.executable, str(MUSL_ROOT / "scripts" / "bootstrap.py"),
         "--archive", str(archive), "--output", str(bootstrap)], cwd=REPO)
    source = bootstrap / "source" / "patched" / source_manifest["top_level"]
    if not source.is_dir():
        raise ValueError(f"musl bootstrap did not create patched source: {source}")

    prepare_output(output)
    source_copy = output / "source" / source_manifest["top_level"]
    build_dir = output / "build"
    sysroot = output / "sysroot"
    (source_copy.parent).mkdir(parents=True)
    build_dir.mkdir()
    sysroot.mkdir()
    shutil.copytree(source, source_copy)
    overlays = MUSL_ROOT / "opensillicon"
    shutil.copy2(overlays / "syscall_arch.h",
                 source_copy / "arch" / "x86_64" / "syscall_arch.h")
    shutil.copy2(overlays / "syscall_cp.s",
                 source_copy / "src" / "thread" / "x86_64" / "syscall_cp.s")
    shutil.copy2(overlays / "set_thread_area.s",
                 source_copy / "src" / "thread" / "x86_64" / "__set_thread_area.s")

    env = os.environ.copy()
    linker_dir = str(Path(tools["ld.lld"]["path"]).parent)
    # Clang delegates x86_64-unknown-none-elf linking through its configured
    # GCC driver. Put the already-validated LLD directory first in that
    # driver's PATH lookup so -fuse-ld=lld cannot select another installation.
    env["PATH"] = linker_dir + os.pathsep + env.get("PATH", "")
    env.update({"LC_ALL": "C", "AR": tools["llvm-ar"]["path"],
                "RANLIB": tools["llvm-ranlib"]["path"],
                "CC": f"{tools['clang']['path']} --target={TARGET}",
                "LDFLAGS": f"-fuse-ld=lld -B{linker_dir} -nostdlib -static"})
    configure = [
        str(source_copy / "configure"), f"--srcdir={source_copy}",
        f"--target={TARGET}", "--build=x86_64-redhat-linux-gnu",
        "--prefix=/usr", "--syslibdir=/lib", "--disable-shared",
        "--disable-wrapper", "LIBCC= ", "CFLAGS=" + " ".join(FLAGS),
    ]
    run(configure, cwd=build_dir, env=env, log=output / "configure.log")
    run([tools["make"]["path"], f"-j{args.jobs}", "V=1"], cwd=build_dir, env=env,
        log=output / "build.log")
    make_mallocng_fpu_independent(build_dir, output / "build.log", tools)
    run([tools["make"]["path"], f"DESTDIR={sysroot}", "install"], cwd=build_dir, env=env,
        log=output / "install.log")

    adapter_object = output / "native_syscall.o"
    run([
        tools["clang"]["path"], f"--target={TARGET}", "-std=c11", "-O2", "-Wall", "-Wextra",
        "-Werror", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-mno-red-zone", "-nostdinc",
        "-isystem", str(sysroot / "usr" / "include"), "-c",
        str(overlays / "native_syscall.c"), "-o", str(adapter_object),
    ], cwd=REPO, env=env)
    run([tools["llvm-ar"]["path"], "r", str(sysroot / "usr" / "lib" / "libc.a"),
         str(adapter_object)], cwd=REPO, env=env)
    run([tools["llvm-ranlib"]["path"], str(sysroot / "usr" / "lib" / "libc.a")],
        cwd=REPO, env=env)
    run([tools["llvm-nm"]["path"], "-u", str(adapter_object)], cwd=REPO, env=env,
        log=output / "adapter-undefined.log")
    if (output / "adapter-undefined.log").read_text().strip():
        raise ValueError("native syscall adapter has unresolved symbols")

    license_dir = sysroot / "usr" / "share" / "licenses" / "musl"
    license_dir.mkdir(parents=True)
    shutil.copy2(source_copy / "COPYRIGHT", license_dir / "COPYRIGHT")
    patch_records = patch_manifest["confirmed_upstream"]
    info = {
        "project": "OpenSillicon native musl sysroot",
        "musl_version": source_manifest["version"],
        "archive_sha256": source_manifest["archive_sha256"],
        "copyright_sha256": source_manifest["copyright_sha256"],
        "confirmed_patches": [
            {"file": item["file"], "sha256": item["sha256"]}
            for item in patch_records
        ],
        "local_adapter_files": {
            path.name: sha256(path)
            for path in sorted(overlays.iterdir()) if path.is_file()
        },
        "target": TARGET,
        "toolchain": tools,
        "linker_flags": env["LDFLAGS"],
        "cflags": FLAGS,
        "mallocng_objects": MALLOCNG_OBJECTS,
        "mallocng_cflags_extra": MALLOCNG_INTEGER_FLAGS,
        "configure": [
            "--srcdir=source/musl-1.2.6",
            f"--target={TARGET}", "--build=x86_64-redhat-linux-gnu",
            "--prefix=/usr", "--syslibdir=/lib", "--disable-shared",
            "--disable-wrapper", "LIBCC= ", "CFLAGS=" + " ".join(FLAGS),
        ],
        "runtime_libraries": "none; LIBCC intentionally empty; no host libc or compiler runtime",
        "profile": "static single-thread x86_64; unsupported syscalls return ENOSYS",
    }
    (output / "build-info.json").write_text(
        json.dumps(info, indent=2, sort_keys=True) + "\n")
    print(f"native musl sysroot installed at {sysroot}")
    print("musl source was built; no system installation was modified")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"native musl build failed: {exc}", file=sys.stderr)
        sys.exit(1)
