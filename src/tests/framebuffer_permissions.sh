#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Isrc/include -Isrc/limine src/tests/framebuffer_permissions.c \
    src/kernel/tty/framebuffer_device.c src/kernel/tty/tty.c \
    -Wl,--gc-sections -o "$test_binary"
"$test_binary"
