#!/usr/bin/env python3
"""Format a version 1 SilliconFS image."""

import argparse
from pathlib import Path
import struct
import zlib

BLOCK_SIZE = 4096
U32_MAX = (1 << 32) - 1
MAGIC = b"SILFS1\0\0"


def ceil_div(value: int, divisor: int) -> int:
    return value // divisor + (value % divisor != 0)


def put_range(bitmap: bytearray, start: int, end: int) -> None:
    """Set [start, end) bits in one bitmap block."""
    if start >= end:
        return
    first_byte, first_bit = divmod(start, 8)
    last_byte, last_bit = divmod(end, 8)
    if first_byte == last_byte:
        bitmap[first_byte] |= ((1 << (last_bit - first_bit)) - 1) << first_bit
        return
    if first_bit:
        bitmap[first_byte] |= (0xFF << first_bit) & 0xFF
        first_byte += 1
    bitmap[first_byte:last_byte] = b"\xff" * (last_byte - first_byte)
    if last_bit:
        bitmap[last_byte] |= (1 << last_bit) - 1


def superblock(blocks: int, inodes: int, bb_start: int, bb_count: int,
               ib_start: int, ib_count: int, it_start: int, it_count: int,
               data_start: int) -> bytes:
    result = bytearray(BLOCK_SIZE)
    result[:8] = MAGIC
    struct.pack_into("<HHIII", result, 8, 1, 80, BLOCK_SIZE, 0, 0)
    struct.pack_into("<I", result, 24, 0)
    struct.pack_into("<9I", result, 28, blocks, inodes, 1, bb_start,
                     bb_count, ib_start, ib_count, it_start, it_count)
    struct.pack_into("<I", result, 64, data_start)
    checksum = zlib.crc32(result) & U32_MAX
    struct.pack_into("<I", result, 72, checksum)
    return bytes(result)


def directory_record(inode: int, record_length: int, name: bytes,
                     entry_type: int) -> bytes:
    header = struct.pack("<IHBB", inode, record_length, len(name), entry_type)
    contents = header + name
    return contents + bytes(record_length - len(contents))


def format_image(path: Path, blocks: int, inodes: int) -> None:
    if not 1 <= blocks <= U32_MAX:
        raise ValueError("--blocks must be in [1, UINT32_MAX]")
    if not 1 <= inodes <= U32_MAX:
        raise ValueError("--inodes must be in [1, UINT32_MAX]")
    bb_count = ceil_div(blocks, BLOCK_SIZE * 8)
    ib_count = ceil_div(inodes, BLOCK_SIZE * 8)
    bb_start = 1
    ib_start = bb_start + bb_count
    it_start = ib_start + ib_count
    it_count = ceil_div(inodes, BLOCK_SIZE // 128)
    data_start = it_start + it_count
    if data_start + 1 > blocks:
        raise ValueError("image is too small for metadata and the root directory")
    if data_start >= U32_MAX:
        raise ValueError("metadata region exceeds v1 block-number range")

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as image:
        image.truncate(blocks * BLOCK_SIZE)
        image.seek(0)
        image.write(superblock(blocks, inodes, bb_start, bb_count, ib_start,
                               ib_count, it_start, it_count, data_start))

        for map_block in range(bb_count):
            base = map_block * BLOCK_SIZE * 8
            end = base + BLOCK_SIZE * 8
            bits = bytearray(BLOCK_SIZE)
            put_range(bits, 0, min(BLOCK_SIZE * 8, data_start - base))
            if end > blocks:
                put_range(bits, max(0, blocks - base), BLOCK_SIZE * 8)
            if base <= data_start < end:
                bit = data_start - base
                bits[bit // 8] |= 1 << (bit % 8)
            image.seek((bb_start + map_block) * BLOCK_SIZE)
            image.write(bits)

        for map_block in range(ib_count):
            base = map_block * BLOCK_SIZE * 8
            end = base + BLOCK_SIZE * 8
            bits = bytearray(BLOCK_SIZE)
            if base == 0:
                bits[0] |= 1
            if end > inodes:
                put_range(bits, max(0, inodes - base), BLOCK_SIZE * 8)
            image.seek((ib_start + map_block) * BLOCK_SIZE)
            image.write(bits)

        root_inode = bytearray(128)
        struct.pack_into("<HHII", root_inode, 0, 0o40755, 0, 0, 0)
        struct.pack_into("<Q", root_inode, 16, BLOCK_SIZE)
        struct.pack_into("<I", root_inode, 24, data_start)
        image.seek(it_start * BLOCK_SIZE)
        image.write(root_inode)

        root_block = bytearray(BLOCK_SIZE)
        root_block[:12] = directory_record(1, 12, b".", 2)
        root_block[12:] = directory_record(1, BLOCK_SIZE - 12, b"..", 2)
        image.seek(data_start * BLOCK_SIZE)
        image.write(root_block)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--blocks", type=int, required=True,
                        help="filesystem size in 4096-byte blocks")
    parser.add_argument("--inodes", type=int, default=128,
                        help="inode table capacity (default: 128)")
    args = parser.parse_args()
    try:
        format_image(args.image, args.blocks, args.inodes)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
