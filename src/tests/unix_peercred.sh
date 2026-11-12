#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -DKERNEL_SPINLOCK_HOST_TEST -Isrc/include -Isrc/limine src/tests/unix_peercred.c \
    src/kernel/ipc/unix_socket.c src/kernel/proc/fd.c \
    -Wl,--gc-sections -o "$test_binary"
"$test_binary"
