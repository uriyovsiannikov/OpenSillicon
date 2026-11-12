#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Isrc/include -Isrc/limine -Isrc/kernel/net/cyclonetcp/opensillicon \
    -Isrc/kernel/net/cyclonetcp -Isrc/kernel/net/cyclonetcp/common \
    src/tests/tcp_accept.c src/kernel/proc/fd.c -Wl,--gc-sections -o "$test_binary"
"$test_binary"
