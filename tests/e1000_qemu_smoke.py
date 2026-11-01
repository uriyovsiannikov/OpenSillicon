#!/usr/bin/env python3
import socket
import struct
import subprocess
import sys
import time


TX_TEXT = b"OpenSillicon e1000 TX 15.2B"
RX_TEXT = b"OS-E1000-RX-15.2B"


def ethernet_frame(source_suffix, ethertype, payload):
	frame = (b"\xff" * 6 + b"\x02\x00\x00\x00\x15" +
	    bytes([source_suffix]) + struct.pack("!H", ethertype) + payload)
	return frame.ljust(60, b"\x00")


def recv_exact(connection, amount):
	data = bytearray()
	while len(data) < amount:
		chunk = connection.recv(amount - len(data))
		if not chunk:
			raise RuntimeError("QEMU socket closed during Ethernet frame")
		data.extend(chunk)
	return bytes(data)


def wait_log(path, marker, process, timeout):
	deadline = time.monotonic() + timeout
	while time.monotonic() < deadline:
		try:
			with open(path, encoding="ascii", errors="replace") as stream:
				content = stream.read()
		except FileNotFoundError:
			content = ""
		if marker in content:
			return content
		if process.poll() is not None:
			raise RuntimeError("QEMU exited before guest marker: " + marker +
			    "\n" + content)
		time.sleep(0.05)
	try:
		with open(path, encoding="ascii", errors="replace") as stream:
			content = stream.read()
	except FileNotFoundError:
		content = ""
	raise RuntimeError("timed out waiting for guest marker " + marker +
	    "\n" + content)


def main():
	qemu, ovmf, iso, disk, serial_path = sys.argv[1:6]
	port_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	port_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	port_listener.bind(("127.0.0.1", 0))
	port_listener.listen(1)
	port_listener.settimeout(90)
	port = port_listener.getsockname()[1]
	qemu_command = [
		qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
		"-cdrom", iso,
		"-drive", "file=" + disk + ",if=none,id=rootdisk,format=raw",
		"-device", "ide-hd,drive=rootdisk,bus=ide.0",
		"-nodefaults", "-nic", "none",
		"-netdev", "socket,id=net0,connect=127.0.0.1:" + str(port),
		"-device", "e1000,netdev=net0,id=e1000,mac=52:54:00:15:02:0b",
		"-serial", "file:" + serial_path, "-display", "none",
		"-no-reboot", "-no-shutdown"
	]
	process = subprocess.Popen(qemu_command, stdout=subprocess.DEVNULL,
	    stderr=subprocess.PIPE, text=True)
	peer = None
	try:
		peer, _ = port_listener.accept()
		peer.settimeout(90)
		tx_length = struct.unpack("!I", recv_exact(peer, 4))[0]
		if tx_length != 60:
			raise RuntimeError("unexpected guest TX frame length " +
			    str(tx_length))
		tx_frame = recv_exact(peer, tx_length)
		expected_tx = ethernet_frame(0x2b, 0x88b5, TX_TEXT)
		if tx_frame != expected_tx:
			raise RuntimeError("guest TX frame bytes differ from deterministic test frame: " +
			    tx_frame.hex())
		print("host peer observed guest Ethernet TX: 60 bytes, exact frame match")
		rx_frame = ethernet_frame(0x2c, 0x88b6, RX_TEXT)
		peer.sendall(struct.pack("!I", len(rx_frame)) + rx_frame)
		print("host peer injected deterministic Ethernet RX: 60 bytes")
		log = wait_log(serial_path,
		    "OpenSillicon: e1000 QEMU TX/RX smoke passed", process, 90)
		for marker in (
		    "OpenSillicon: e1000 DMA rings ready TX/RX 8 descriptors each",
		    "OpenSillicon: e1000 TX descriptor 0 DD complete, frame length 60",
		    "OpenSillicon: e1000 RX descriptor 0 DD/EOP complete, frame length 60",
		    "OpenSillicon: e1000 DMA stopped, bus mastering disabled, rings released",
		):
			if marker not in log:
				raise RuntimeError("missing guest evidence: " + marker + "\n" + log)
		print("guest confirmed TX/RX descriptor completion and safe DMA shutdown")
		print("QEMU e1000 DMA Ethernet smoke passed")
	finally:
		if peer is not None:
			peer.close()
		port_listener.close()
		if process.poll() is None:
			process.terminate()
			try:
				process.wait(timeout=5)
			except subprocess.TimeoutExpired:
				process.kill()
				process.wait()
		if process.stderr is not None:
			stderr = process.stderr.read()
			if process.returncode not in (-15, 0) and stderr:
				print(stderr, file=sys.stderr)


if __name__ == "__main__":
	try:
		main()
	except Exception as error:
		print("e1000 QEMU smoke failed:", error, file=sys.stderr)
		sys.exit(1)
