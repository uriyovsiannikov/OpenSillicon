# Phase 9 — SilliconFS

Phase 9 implements the v1 disk format accepted in [ADR-003](../OPEN_ADRS.md).
`tools/mksilliconfs.py` creates zero-filled 4 KiB block images with a valid
superblock, allocation maps, inode table, and root directory. The kernel
filesystem uses one-block callbacks, validates the image before returning a
root vnode, and implements VFS lookup, create, read, write, truncate, and
directory enumeration. Hardware block discovery and AHCI remain Phase 10.

SilliconFS v1 uses one-based inode numbers, a bitmap-backed fixed inode table,
12 direct blocks and one 1024-entry indirect block. This caps files and
directories at 1036 blocks, or 4,243,456 bytes. Files may be sparse;
directories may not. The filesystem is unjournaled and does not support
unlink, hard links, symbolic links, timestamps, special files, or concurrent
access locking.

## Verification

`make silliconfs-test` checks formatter geometry and metadata with an odd
129-inode layout, formats a host disk image, mounts it through the same
VFS driver used by the kernel, checks invalid superblocks, maps, inode records,
block pointers and directory records, exercises create/read/write/truncate
including the indirect block and size limit, injects I/O failures, writes the
image back to disk, and remounts the persisted image.
