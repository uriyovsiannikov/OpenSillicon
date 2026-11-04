#!/usr/bin/env python3
"""Exercise nano editing, saving, reopening and exiting on the native console."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import time

from xorg_session_qemu_test import Monitor, wait_until


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--bios', required=True)
    parser.add_argument('--build-dir', default='build')
    parser.add_argument('--root-image', type=pathlib.Path)
    parser.add_argument('--screenshot', type=pathlib.Path)
    args = parser.parse_args()
    build = pathlib.Path(args.build_dir).resolve()
    with tempfile.TemporaryDirectory(prefix='opensillicon-nano-') as name:
        temp = pathlib.Path(name)
        serial, monitor_path = temp / 'serial.log', temp / 'monitor.sock'
        image = temp / 'root.img'
        shutil.copyfile(args.root_image or build / 'root.img', image)
        process = subprocess.Popen([
            args.qemu, '-machine', 'q35', '-m', '256M', '-bios', args.bios,
            '-cdrom', str(build / 'opensillicon.iso'), '-drive',
            f'file={image},if=none,id=rootdisk,format=raw', '-device',
            'ide-hd,drive=rootdisk,bus=ide.0', '-serial', f'file:{serial}',
            '-monitor', f'unix:{monitor_path},server=on,wait=off',
            '-display', 'none', '-no-reboot'], stderr=subprocess.PIPE)
        monitor = None
        try:
            def output():
                return serial.read_bytes() if serial.exists() else b''

            wait_until(lambda: b'/# ' in output(), process, 'shell')
            wait_until(monitor_path.exists, process, 'QEMU monitor')
            monitor = Monitor(monitor_path)

            def key(name):
                monitor.command('sendkey ' + name)
                time.sleep(0.15)

            def command(text):
                monitor.type(text)
                key('ret')

            command('nano /tmp/nano-test.txt')
            wait_until(lambda: b'nano-test.txt' in output() and b'Write Out' in output(),
                       process, 'nano editor')
            if args.screenshot:
                time.sleep(0.5)
                monitor.command('screendump ' + str(args.screenshot.resolve()))
            monitor.type('nanosavedok')
            key('ctrl-o')
            key('ret')
            wait_until(lambda: b'Wrote 1 line' in output(), process, 'nano save')
            key('ctrl-x')
            wait_until(lambda: b'/# ' in output().split(b'Wrote 1 line')[-1],
                       process, 'return to shell')
            # Reopen the file to exercise reading, then append another line.
            command('nano /tmp/nano-test.txt')
            time.sleep(1)
            key('ctrl-e')
            key('ret')
            monitor.type('reopenedok')
            key('ctrl-o')
            key('ret')
            wait_until(lambda: b'Wrote 2 lines' in output(), process, 'save reopened file')
            key('ctrl-x')
            wait_until(lambda: b'/# ' in output().split(b'Wrote 2 lines')[-1],
                       process, 'second return to shell')
            start = len(output())
            command('cat /tmp/nano-test.txt')
            wait_until(lambda: b'nanosavedok\r\nreopenedok' in output()[start:] or
                       b'nanosavedok\nreopenedok' in output()[start:], process, 'saved contents')
            if b'OpenSillicon panic:' in output():
                raise RuntimeError('kernel panic during nano session')
            print('PASS: nano opened, edited, saved, reopened and exited; cat verified contents')
            monitor.sock.sendall(b'quit\n')
            process.wait(timeout=5)
            if process.returncode != 0:
                raise RuntimeError(f'QEMU exited with {process.returncode}')
        except Exception:
            print(serial.read_text(errors='replace')[-5000:] if serial.exists() else '')
            raise
        finally:
            if monitor is not None:
                monitor.sock.close()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


if __name__ == '__main__':
    main()
