#!/usr/bin/env python3
"""One-shot deterministic Linux TCP listener for the isolated veth test."""

import socket
import sys
import time

ADDRESS = "10.55.0.2"
PORT = 19090
REQUEST_LENGTH = 1800
RESPONSE_LENGTH = 1750


def pattern(length, multiplier, addend):
    return bytes((i * multiplier + addend) & 0xFF for i in range(length))


def recv_exact(sock, length):
    result = bytearray()
    while len(result) < length:
        part = sock.recv(length - len(result))
        if not part:
            raise RuntimeError("Linux peer saw EOF before the complete request")
        result.extend(part)
    return bytes(result)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "server"
    if mode == "passive-client":
        # C's bounded raw-packet endpoint owns the passive listener.
        print("LINUX_CLIENT_READY", flush=True)
        deadline = time.monotonic() + 15
        while True:
            try:
                connection = socket.create_connection(("10.55.0.1", PORT), timeout=2)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)
        connection.settimeout(10)
        connection.shutdown(socket.SHUT_WR)
        while connection.recv(64):
            pass
        connection.close()
        print("Linux active client observed orderly EOF", flush=True)
        return
    if mode != "server":
        raise RuntimeError("unknown peer mode")
    expected = pattern(REQUEST_LENGTH, 17, 9)
    response = pattern(RESPONSE_LENGTH, 13, 7)
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.settimeout(15)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((ADDRESS, PORT))
    listener.listen(1)
    print("LINUX_LISTENER_READY", flush=True)
    connection, peer = listener.accept()
    connection.settimeout(15)
    if peer[0] != "10.55.0.1" or peer[1] != 42000:
        raise RuntimeError("unexpected OpenSillicon source tuple: %r" % (peer,))
    request = recv_exact(connection, REQUEST_LENGTH)
    if request != expected:
        raise RuntimeError("request payload mismatch")
    connection.sendall(response)
    # Wait for OpenSillicon's FIN. Closing this socket then sends the peer FIN.
    if connection.recv(1) != b"":
        raise RuntimeError("unexpected bytes after fixed request")
    print("Linux independently verified request=1800, response=1750, orderly EOF",
          flush=True)
    connection.close()
    listener.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("Linux peer failure: %s" % error, file=sys.stderr, flush=True)
        raise
