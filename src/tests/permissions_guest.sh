#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
# A fresh, disposable disk keeps the developer's normal root.img untouched.
make iso src/build/populate_root src/build/user_start.o src/build/user_os.o src/build/user_path.o
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
    src/build/permissions-guest/guest.elf src/build/permissions-guest/guest.elf
# QEMU should power off after PASS. A timeout or a failing assertion fails the test.
timeout 45s "${QEMU:-qemu-system-x86_64}" -machine q35 -m 256M \
    -bios "${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE.fd}" \
    -cdrom src/build/opensillicon.iso \
    -drive file=src/build/permissions-guest/root.img,if=none,id=rootdisk,format=raw \
    -device ide-hd,drive=rootdisk,bus=ide.0 -nic none \
    -serial file:src/build/permissions-guest/serial.log -display none -no-reboot
grep -q 'PERMISSIONS GUEST PASS' src/build/permissions-guest/serial.log
if grep -q 'PERMISSIONS GUEST FAIL' src/build/permissions-guest/serial.log; then
    cat src/build/permissions-guest/serial.log
    exit 1
fi
printf '%s\n' 'QEMU permission syscall tests passed'
