#!/usr/bin/env python3
"""Verify the musl source pin and patch application without building musl."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

from common import (
    MUSL_ROOT,
    apply_series,
    check_archive,
    check_patch_artifacts,
    extract_archive,
    load_manifests,
    verify_signature,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="archive to verify; defaults to output cache")
    parser.add_argument("--output", type=Path, help="bootstrap output directory (default: build/musl-bootstrap)")
    parser.add_argument("--signature", type=Path, help="optional detached signature to verify")
    parser.add_argument("--key", type=Path, help="public key file; required together with --signature")
    args = parser.parse_args()
    if bool(args.signature) != bool(args.key):
        parser.error("--signature and --key must be supplied together")

    source, patches = load_manifests()
    output = (args.output or Path(os.environ.get("OPENSILLICON_MUSL_OUT", MUSL_ROOT.parents[1] / "build" / "musl-bootstrap"))).resolve()
    archive = args.archive.resolve() if args.archive else output / "downloads" / source["archive_name"]
    check_archive(source, archive)
    print(f"PASS archive SHA-256: {source['archive_sha256']}")
    print(f"PASS archive version marker: {source['version']}")
    print(f"PASS archive COPYRIGHT SHA-256: {source['copyright_sha256']}")

    check_patch_artifacts(patches)
    for item in patches["confirmed_upstream"]:
        print(f"PASS confirmed patch {item['file']} ({item['sha256']})")
    for item in patches["candidates"]:
        print(f"PASS separate candidate {item['file']} ({item['sha256']}); not applied")

    if args.signature:
        result = verify_signature(source, args.signature.resolve(), args.key.resolve(), archive)
        print(f"PASS detached signature: {result}")
    else:
        print("SKIPPED detached signature check (provide --signature and --key)")

    with tempfile.TemporaryDirectory(prefix="opensillicon-musl-verify-") as temp_name:
        temp = Path(temp_name)
        pristine = temp / "upstream"
        extract_archive(archive, source, pristine)
        patched = temp / "patched"
        apply_series(pristine, patched, patches["confirmed_upstream"])
        print("PASS confirmed patch series applies cleanly to a clean pinned extraction")
        print("PASS changed paths: " + ", ".join(sorted({p for item in patches["confirmed_upstream"] for p in item["expected_files"]})))
    print("Verification complete; no musl configure, compile, or install was run")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"musl verification failed: {exc}", file=sys.stderr)
        sys.exit(1)
