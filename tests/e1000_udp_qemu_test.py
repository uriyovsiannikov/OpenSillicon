#!/usr/bin/env python3
import socket
import struct
import subprocess
import sys
import time

GUEST_MAC = bytes.fromhex("525400150301")
PEER_MAC = bytes.fromhex("525400150302")
GUEST_IP = bytes((10, 15, 3, 1))
PEER_IP = bytes((10, 15, 3, 2))
GUEST_PORT = 45000
PEER_PORT = 45001
REQUEST = b"OS-UDP-REQ-15.3B"
RESPONSE = b"OS-UDP-REPLY-15.3B"


def checksum(data):
    if len(data) & 1:
        data += b"\x00"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xffff) + (total >> 16)
    return (~total) & 0xffff


def udp_checksum(source, destination, udp):
    pseudo = source + destination + b"\x00\x11" + struct.pack("!H", len(udp))
    return checksum(pseudo + udp)


def recv_exact(connection, amount):
    output = bytearray()
    while len(output) < amount:
        part = connection.recv(amount - len(output))
        if not part:
            raise RuntimeError("QEMU closed the Ethernet backend")
        output.extend(part)
    return bytes(output)


def recv_frame(connection):
    length = struct.unpack("!I", recv_exact(connection, 4))[0]
    if length < 14 or length > 2048:
        raise RuntimeError("invalid Ethernet frame length %d" % length)
    return recv_exact(connection, length)


def send_frame(connection, frame):
    connection.sendall(struct.pack("!I", len(frame)) + frame)


def make_arp_reply():
    arp = struct.pack("!HHBBH", 1, 0x0800, 6, 4, 2)
    arp += PEER_MAC + PEER_IP + GUEST_MAC + GUEST_IP
    return (GUEST_MAC + PEER_MAC + b"\x08\x06" + arp).ljust(60, b"\x00")


def make_udp_frame(source_port, destination_port, payload):
    udp = struct.pack("!HHHH", source_port, destination_port, 8 + len(payload), 0) + payload
    check = udp_checksum(PEER_IP, GUEST_IP, udp)
    udp = udp[:6] + struct.pack("!H", check or 0xffff) + udp[8:]
    ip = bytearray(20)
    ip[0] = 0x45
    struct.pack_into("!H", ip, 2, 20 + len(udp))
    struct.pack_into("!H", ip, 4, 0x153b)
    ip[8] = 64
    ip[9] = 17
    ip[12:16] = PEER_IP
    ip[16:20] = GUEST_IP
    struct.pack_into("!H", ip, 10, checksum(bytes(ip)))
    return (GUEST_MAC + PEER_MAC + b"\x08\x00" + bytes(ip) + udp).ljust(60, b"\x00")


def validate_guest_datagram(frame):
    if len(frame) < 14 + 20 + 8 or frame[:6] != PEER_MAC or frame[6:12] != GUEST_MAC:
        raise RuntimeError("guest UDP Ethernet addresses/length mismatch: " + frame.hex())
    if frame[12:14] != b"\x08\x00":
        raise RuntimeError("guest did not send IPv4")
    ip = frame[14:]
    if ip[0] != 0x45 or ip[9] != 17 or checksum(ip[:20]) != 0:
        raise RuntimeError("guest IPv4 header malformed or checksum invalid")
    total = struct.unpack_from("!H", ip, 2)[0]
    if total < 28 or 14 + total > len(frame):
        raise RuntimeError("guest IPv4 length out of bounds")
    if ip[12:16] != GUEST_IP or ip[16:20] != PEER_IP:
        raise RuntimeError("guest IPv4 source/destination mismatch")
    udp = ip[20:total]
    source_port, destination_port, udp_len, check = struct.unpack_from("!HHHH", udp)
    if udp_len != len(udp) or udp_len < 8 or not check:
        raise RuntimeError("guest UDP length/checksum field invalid")
    if udp_checksum(GUEST_IP, PEER_IP, udp) != 0:
        raise RuntimeError("guest UDP pseudoheader checksum invalid")
    if (source_port, destination_port, udp[8:]) != (GUEST_PORT, PEER_PORT, REQUEST):
        raise RuntimeError("guest UDP endpoints or payload mismatch: %r %r %r" %
            (source_port, destination_port, udp[8:]))
    print("peer observed guest UDP request: ports %d->%d, payload %r, IPv4/UDP checksums valid" %
        (source_port, destination_port, udp[8:]))


