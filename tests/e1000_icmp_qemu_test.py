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
ECHO_ID = 0x153A
ECHO_SEQUENCE = 1
ECHO_PAYLOAD = b"OS-PHASE15.3A-ECHO"


def checksum(data):
    if len(data) & 1:
        data += b"\x00"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xffff) + (total >> 16)
    return (~total) & 0xffff


def recv_exact(connection, amount):
    result = bytearray()
    while len(result) < amount:
        part = connection.recv(amount - len(result))
        if not part:
            raise RuntimeError("QEMU closed the Ethernet backend")
        result.extend(part)
    return bytes(result)


def recv_frame(connection):
    length = struct.unpack("!I", recv_exact(connection, 4))[0]
    if length < 14 or length > 2048:
        raise RuntimeError("invalid QEMU Ethernet frame length: %d" % length)
    return recv_exact(connection, length)


def send_frame(connection, frame):
    connection.sendall(struct.pack("!I", len(frame)) + frame)


def arp_reply():
    body = struct.pack("!HHBBH", 1, 0x0800, 6, 4, 2)
    body += PEER_MAC + PEER_IP + GUEST_MAC + GUEST_IP
    return (GUEST_MAC + PEER_MAC + struct.pack("!H", 0x0806) + body).ljust(60, b"\x00")


def echo_reply(request):
    if len(request) < 42 or request[12:14] != b"\x08\x00":
        raise RuntimeError("guest did not transmit an IPv4 Ethernet frame")
    ip = request[14:]
    if ip[0] != 0x45 or ip[9] != 1 or checksum(ip[:20]) != 0:
        raise RuntimeError("guest IPv4 header invalid or unsupported")
    total = struct.unpack("!H", ip[2:4])[0]
    if total < 28 or 14 + total > len(request):
        raise RuntimeError("guest IPv4 total length invalid")
    if ip[12:16] != GUEST_IP or ip[16:20] != PEER_IP:
        raise RuntimeError("guest IPv4 source/destination mismatch")
    icmp = bytearray(ip[20:total])
    if len(icmp) != 8 + len(ECHO_PAYLOAD) or icmp[0:2] != b"\x08\x00":
        raise RuntimeError("guest ICMP Echo Request shape mismatch")
    if checksum(icmp) != 0:
        raise RuntimeError("guest ICMP checksum invalid")
    identifier, sequence = struct.unpack("!HH", icmp[4:8])
    if identifier != ECHO_ID or sequence != ECHO_SEQUENCE or icmp[8:] != ECHO_PAYLOAD:
        raise RuntimeError("guest ICMP identifier, sequence, or payload mismatch")
    print("peer verified guest IPv4/ICMP Echo Request: %d bytes, checksum valid" % total)

    icmp[0] = 0
    icmp[2:4] = b"\x00\x00"
    icmp[2:4] = struct.pack("!H", checksum(bytes(icmp)))
    response_ip = bytearray(20)
    response_ip[0] = 0x45
    struct.pack_into("!H", response_ip, 2, 20 + len(icmp))
    struct.pack_into("!H", response_ip, 4, 0x153a)
    response_ip[8] = 64
    response_ip[9] = 1
    response_ip[12:16] = PEER_IP
    response_ip[16:20] = GUEST_IP
    struct.pack_into("!H", response_ip, 10, checksum(bytes(response_ip)))
    frame = GUEST_MAC + PEER_MAC + b"\x08\x00" + response_ip + icmp
    return frame.ljust(60, b"\x00")


def wait_guest_log(path, process, marker, timeout=60):
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
            raise RuntimeError("QEMU exited before guest completion:\n" + content)
        time.sleep(0.05)
    try:
        with open(path, encoding="ascii", errors="replace") as stream:
            content = stream.read()
    except FileNotFoundError:
        content = ""
    raise RuntimeError("guest protocol test timed out:\n" + content)


def main():
    qemu, ovmf, iso, disk, serial_path = sys.argv[1:6]
    userspace_ping = len(sys.argv) == 7 and sys.argv[6] == "--userspace-ping"
    if len(sys.argv) not in (6, 7) or (len(sys.argv) == 7 and not userspace_ping):
        raise SystemExit("usage: e1000_icmp_qemu_test.py QEMU OVMF ISO ROOT LOG [--userspace-ping]")
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
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True)
    peer = None
    try:
        peer, _ = listener.accept()
        peer.settimeout(30)
        frame = recv_frame(peer)
        if frame[12:14] != b"\x08\x06" or len(frame) < 42:
            raise RuntimeError("expected guest ARP request")
        arp = frame[14:42]
        if (struct.unpack("!HHBBH", arp[:8]) != (1, 0x0800, 6, 4, 1) or
            frame[6:12] != GUEST_MAC or arp[8:14] != GUEST_MAC or
            arp[14:18] != GUEST_IP or arp[24:28] != PEER_IP):
            raise RuntimeError("guest ARP request fields mismatch: " + frame.hex())
        print("peer verified guest ARP request: 60-byte Ethernet frame")
        send_frame(peer, arp_reply())
        print("peer injected ARP reply for 10.15.3.2 / 52:54:00:15:03:02")

        frame = recv_frame(peer)
        response = echo_reply(frame)
        send_frame(peer, response)
        print("peer injected matching IPv4 ICMP Echo Reply: 60-byte Ethernet frame")

        marker = ("OpenSillicon: userspace ICMP ping passed; e1000 stopped"
            if userspace_ping else
            "OpenSillicon: guest ARP/IPv4/ICMP smoke passed")
        log = wait_guest_log(serial_path, process, marker)
        expected = ["Initializing QEMU e1000 82540EM ..."]
        if userspace_ping:
            expected.extend(("ping: reply from 10.15.3.2 sequence=1",
                "OpenSillicon: userspace ICMP ping passed; e1000 stopped"))
        else:
            expected.extend((
                "OpenSillicon: ARP request transmitted for 10.15.3.2",
                "OpenSillicon: ARP peer resolved 10.15.3.2",
                "OpenSillicon: ICMP Echo Request transmitted"))
        for expected_line in expected:
            if expected_line not in log:
                raise RuntimeError("missing guest evidence: " + expected_line + "\n" + log)
        print("guest serial confirmed matching ICMP Echo Reply")
        print("QEMU userspace ICMP Echo test passed" if userspace_ping else
            "QEMU bounded ARP/IPv4/ICMP exchange passed")
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
        print("e1000 ICMP QEMU test failed:", error, file=sys.stderr)
        sys.exit(1)
