#!/usr/bin/env python3
"""Create a deterministic, uncompressed `newc` CPIO initramfs."""

import argparse
from pathlib import Path
import stat


def align4(data: bytearray) -> None:
    data.extend(b"\0" * (-len(data) % 4))


def append_entry(data: bytearray, inode: int, name: str, mode: int,
                 contents: bytes) -> None:
    name_bytes = name.encode("utf-8") + b"\0"
    if len(name_bytes) > 256 or len(contents) > 0xFFFFFFFF:
        raise ValueError(f"initramfs entry exceeds format limits: {name}")
    fields = (
        inode,
        mode,
        0,
        0,
        2 if stat.S_ISDIR(mode) else 1,
        0,
        len(contents),
        0,
        0,
        0,
        0,
        len(name_bytes),
        0,
    )
    if any(field < 0 or field > 0xFFFFFFFF for field in fields):
        raise ValueError(f"initramfs metadata exceeds newc limits: {name}")
    header = b"070701" + b"".join(f"{field:08X}".encode("ascii")
                                  for field in fields)
    if len(header) != 110:
        raise ValueError("invalid newc header length")
    data.extend(header)
    data.extend(name_bytes)
    align4(data)
    data.extend(contents)
    align4(data)


def make_archive(source: Path, extra_files: list[tuple[str, Path]]) -> bytes:
    if not source.is_dir():
        raise ValueError(f"initramfs source is not a directory: {source}")

    entries: dict[str, tuple[int, bytes]] = {}
    paths = sorted(source.rglob("*"), key=lambda item: item.relative_to(source).as_posix())
    for path in paths:
        metadata = path.lstat()
        if stat.S_ISDIR(metadata.st_mode):
            mode = stat.S_IFDIR | stat.S_IMODE(metadata.st_mode)
            contents = b""
        elif stat.S_ISREG(metadata.st_mode):
            mode = stat.S_IFREG | stat.S_IMODE(metadata.st_mode)
            contents = path.read_bytes()
        else:
            raise ValueError(f"unsupported initramfs entry type: {path}")
        entries[path.relative_to(source).as_posix()] = (mode, contents)
    for name, path in extra_files:
        name = name.strip("/")
        if not name or name == "." or ".." in name.split("/") or name in entries:
            raise ValueError(f"invalid or duplicate initramfs path: {name}")
        for parent in Path(name).parents:
            parent_name = parent.as_posix()
            if parent_name != "." and parent_name not in entries:
                entries[parent_name] = (stat.S_IFDIR | 0o755, b"")
        entries[name] = (stat.S_IFREG | 0o755, path.read_bytes())
    archive = bytearray()
    inode = 1
    for name, (mode, contents) in sorted(entries.items()):
        append_entry(archive, inode, name, mode, contents)
        inode += 1

    append_entry(archive, inode, "TRAILER!!!", 0, b"")
    archive.extend(b"\0" * (-len(archive) % 512))
    return bytes(archive)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--file", action="append", default=[],
                        metavar="ARCHIVE_PATH=HOST_PATH",
                        help="add an executable file to the archive")
    args = parser.parse_args()
    extra_files = []
    for specification in args.file:
        name, separator, host_path = specification.partition("=")
        if not separator or not host_path:
            parser.error("--file requires ARCHIVE_PATH=HOST_PATH")
        extra_files.append((name, Path(host_path)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_archive(args.source, extra_files))


if __name__ == "__main__":
    main()
