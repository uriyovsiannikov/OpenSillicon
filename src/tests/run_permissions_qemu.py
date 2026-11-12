#!/usr/bin/env python3
"""Run the permission guest and provide a real localhost TCP client."""
import os
from pathlib import Path
import socket
import subprocess
import time


def main():
    log = Path('src/build/permissions-guest/serial.log')
    log.write_bytes(b'')
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    command = [
        os.environ.get('QEMU', 'qemu-system-x86_64'),
        '-machine', 'q35', '-m', '256M',
        '-bios', os.environ.get('OVMF_CODE', '/usr/share/OVMF/OVMF_CODE.fd'),
        '-cdrom', 'src/build/opensillicon.iso',
        '-drive', 'file=src/build/permissions-guest/root.img,if=none,id=rootdisk,format=raw',
        '-device', 'ide-hd,drive=rootdisk,bus=ide.0',
        '-netdev', f'user,id=net0,hostfwd=tcp:127.0.0.1:{port}-:23456',
        '-device', 'e1000,netdev=net0',
        '-serial', f'file:{log}', '-display', 'none', '-no-reboot',
    ]
    process = subprocess.Popen(command)
    deadline = time.monotonic() + 45
    exchanged = False
    try:
        while process.poll() is None:
            output = log.read_bytes()
            if b'PERMISSIONS GUEST FAIL' in output:
                raise RuntimeError(output.decode(errors='replace'))
            if not exchanged and b'TCP ACCEPT READY' in output:
                with socket.create_connection(('127.0.0.1', port), timeout=10) as client:
                    client.settimeout(10)
                    client.sendall(b'TCPKEEP')
                    reply = b''
                    while len(reply) < 2:
                        part = client.recv(2 - len(reply))
                        if not part:
                            raise RuntimeError('TCP peer closed before successful accept')
                        reply += part
                    if reply != b'OK':
                        raise RuntimeError(f'Unexpected TCP reply: {reply!r}')
                exchanged = True
            if time.monotonic() >= deadline:
                raise TimeoutError('Permission guest exceeded 45 seconds')
            time.sleep(0.05)
        if process.returncode != 0 or not exchanged:
            raise RuntimeError(f'QEMU exited {process.returncode}; TCP exchange={exchanged}')
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    print('TCP accept integration passed')


if __name__ == '__main__':
    main()
