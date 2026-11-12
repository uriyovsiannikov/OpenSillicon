#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-pic -fno-pie -m64 -mno-red-zone -mgeneral-regs-only \
    -Wall -Wextra -Werror -nostdinc -Isrc/include -Isrc/user/libos/include \
    -Isrc/user/libos/src -isystem "$("${CC:-cc}" -print-file-name=include)" \
    -ffunction-sections -fdata-sections -nostdlib -static -no-pie \
    src/tests/libos_credentials.c src/user/libos/src/unistd.c \
    -Wl,--gc-sections -Wl,-e,_start -o "$test_binary"
"$test_binary"
printf '%s\n' 'Libos credential failure tests passed'
