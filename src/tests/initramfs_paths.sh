#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
# Put the file before its directory entries to exercise implicit parents too.
python3 - "$test_dir/archive.cpio" <<'PY'
import runpy
import sys
from pathlib import Path
append_entry = runpy.run_path('src/tools/packers/mkinitramfs.py')['append_entry']
archive = bytearray()
entries = [
    ('private/inner/file', 0o100644, b'private'),
    ('private', 0o40700, b''),
    ('private/inner', 0o40755, b''),
    ('public/file', 0o100644, b'public'),
    ('TRAILER!!!', 0, b''),
]
for inode, (name, mode, contents) in enumerate(entries, 1):
    append_entry(archive, inode, name, mode, contents)
Path(sys.argv[1]).write_bytes(archive)
PY
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Isrc/include -Isrc/limine src/tests/initramfs_paths.c \
    src/kernel/fs/initramfs/initramfs.c src/kernel/fs/vfs/vfs.c \
    src/kernel/fs/ramfs/ramfs.c src/kernel/proc/fd.c -Wl,--gc-sections \
    -o "$test_dir/test"
"$test_dir/test" "$test_dir/archive.cpio"