def wait_guest_log(path, process, timeout=60):
    marker = "OpenSillicon: user UDP socket send/receive/close passed; e1000 stopped"
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
            raise RuntimeError("QEMU exited before completion:\n" + content)
        time.sleep(0.05)
    try:
        with open(path, encoding="ascii", errors="replace") as stream:
            content = stream.read()
    except FileNotFoundError:
        content = ""
    raise RuntimeError("guest UDP test timed out:\n" + content)


def main():
    netdiag_mode = len(sys.argv) == 7 and sys.argv[6] == "--netdiag"
    if len(sys.argv) not in (6, 7) or (len(sys.argv) == 7 and not netdiag_mode):
        raise SystemExit("usage: e1000_udp_qemu_test.py QEMU OVMF ISO ROOT LOG [--netdiag]")
    qemu, ovmf, iso, disk, serial_path = sys.argv[1:6]
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(60)
    port = listener.getsockname()[1]
    command = [qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
        "-cdrom", iso, "-drive", "file=" + disk + ",if=none,id=rootdisk,format=raw",
        "-device", "ide-hd,drive=rootdisk,bus=ide.0", "-nodefaults", "-nic", "none",
        "-netdev", "socket,id=net0,connect=127.0.0.1:" + str(port),
        "-device", "e1000,netdev=net0,id=e1000,mac=52:54:00:15:03:01",
        "-serial", "file:" + serial_path, "-display", "none",
        "-no-reboot", "-no-shutdown"]
    print("QEMU command: " + " ".join(command))
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True)
    peer = None
    try:
        peer, _ = listener.accept()
        peer.settimeout(30)
        frame = recv_frame(peer)
        if len(frame) < 42 or frame[12:14] != b"\x08\x06":
            raise RuntimeError("expected guest ARP request, got " + frame.hex())
        arp = frame[14:42]
        if (struct.unpack("!HHBBH", arp[:8]) != (1, 0x0800, 6, 4, 1) or
            frame[6:12] != GUEST_MAC or arp[8:14] != GUEST_MAC or
            arp[14:18] != GUEST_IP or arp[24:28] != PEER_IP):
            raise RuntimeError("ARP request mismatch: " + frame.hex())
        print("peer received guest ARP request for 10.15.3.2")
        send_frame(peer, make_arp_reply())
        print("peer sent ARP reply from 10.15.3.2 / 52:54:00:15:03:02")
        request = recv_frame(peer)
        validate_guest_datagram(request)
        send_frame(peer, make_udp_frame(PEER_PORT, GUEST_PORT, RESPONSE))
        print("peer sent UDP reply: ports %d->%d, payload %r, checksum valid" %
            (PEER_PORT, GUEST_PORT, RESPONSE))
        log = wait_guest_log(serial_path, process)
        guest_markers = [
            "OpenSillicon: static IPv4 UDP profile 10.15.3.1/24 initialized",
            "OpenSillicon: user UDP datagram sent",
            "OpenSillicon: user UDP reply payload/source verified",
            "OpenSillicon: user UDP socket send/receive/close passed; e1000 stopped"]
        if netdiag_mode:
            guest_markers[1:3] = [
                "netdiag: request sent; waiting for one UDP reply",
                "netdiag: UDP reply verified from 10.15.3.2:45001"]
        for expected in guest_markers:
            if expected not in log:
                raise RuntimeError("missing guest log marker %r\n%s" % (expected, log))
        if netdiag_mode:
            print("guest serial confirmed netdiag UDP bind/send/receive/peer/payload/close")
            print("e1000 userspace netdiag QEMU test passed")
        else:
            print("guest serial confirmed socket bind/send/receive/close and process status zero")
            print("e1000 guest UDP round-trip QEMU test passed")
    finally:
        if peer is not None:
            peer.close()
        listener.close()
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
        print("e1000 UDP QEMU test failed: " + str(error), file=sys.stderr)
        sys.exit(1)
