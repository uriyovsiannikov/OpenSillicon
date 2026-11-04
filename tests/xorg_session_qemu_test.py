#!/usr/bin/env python3
"""Smoke-test TinyX, xterm input, and feh image decoding in QEMU."""
import argparse
import pathlib
import socket
import shutil
import subprocess
import tempfile
import time


class Monitor:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect(str(path))
        self.response()

    def response(self):
        data = b""
        while not data.endswith(b"(qemu) "):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("QEMU monitor disconnected")
            data += chunk
        return data

    def command(self, command):
        self.sock.sendall(command.encode() + b"\n")
        return self.response()

    def type(self, text):
        names = {" ": "spc", "-": "minus", ".": "dot", "/": "slash"}
        for char in text:
            key = names.get(char, char)
            self.command("sendkey " + key)
            time.sleep(0.08)


def wait_until(predicate, process, label, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited with {process.returncode} while waiting for {label}")
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise RuntimeError(f"timeout waiting for {label}")


def ppm_pixels(path):
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError("QEMU screendump is not binary PPM")
    index = 2
    tokens = []
    while len(tokens) < 3:
        while index < len(data) and (data[index] <= 32 or data[index] == ord("#")):
            if data[index] == ord("#"):
                while index < len(data) and data[index] not in (10, 13):
                    index += 1
            else:
                index += 1
        start = index
        while index < len(data) and data[index] > 32:
            index += 1
        tokens.append(data[start:index])
    width, height, maximum = map(int, tokens)
    if maximum != 255:
        raise RuntimeError(f"unexpected PPM channel range {maximum}")
    if index >= len(data) or data[index] > 32:
        raise RuntimeError("malformed PPM header separator")
    if data[index:index + 2] == b"\r\n":
        index += 2
    else:
        index += 1
    pixels = data[index:]
    if len(pixels) != width * height * 3:
        raise RuntimeError(f"unexpected screenshot dimensions {width}x{height}")
    return width, height, pixels


def screenshot(monitor, path):
    monitor.command(f"screendump {path}")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if path.exists() and path.stat().st_size > 32:
            return ppm_pixels(path)
        time.sleep(0.05)
    raise RuntimeError("QEMU did not write a framebuffer screendump")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--bios", required=True)
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    build = pathlib.Path(args.build_dir).resolve()
    with tempfile.TemporaryDirectory(prefix="opensillicon-xorg-") as temp_name:
        temp = pathlib.Path(temp_name)
        root_image = temp / "root.img"
        serial_log = temp / "serial.log"
        monitor_socket = temp / "monitor.sock"
        screen = temp / "screen.ppm"
        shutil.copyfile(build / "root.img", root_image)
        command = [
            args.qemu, "-machine", "q35", "-m", "256M", "-bios", args.bios,
            "-cdrom", str(build / "opensillicon.iso"), "-drive",
            f"file={root_image},if=none,id=rootdisk,format=raw", "-device",
            "ide-hd,drive=rootdisk,bus=ide.0", "-serial", f"file:{serial_log}",
            "-monitor", f"unix:{monitor_socket},server=on,wait=off",
            "-display", "none", "-snapshot", "-no-reboot",
        ]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True)
        monitor = None
        try:
            wait_until(lambda: b"OpenSillicon shell ready" in
                       (serial_log.read_bytes() if serial_log.exists() else b""),
                       process, "shell startup")
            wait_until(lambda: monitor_socket.exists(), process, "QEMU monitor")
            monitor = Monitor(monitor_socket)
            monitor.type("startx")
            monitor.command("sendkey ret")
            wait_until(lambda: b"startx: TinyX, twm and xterm launched" in
                       (serial_log.read_bytes() if serial_log.exists() else b""),
                       process, "graphical session startup", timeout=20)

            width, height, before = screenshot(monitor, screen)
            if width < 600 or height < 300 or not any(before):
                raise RuntimeError("TinyX did not render a visible desktop")
            print(f"PASS: startx opened TinyX at {width}x{height}")

            # The backend starts its pointer at the top-left corner. Move
            # inside xterm explicitly before clicking to give it focus.
            monitor.command("mouse_move -4096 -4096")
            time.sleep(0.1)
            monitor.command("mouse_move 150 150")
            time.sleep(0.1)
            monitor.command("mouse_button 1")
            monitor.command("mouse_button 0")
            monitor.type("echo xterm_input_ok")
            monitor.command("sendkey ret")
            time.sleep(1)
            _, _, after_input = screenshot(monitor, screen)
            if after_input == before:
                raise RuntimeError("xterm keyboard input did not change the framebuffer")
            print("PASS: xterm accepted keyboard input and updated the framebuffer")

            monitor.type("feh /usr/share/feh/images/feh.png")
            monitor.command("sendkey ret")
            time.sleep(2)
            _, _, after_feh = screenshot(monitor, screen)
            red_pixels = sum(1 for i in range(0, len(after_feh), 3)
                             if after_feh[i] > 100 and after_feh[i + 1] < 80
                             and after_feh[i + 2] < 80)
            if red_pixels < 8 or after_feh == after_input:
                serial = serial_log.read_text(errors="replace")
                print("QEMU serial tail:\n" + serial[-3000:])
                screen_copy = build / "xorg-session-failure.ppm"
                shutil.copyfile(screen, screen_copy)
                print(f"Framebuffer screenshot saved to {screen_copy}")
                raise RuntimeError("feh did not display the bundled PNG image")
            print(f"PASS: feh decoded the bundled PNG ({red_pixels} red pixels)")

            monitor.command("quit")
            process.wait(timeout=5)
            if process.returncode != 0:
                raise RuntimeError(f"QEMU exited with status {process.returncode}")
            print("PASS: QEMU exited normally (status 0)")
        finally:
            if monitor is not None:
                monitor.sock.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
