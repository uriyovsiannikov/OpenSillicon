#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Isrc/include src/tests/socket_disk.c src/kernel/fs/vfs/vfs.c \
    src/kernel/fs/silliconfs/silliconfs.c -Wl,--gc-sections -o "$test_binary"
"$test_binary" "${1:-src/build/permissions-guest/root.img}"
