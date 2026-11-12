#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
# A fresh, disposable disk keeps the developer's normal root.img untouched.
make iso src/build/populate_root src/build/user_start.o src/build/user_os.o src/build/user_path.o src/build/modules/e1000.ko
mkdir -p src/build/permissions-guest
${CC:-cc} -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=small -Wall -Wextra -Werror \
    -Isrc/include -Isrc/user -c src/tests/permissions_guest.c \
    -o src/build/permissions-guest/guest.o
${LD:-ld} -nostdlib -static -no-pie -T src/user/c_linker.ld \
    src/build/user_start.o src/build/user_os.o src/build/user_path.o \
    src/build/permissions-guest/guest.o -o src/build/permissions-guest/guest.elf
python3 src/tools/packers/mksilliconfs.py src/build/permissions-guest/root.img \
    --blocks 4096 --inodes 128
src/build/populate_root src/build/permissions-guest/root.img \
    src/build/permissions-guest/guest.elf src/build/permissions-guest/guest.elf \
    --module src/build/modules/e1000.ko
# The runner supplies a localhost TCP client and enforces the boot timeout.
python3 src/tests/run_permissions_qemu.py
grep -q 'PERMISSIONS GUEST PASS' src/build/permissions-guest/serial.log
if grep -q 'PERMISSIONS GUEST FAIL' src/build/permissions-guest/serial.log; then
    cat src/build/permissions-guest/serial.log
    exit 1
fi
sh src/tests/socket_disk.sh src/build/permissions-guest/root.img
printf '%s\n' 'QEMU permission syscall tests passed'
