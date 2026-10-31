#!/usr/bin/env python3
"""Fetch and materialize the pinned musl source and confirmed patch series."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

sys.dont_write_bytecode = True

from common import (
    MUSL_ROOT,
    apply_series,
    check_archive,
    check_patch_artifacts,
    compare_trees,
    extract_archive,
    load_manifests,
    verify_source_dir,
)


def obtain_archive(source: dict, output: Path, supplied: Path | None) -> Path:
    cache = output / "downloads" / source["archive_name"]
    cache.parent.mkdir(parents=True, exist_ok=True)
    if cache.is_symlink():
        raise ValueError(f"refusing to use a symlink as the archive cache: {cache}")
    if cache.exists():
        check_archive(source, cache)
        return cache

    with tempfile.NamedTemporaryFile(prefix=".musl-download-", dir=cache.parent, delete=False) as tmp:
        temporary = Path(tmp.name)
    try:
        if supplied is not None:
            shutil.copyfile(supplied, temporary)
        else:
            request = urllib.request.Request(source["archive_url"], headers={"User-Agent": "OpenSillicon-musl-bootstrap/1"})
            with urllib.request.urlopen(request, timeout=60) as response, temporary.open("wb") as target:
                shutil.copyfileobj(response, target)
        check_archive(source, temporary)
        os.replace(temporary, cache)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    return cache


def install_if_absent(generated: Path, final: Path, label: str) -> None:
    final.parent.mkdir(parents=True, exist_ok=True)
    if final.is_symlink():
        raise ValueError(f"refusing to replace or follow a symlink for {label}: {final}")
    if final.exists():
        if not compare_trees(generated, final):
            raise ValueError(f"existing {label} differs from reproducible output; choose a new output directory: {final}")
        print(f"verified existing {label}: {final}")
        return
    os.rename(generated, final)
    print(f"created {label}: {final}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="use this local archive if the output cache is empty")
    parser.add_argument("--candidate", action="store_true", help="also create a separate experimental tree with the local regexec candidate")
    parser.add_argument("--output", type=Path, help="generated output directory (default: build/musl-bootstrap)")
    args = parser.parse_args()

    source, patches = load_manifests()
    check_patch_artifacts(patches)
    output = (args.output or Path(os.environ.get("OPENSILLICON_MUSL_OUT", MUSL_ROOT.parents[1] / "build" / "musl-bootstrap"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = obtain_archive(source, output, args.archive.resolve() if args.archive else None)
    print(f"verified archive SHA-256: {archive}")

    source_root = output / "source"
    final_upstream = source_root / "upstream" / source["top_level"]
    final_patched = source_root / "patched" / source["top_level"]
    final_candidate = source_root / "experimental-regexec" / source["top_level"]

    with tempfile.TemporaryDirectory(prefix=".musl-bootstrap-work-", dir=output) as work_name:
        work = Path(work_name)
        generated_upstream = work / "upstream"
        extract_archive(archive, source, generated_upstream)
        generated_patched = work / "patched"
        apply_series(generated_upstream, generated_patched, patches["confirmed_upstream"])
        generated_candidate: Path | None = None
        if args.candidate:
            if len(patches["candidates"]) != 1:
                raise ValueError("bootstrap supports exactly one separately reviewed candidate patch")
            generated_candidate = work / "candidate"
            apply_series(generated_patched, generated_candidate, patches["candidates"])

        install_if_absent(generated_upstream, final_upstream, "pristine source tree")
        install_if_absent(generated_patched, final_patched, "confirmed-patched source tree")
        if generated_candidate is not None:
            install_if_absent(generated_candidate, final_candidate, "experimental candidate source tree")

    verify_source_dir(source, final_upstream)
    print("bootstrap complete; musl was not configured, compiled, or installed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"musl bootstrap failed: {exc}", file=sys.stderr)
        sys.exit(1)
