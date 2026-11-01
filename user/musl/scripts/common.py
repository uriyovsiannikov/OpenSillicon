#!/usr/bin/env python3
"""Shared verification helpers for the pinned musl source bootstrap."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import subprocess
import tarfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
MUSL_ROOT = REPO_ROOT / "user" / "musl"


def load_manifests() -> tuple[dict, dict]:
    with (MUSL_ROOT / "manifest" / "source.json").open(encoding="utf-8") as f:
        source = json.load(f)
    with (MUSL_ROOT / "manifest" / "patches.json").open(encoding="utf-8") as f:
        patches = json.load(f)
    if source.get("schema_version") != 1 or patches.get("schema_version") != 1:
        raise ValueError("unsupported musl manifest schema")
    return source, patches


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_sha256(path: Path, expected: str, label: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"{label} is missing: {path}")
    actual = sha256_file(path)
    if actual != expected:
        raise ValueError(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")


def check_archive(source: dict, archive: Path) -> None:
    check_sha256(archive, source["archive_sha256"], "musl source archive")
    expected_prefix = source["top_level"] + "/"
    with tarfile.open(archive, "r:gz") as tf:
        members = tf.getmembers()
        for member in members:
            name = member.name
            parts = Path(name).parts
            is_root = name.rstrip("/") == source["top_level"] and member.isdir()
            if name.startswith("/") or ".." in parts or (not is_root and not name.startswith(expected_prefix)):
                raise ValueError(f"unexpected path in pinned archive: {name}")
        version_member = tf.extractfile(expected_prefix + source["version_file"])
        copyright_member = tf.extractfile(expected_prefix + source["copyright_file"])
        if version_member is None or copyright_member is None:
            raise ValueError("archive lacks VERSION or COPYRIGHT")
        version = version_member.read().decode("ascii").strip()
        if version != source["version"]:
            raise ValueError(f"archive version mismatch: expected {source['version']}, got {version}")
        copyright_hash = hashlib.sha256(copyright_member.read()).hexdigest()
        if copyright_hash != source["copyright_sha256"]:
            raise ValueError(
                "archive COPYRIGHT SHA-256 mismatch: "
                f"expected {source['copyright_sha256']}, got {copyright_hash}"
            )


def verify_source_dir(source: dict, source_dir: Path) -> None:
    version_path = source_dir / source["version_file"]
    copyright_path = source_dir / source["copyright_file"]
    if not version_path.is_file() or version_path.read_text(encoding="ascii").strip() != source["version"]:
        raise ValueError(f"source tree has wrong or missing version marker: {source_dir}")
    check_sha256(copyright_path, source["copyright_sha256"], "source COPYRIGHT")


def patch_paths(patch_path: Path) -> set[str]:
    result = subprocess.run(
        ["git", "apply", "--numstat", str(patch_path)],
        check=True,
        text=True,
        capture_output=True,
    )
    paths = set()
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            raise ValueError(f"cannot parse patch file list in {patch_path}: {line!r}")
        paths.add(fields[2])
    return paths


def check_patch_artifacts(patches: dict) -> None:
    if patches.get("application_order") != [item["file"] for item in patches["confirmed_upstream"]]:
        raise ValueError("confirmed patch application_order does not match the patch list")
    if patches.get("candidate_application_order") != [item["file"] for item in patches["candidates"]]:
        raise ValueError("candidate application order does not match the candidate list")
    for group_name in ("confirmed_upstream", "candidates"):
        for item in patches[group_name]:
            path = MUSL_ROOT / item["file"]
            check_sha256(path, item["sha256"], item["file"])
            if patch_paths(path) != set(item["expected_files"]):
                raise ValueError(f"unexpected changed-file list in patch {item['file']}")
    if any(item["classification"] != "upstream-accepted" and
           item["classification"] != "upstream-accepted-bundle"
           for item in patches["confirmed_upstream"]):
        raise ValueError("confirmed patch series contains a non-upstream classification")
    if any(item["classification"] != "local-candidate-not-confirmed-upstream"
           for item in patches["candidates"]):
        raise ValueError("candidate series classification changed unexpectedly")


def extract_archive(archive: Path, source: dict, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    subprocess.run(
        ["tar", "-xzf", str(archive), "--strip-components=1", "--no-same-owner", "-C", str(destination)],
        check=True,
    )
    verify_source_dir(source, destination)


def tree_files(root: Path) -> dict[str, tuple[str, str, str]]:
    result: dict[str, tuple[str, str, str]] = {}
    for current, dirs, files in os.walk(root, followlinks=False):
        dirs.sort()
        files.sort()
        current_path = Path(current)
        for name in dirs + files:
            path = current_path / name
            rel = path.relative_to(root).as_posix()
            mode = format(stat.S_IMODE(path.lstat().st_mode), "04o")
            if path.is_symlink():
                result[rel] = ("symlink", os.readlink(path), mode)
            elif path.is_file():
                result[rel] = ("file", sha256_file(path), mode)
            elif path.is_dir():
                result[rel] = ("dir", "", mode)
            else:
                result[rel] = ("other", "", mode)
    return result


def changed_paths(before: Path, after: Path) -> set[str]:
    a = tree_files(before)
    b = tree_files(after)
    return {path for path in a.keys() | b.keys() if a.get(path) != b.get(path)}


def apply_series(source_dir: Path, target_dir: Path, series: list[dict]) -> None:
    shutil.copytree(source_dir, target_dir, symlinks=True)
    for item in series:
        patch = MUSL_ROOT / item["file"]
        subprocess.run(
            ["patch", "--batch", "--forward", "--fuzz=0", "--no-backup-if-mismatch", "-p1", "-i", str(patch)],
            cwd=target_dir,
            check=True,
        )
    expected = {path for item in series for path in item["expected_files"]}
    actual = changed_paths(source_dir, target_dir)
    if actual != expected:
        raise ValueError(
            f"patch series changed unexpected paths; expected {sorted(expected)}, got {sorted(actual)}"
        )


def compare_trees(a: Path, b: Path) -> bool:
    return tree_files(a) == tree_files(b)


def verify_signature(source: dict, signature: Path, key: Path, archive: Path) -> str:
    check_sha256(signature, source["signature_sha256"], "detached signature")
    expected_fpr = source["signing_key_fingerprint"].replace(" ", "").upper()
    with __import__("tempfile").TemporaryDirectory(prefix="opensillicon-musl-gpg-") as home:
        subprocess.run(
            ["gpg", "--homedir", home, "--batch", "--import", str(key)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        result = subprocess.run(
            ["gpg", "--homedir", home, "--batch", "--status-fd=1", "--verify", str(signature), str(archive)],
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            raise ValueError("GPG detached signature verification failed: " + result.stderr.strip())
        valid = [line.split() for line in result.stdout.splitlines() if line.startswith("[GNUPG:] VALIDSIG ")]
        if not valid or valid[0][2].upper() != expected_fpr:
            raise ValueError("signature is valid under an unexpected signing-key fingerprint")
        return "cryptographically valid; signer fingerprint matches pin; GPG trust is not asserted"
