#!/usr/bin/env python3
import json
import os
import socket
import subprocess
import sys
import tempfile
import time


def qmp_command(connection, stream, command, arguments=None):
    request = {"execute": command}
    if arguments is not None:
        request["arguments"] = arguments
    connection.sendall((json.dumps(request) + "\r\n").encode())
    while True:
        response = json.loads(stream.readline())
        if "return" in response:
            return response["return"]
        if "error" in response:
            raise RuntimeError(response["error"])


def wait_for(predicate, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise RuntimeError("timed out waiting for " + description)


def main():
    qemu, ovmf, iso, disk = sys.argv[1:5]
    with tempfile.TemporaryDirectory(prefix="opensillicon-terminal-") as work:
        qmp_path = os.path.join(work, "qmp.sock")
        serial_path = os.path.join(work, "serial.log")
        before_path = os.path.join(work, "before.ppm")
        after_path = os.path.join(work, "after.ppm")
        process = subprocess.Popen([
            qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
            "-cdrom", iso, "-drive",
            "file=" + disk + ",if=none,id=rootdisk,format=raw",
            "-device", "ide-hd,drive=rootdisk,bus=ide.0",
            "-serial", "file:" + serial_path, "-display", "none",
            "-no-reboot", "-no-shutdown", "-qmp",
            "unix:" + qmp_path + ",server=on,wait=off"
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_for(lambda: os.path.exists(qmp_path), 15, "QMP socket")
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.connect(qmp_path)
            stream = connection.makefile("rb")
            json.loads(stream.readline())
            connection.sendall(b'{"execute":"qmp_capabilities"}\r\n')
            json.loads(stream.readline())
            wait_for(lambda: os.path.exists(serial_path) and
                "OpenSillicon terminal ready" in
                open(serial_path, encoding="ascii", errors="replace").read(),
                45, "kernel terminal prompt")
            if "polled PS/2 keyboard initialized" not in open(
                serial_path, encoding="ascii", errors="replace").read():
                raise RuntimeError("QEMU did not initialize its PS/2 keyboard")
            qmp_command(connection, stream, "human-monitor-command", {
                "command-line": "screendump " + before_path
            })
            qmp_command(connection, stream, "send-key", {
                "keys": [{"type": "qcode", "data": "a"}]
            })
            time.sleep(0.25)
            qmp_command(connection, stream, "human-monitor-command", {
                "command-line": "screendump " + after_path
            })
            connection.close()
            before = open(before_path, "rb").read()
            after = open(after_path, "rb").read()
            if len(before) < 64 or len(before) != len(after) or before == after:
                raise RuntimeError("keyboard input did not change the framebuffer")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

        absent_serial = os.path.join(work, "no-keyboard.log")
        process = subprocess.Popen([
            qemu, "-machine", "q35,i8042=off", "-m", "256M", "-bios",
            ovmf, "-cdrom", iso, "-drive",
            "file=" + disk + ",if=none,id=rootdisk,format=raw",
            "-device", "ide-hd,drive=rootdisk,bus=ide.0", "-serial",
            "file:" + absent_serial, "-display", "none", "-no-reboot",
            "-no-shutdown"
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        start = time.monotonic()
        try:
            wait_for(lambda: os.path.exists(absent_serial) and
                "OpenSillicon terminal ready" in
                open(absent_serial, encoding="ascii", errors="replace").read(),
                45, "terminal startup without an i8042 controller")
            log = open(absent_serial, encoding="ascii", errors="replace").read()
            if "PS/2 keyboard unavailable: i8042 controller timeout" not in log:
                raise RuntimeError("missing i8042-unavailable diagnostic")
            if time.monotonic() - start > 15:
                raise RuntimeError("i8042 timeout delayed boot unexpectedly")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        print("QEMU UEFI framebuffer, PS/2 input, and absent-controller handling passed")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("terminal QEMU test failed:", error, file=sys.stderr)
        sys.exit(1)
