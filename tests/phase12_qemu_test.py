#!/usr/bin/env python3
import json
import os
import socket
import subprocess
import sys
import tempfile
import time


def qmp(connection, stream, command, arguments=None):
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


def serial_text(path):
	try:
		with open(path, encoding="ascii", errors="replace") as log:
			return log.read()
	except FileNotFoundError:
		return ""


def send_line(connection, stream, line):
	qcodes = {" ": "spc", "/": "slash", ".": "dot", "_": "underscore",
	    "-": "minus", "\t": "tab"}
	for character in line:
		qcode = qcodes.get(character, character.lower())
		qmp(connection, stream, "send-key", {"keys": [{"type": "qcode",
		    "data": qcode}]})
		time.sleep(0.025)
	qmp(connection, stream, "send-key", {"keys": [{"type": "qcode",
	    "data": "ret"}]})


def run_interactive(qemu, ovmf, iso, disk, work):
	qmp_path = os.path.join(work, "qmp.sock")
	serial_path = os.path.join(work, "serial.log")
	process = subprocess.Popen([
		qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
		"-cdrom", iso, "-drive", "file=" + disk + ",if=none,id=rootdisk,format=raw",
		"-device", "ide-hd,drive=rootdisk,bus=ide.0", "-serial",
		"file:" + serial_path, "-display", "none", "-no-reboot",
		"-no-shutdown", "-qmp", "unix:" + qmp_path + ",server=on,wait=off"
	], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	try:
		wait_for(lambda: os.path.exists(qmp_path), 15, "QMP socket")
		connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		connection.connect(qmp_path)
		stream = connection.makefile("rb")
		json.loads(stream.readline())
		qmp(connection, stream, "qmp_capabilities")
		wait_for(lambda: "OpenSillicon shell ready" in serial_text(serial_path),
		    60, "userspace init and shell")
		initial = serial_text(serial_path)
		if "OpenSillicon init: starting /bin/sh" not in initial:
			raise RuntimeError("init startup checks did not complete")

		commands = [
			("", None),
			("/init/bin/argv_probe", "argv argc=1"),
			("/init/bin/argv_probe   alpha\tbeta  x,y:!",
			    "argv[3]=x,y:!"),
			("/init/bin/argv_probe a b c d e f g h i j k l m n o",
			    "argv argc=16"),
			("/init/bin/argv_probe a b c d e f g h i j k l m n o p",
			    "sh: too many arguments"),
			("/init/bin/argv_limit_test", "argv limits passed"),
			("help", "external commands: ls cat echo pwd true false"),
			("echo", None),
			("pwd", None),
			("cd /etc", None),
			("pwd", "/etc# "),
			("ls", "bin"),
			("ls /etc/passwd", "/etc/passwd"),
			("cat /etc/passwd", "root:*:0:0:OpenSillicon root"),
			("uname -sm", "OpenSillicon x86_64"),
			("true", None),
			("false", "sh: command exited status 1"),
			("echo phase12-shell-ok", "phase12-shell-ok"),
			("probe", "external ELF probe passed"),
			("badcmd", "sh: exec: /bin/badcmd failed"),
		]
		for command, marker in commands:
			send_line(connection, stream, command)
			if marker is not None:
				try:
					wait_for(lambda marker=marker: marker in serial_text(serial_path),
					    15, "shell command " + command)
				except RuntimeError as error:
					raise RuntimeError(str(error) + "\nserial:\n" +
					    serial_text(serial_path))
			else:
				time.sleep(1)
		send_line(connection, stream, "exit")
		wait_for(lambda: serial_text(serial_path).count("OpenSillicon shell ready") >= 2,
		    15, "init shell restart after exit")
		connection.close()
	finally:
		process.terminate()
		try:
			process.wait(timeout=5)
		except subprocess.TimeoutExpired:
			process.kill()
			process.wait()
	log = serial_text(serial_path)
	if "OpenSillicon: external ELF probe passed" not in log:
		raise RuntimeError("external ELF execution was not observed")
	if "OpenSillicon init: restarting /bin/sh" not in log:
		raise RuntimeError("init did not report shell restart")
	print("QEMU Phase 12 persistent-root init, shell, TTY, exec, and restart passed")


def run_missing_init(qemu, ovmf, iso, disk, work):
	serial_path = os.path.join(work, "missing-init.log")
	process = subprocess.Popen([
		qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
		"-cdrom", iso, "-drive", "file=" + disk + ",if=none,id=rootdisk,format=raw",
		"-device", "ide-hd,drive=rootdisk,bus=ide.0", "-serial",
		"file:" + serial_path, "-display", "none", "-no-reboot", "-no-shutdown"
	], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	try:
		wait_for(lambda: "init missing or failed to load; halting" in
		    serial_text(serial_path), 60, "missing-init halt diagnostic")
	finally:
		process.terminate()
		try:
			process.wait(timeout=5)
		except subprocess.TimeoutExpired:
			process.kill()
			process.wait()
	print("QEMU missing-init diagnostic and halt passed")


def run_crashing_shell(qemu, ovmf, iso, disk, work):
	serial_path = os.path.join(work, "crashing-shell.log")
	process = subprocess.Popen([
		qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
		"-cdrom", iso, "-drive", "file=" + disk + ",if=none,id=rootdisk,format=raw",
		"-device", "ide-hd,drive=rootdisk,bus=ide.0", "-serial",
		"file:" + serial_path, "-display", "none", "-no-reboot", "-no-shutdown"
	], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	try:
		wait_for(lambda: "shell exited status 134" in serial_text(serial_path),
		    60, "init reaping crashed shell")
		wait_for(lambda: "restarting /bin/sh" in serial_text(serial_path),
		    15, "init restarting crashed shell")
	finally:
		process.terminate()
		try:
			process.wait(timeout=5)
		except subprocess.TimeoutExpired:
			process.kill()
			process.wait()
	print("QEMU init crash reap and delayed shell restart passed")


def main():
	qemu, ovmf, iso, disk, blank_disk, bad_init_disk, crash_disk = sys.argv[1:8]
	with tempfile.TemporaryDirectory(prefix="opensillicon-phase12-") as work:
		run_interactive(qemu, ovmf, iso, disk, work)
		run_missing_init(qemu, ovmf, iso, blank_disk, work)
		run_missing_init(qemu, ovmf, iso, bad_init_disk, work)
		run_crashing_shell(qemu, ovmf, iso, crash_disk, work)


if __name__ == "__main__":
	try:
		main()
	except Exception as error:
		print("Phase 12 QEMU test failed:", error, file=sys.stderr)
		sys.exit(1)
