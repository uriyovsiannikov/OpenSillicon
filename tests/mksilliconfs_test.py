#!/usr/bin/env python3
"""Host-side tests for SilliconFS image geometry and initial metadata."""

from pathlib import Path
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
sys.dont_write_bytecode = True
from mksilliconfs import BLOCK_SIZE, format_image  # noqa: E402


def bit(image: bytes, block: int, number: int) -> bool:
    return bool(image[block * BLOCK_SIZE + number // 8] & (1 << (number % 8)))


def main() -> None:
    with tempfile.TemporaryDirectory() as directory:
        image_path = Path(directory) / "odd-geometry.sfs"
        format_image(image_path, 520, 129)
        image = image_path.read_bytes()
        superblock = bytearray(image[:BLOCK_SIZE])
        checksum = int.from_bytes(superblock[72:76], "little")
        superblock[72:76] = b"\0" * 4

        assert len(image) == 520 * BLOCK_SIZE
        assert image[:8] == b"SILFS1\0\0"
        assert checksum == zlib.crc32(superblock) & 0xFFFFFFFF
        assert int.from_bytes(image[44:48], "little") == 1
        assert int.from_bytes(image[52:56], "little") == 1
        assert int.from_bytes(image[60:64], "little") == 5
        assert int.from_bytes(image[64:68], "little") == 8
        assert all(bit(image, 1, block) for block in range(9))
        assert bit(image, 2, 0)
        assert all(bit(image, 2, inode) for inode in range(129, 32768))

        inode = image[3 * BLOCK_SIZE:3 * BLOCK_SIZE + 128]
        assert int.from_bytes(inode[0:2], "little") == 0o40755
        assert int.from_bytes(inode[16:24], "little") == BLOCK_SIZE
        assert int.from_bytes(inode[24:28], "little") == 8

        try:
            format_image(image_path, 7, 128)
        except ValueError:
            pass
        else:
            raise AssertionError("formatter accepted an undersized image")

    print("SilliconFS formatter geometry and metadata tests passed")


if __name__ == "__main__":
    main()
