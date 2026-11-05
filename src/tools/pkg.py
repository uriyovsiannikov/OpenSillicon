#!/usr/bin/env python3
"""Small host-side package tool for OpenSillicon.

Packages are gzip-compressed tar archives containing PKG-MANIFEST.json and
relative payload paths. The format is intentionally simple so the native
package client can consume it without requiring kernel support.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tarfile
import tempfile
from contextlib import contextmanager

MANIFEST = "PKG-MANIFEST.json"
FORMAT = 1


def safe_name(value: str) -> str:
    if not value or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-" for c in value):
        raise ValueError(f"invalid package name: {value!r}")
    return value


def safe_path(value: str) -> str:
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(p in ("", ".", "..") for p in path.parts):
        raise ValueError(f"unsafe package path: {value!r}")
    return str(path)


def create(args):
    name = safe_name(args.name)
    source = Path(args.root).resolve()
    files = []
    for path in sorted(source.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"symbolic links are not supported: {path}")
        if path.is_file():
            rel = path.relative_to(source).as_posix()
            safe_path(rel)
            data = path.read_bytes()
            files.append({"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                          "mode": path.stat().st_mode & 0o777})
    if not files:
        raise ValueError("package payload is empty")
    manifest = {"format": FORMAT, "name": name, "version": args.version,
                "description": args.description, "depends": args.depends, "files": files}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as tmp:
        temporary = Path(tmp.name)
    try:
        with tarfile.open(temporary, "w:gz", format=tarfile.PAX_FORMAT) as archive:
            info = tarfile.TarInfo(MANIFEST)
            raw = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode()
            info.size, info.mode, info.mtime = len(raw), 0o644, 0
            import io
            archive.addfile(info, io.BytesIO(raw))
            for item in files:
                path = source / item["path"]
                info = archive.gettarinfo(str(path), arcname="payload/" + item["path"])
                info.mtime = 0
                with path.open("rb") as stream:
                    archive.addfile(info, stream)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"created {output} ({name}-{args.version}, {len(files)} files)")


@contextmanager
def read_package(path):
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        names = [m.name for m in members]
        if len(names) != len(set(names)) or names.count(MANIFEST) != 1:
            raise ValueError("archive has duplicate entries or no unique manifest")
        manifest_member = archive.getmember(MANIFEST)
        if not manifest_member.isfile() or manifest_member.size > 4 * 1024 * 1024:
            raise ValueError("invalid manifest entry")
        manifest = json.load(archive.extractfile(manifest_member))
        if manifest.get("format") != FORMAT or not isinstance(manifest.get("files"), list):
            raise ValueError("unsupported or malformed package manifest")
        expected = {"payload/" + safe_path(item["path"]): item for item in manifest["files"]}
        if len(expected) != len(manifest["files"]):
            raise ValueError("manifest contains duplicate paths")
        actual = {m.name: m for m in members if m.name != MANIFEST}
        if set(actual) != set(expected):
            raise ValueError("archive payload does not match manifest")
        yield archive, manifest, actual, expected


def verify(args):
    with read_package(args.package) as (archive, manifest, actual, expected):
        for name, item in expected.items():
            member = actual[name]
            if not member.isfile() or member.size != item["size"]:
                raise ValueError(f"invalid payload entry: {name}")
            digest = hashlib.sha256(archive.extractfile(member).read()).hexdigest()
            if digest != item["sha256"]:
                raise ValueError(f"checksum mismatch: {name}")
    print(f"verified {manifest['name']}-{manifest['version']} ({len(expected)} files)")


def install(args):
    root = Path(args.root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    with read_package(args.package) as (archive, manifest, actual, expected):
        db = root / "var/db/pkg"
        dbfile = db / (safe_name(manifest["name"]) + ".json")
        if dbfile.exists() and not args.replace:
            raise ValueError(f"{manifest['name']} is already installed (use --replace)")
        # Verify the complete archive before writing any payload files.
        contents = {}
        for arcname, item in expected.items():
            member = actual[arcname]
            data = archive.extractfile(member).read()
            if len(data) != item["size"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
                raise ValueError(f"checksum mismatch: {item['path']}")
            contents[item["path"]] = (data, item)
        for rel, (data, item) in contents.items():
            target = root.joinpath(*PurePosixPath(safe_path(rel)).parts)
            if target.exists() and not args.replace:
                raise ValueError(f"refusing to overwrite existing file: /{rel}")
            resolved_parent = target.parent.resolve()
            if not resolved_parent.is_relative_to(root):
                raise ValueError(f"package path escapes installation root: /{rel}")
            if target.is_symlink():
                raise ValueError(f"refusing to replace symbolic link: /{rel}")
        for rel, (data, item) in contents.items():
            target = root.joinpath(*PurePosixPath(rel).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(item["mode"] & 0o777)
        record = {"format": FORMAT, "name": manifest["name"], "version": manifest["version"],
                  "files": [x["path"] for x in manifest["files"]]}
        db.mkdir(parents=True, exist_ok=True)
        dbfile.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n")
    print(f"installed {manifest['name']}-{manifest['version']} into {root}")


def remove(args):
    root = Path(args.root).resolve()
    dbfile = root / "var/db/pkg" / (safe_name(args.name) + ".json")
    record = json.loads(dbfile.read_text())
    for rel in record["files"]:
        target = root.joinpath(*PurePosixPath(safe_path(rel)).parts)
        if not target.parent.resolve().is_relative_to(root) or target.is_symlink():
            raise ValueError(f"refusing to remove path outside root or symbolic link: /{rel}")
        if target.is_file():
            target.unlink()
    dbfile.unlink()
    print(f"removed {args.name}-{record['version']} from {root}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("create", help="build a .ospkg archive from a staging directory")
    p.add_argument("name"); p.add_argument("version"); p.add_argument("root"); p.add_argument("output")
    p.add_argument("--description", default=""); p.add_argument("--depends", nargs="*", default=[])
    p.set_defaults(func=create)
    p = sub.add_parser("verify", help="validate manifest and payload checksums")
    p.add_argument("package"); p.set_defaults(func=verify)
    p = sub.add_parser("install", help="install into a host-side root directory")
    p.add_argument("package"); p.add_argument("root"); p.add_argument("--replace", action="store_true")
    p.set_defaults(func=install)
    p = sub.add_parser("remove", help="remove files recorded for a package")
    p.add_argument("name"); p.add_argument("root"); p.set_defaults(func=remove)
    args = parser.parse_args()
    try:
        args.func(args)
    except (OSError, ValueError, KeyError, tarfile.TarError, json.JSONDecodeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
