#!/usr/bin/env python3
"""Build the pinned musl baseline for the bounded OpenSillicon x86_64 target."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from common import MUSL_ROOT, load_manifests


REPO = MUSL_ROOT.parents[1]
BOOTSTRAP = REPO / "build" / "musl-bootstrap"
OUTPUT_ID = "OpenSillicon musl native build output v1"
TARGET = "x86_64-unknown-none-elf"
FLAGS = [
    "-O2", "-pipe", "-ffreestanding", "-fno-builtin",
    "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
]
MALLOCNG_INTEGER_FLAGS = [
    "-mno-mmx", "-mno-sse", "-mno-sse2", "-msoft-float",
]
MALLOCNG_OBJECTS = [
    "aligned_alloc.o", "donate.o", "free.o", "malloc.o",
    "malloc_usable_size.o", "realloc.o",
]


def run(argv: list[str], *, cwd: Path, env: dict[str, str] | None = None,
        log: Path | None = None) -> None:
    print("+ " + " ".join(argv))
    if log is None:
        subprocess.run(argv, cwd=cwd, env=env, check=True)
        return
    with log.open("w", encoding="utf-8") as stream:
        subprocess.run(argv, cwd=cwd, env=env, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def make_mallocng_fpu_independent(build_dir: Path, build_log: Path) -> None:
    """Build integer-only mallocng objects without using unavailable SSE state."""
    object_names = set(MALLOCNG_OBJECTS)
    compile_commands: dict[str, list[str]] = {}
    archive_objects: list[str] | None = None

    for line in build_log.read_text(encoding="utf-8").splitlines():
        if "obj/src/malloc/mallocng/" in line and " -c " in line:
            try:
                command = shlex.split(line)
            except ValueError:
                continue
            output_index = command.index("-o") + 1 if "-o" in command else -1
            if output_index > 0:
                output = command[output_index]
                name = Path(output).name
                if name in object_names and "src/malloc/mallocng/" in output:
                    compile_commands[name] = command
        if line.startswith("llvm-ar rc lib/libc.a "):
            command = shlex.split(line)
            archive_objects = command[3:]

    if set(compile_commands) != object_names:
        missing = sorted(object_names - set(compile_commands))
        raise ValueError(f"missing mallocng compiler commands: {missing}")
    if not archive_objects:
        raise ValueError("could not find libc archive object order in build log")

    for name in sorted(object_names):
        command = compile_commands[name] + MALLOCNG_INTEGER_FLAGS
        run(command, cwd=build_dir)

    # The usual archive has duplicate basenames (for example stdlib/free.o
    # and mallocng/free.o). Rebuild using musl's recorded object order so both
    # members remain present and the selected mallocng members are replaced.
    archive = build_dir / "lib" / "libc.a"
    archive.unlink()
    run(["llvm-ar", "rc", "lib/libc.a", *archive_objects], cwd=build_dir)
    run(["llvm-ranlib", "lib/libc.a"], cwd=build_dir)

    disassembly = subprocess.check_output(
        ["llvm-objdump", "-d", *[f"obj/src/malloc/mallocng/{name}" for name in sorted(object_names)]],
        cwd=build_dir, text=True)
    if re.search(r"\b(?:xmm|ymm|zmm|mm)[0-9]+\b", disassembly):
        raise ValueError("mallocng objects still contain SIMD register instructions")


def prepare_output(output: Path) -> None:
    marker = output / ".opensillicon-musl-output"
    if output.is_symlink():
        raise ValueError(f"refusing symlink output directory: {output}")
    if output.exists():
        if marker.is_symlink() or not marker.is_file() or marker.read_text().strip() != OUTPUT_ID:
            raise ValueError(
                f"output exists without this script's ownership marker: {output}; "
                "choose another --output"
            )
        shutil.rmtree(output)
    output.mkdir(parents=True)
    (output / ".opensillicon-musl-output").write_text(OUTPUT_ID + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=REPO / "build" / "musl-port")
    parser.add_argument("--bootstrap", type=Path, default=BOOTSTRAP)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        raise ValueError("--jobs must be positive")
    output_arg = args.output.expanduser()
    if not output_arg.is_absolute():
        output_arg = REPO / output_arg
    # Do not resolve symlinks before prepare_output checks them. Resolving here
    # would let an output symlink bypass that guard and target unrelated files.
    output = Path(os.path.abspath(output_arg))
    bootstrap = args.bootstrap.resolve()
    source_manifest, patch_manifest = load_manifests()
    source = bootstrap / "source" / "patched" / source_manifest["top_level"]
    archive = bootstrap / "downloads" / source_manifest["archive_name"]
    if not source.is_dir() or not archive.is_file():
        raise ValueError("run the pinned musl bootstrap before native build")
    run([sys.executable, str(MUSL_ROOT / "scripts" / "verify.py"),
         "--output", str(bootstrap)], cwd=REPO)
    run([sys.executable, str(MUSL_ROOT / "scripts" / "bootstrap.py"),
         "--archive", str(archive), "--output", str(bootstrap)], cwd=REPO)

    prepare_output(output)
    source_copy = output / "source" / source_manifest["top_level"]
    build_dir = output / "build"
    sysroot = output / "sysroot"
    (source_copy.parent).mkdir(parents=True)
    build_dir.mkdir()
    sysroot.mkdir()
    shutil.copytree(source, source_copy)
    overlays = MUSL_ROOT / "opensillicon"
    shutil.copy2(overlays / "syscall_arch.h",
                 source_copy / "arch" / "x86_64" / "syscall_arch.h")
    shutil.copy2(overlays / "syscall_cp.s",
                 source_copy / "src" / "thread" / "x86_64" / "syscall_cp.s")
    shutil.copy2(overlays / "set_thread_area.s",
                 source_copy / "src" / "thread" / "x86_64" / "__set_thread_area.s")

    env = os.environ.copy()
    env.update({"LC_ALL": "C", "AR": "llvm-ar", "RANLIB": "llvm-ranlib",
                "CC": f"clang --target={TARGET}",
                "LDFLAGS": "-fuse-ld=lld -nostdlib -static"})
    configure = [
        str(source_copy / "configure"), f"--srcdir={source_copy}",
        f"--target={TARGET}", "--build=x86_64-redhat-linux-gnu",
        "--prefix=/usr", "--syslibdir=/lib", "--disable-shared",
        "--disable-wrapper", "LIBCC= ", "CFLAGS=" + " ".join(FLAGS),
    ]
    run(configure, cwd=build_dir, env=env, log=output / "configure.log")
    run(["make", f"-j{args.jobs}", "V=1"], cwd=build_dir, env=env,
        log=output / "build.log")
    make_mallocng_fpu_independent(build_dir, output / "build.log")
    run(["make", f"DESTDIR={sysroot}", "install"], cwd=build_dir, env=env,
        log=output / "install.log")

    adapter_object = output / "native_syscall.o"
    run([
        "clang", f"--target={TARGET}", "-std=c11", "-O2", "-Wall", "-Wextra",
        "-Werror", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-mno-red-zone", "-nostdinc",
        "-isystem", str(sysroot / "usr" / "include"), "-c",
        str(overlays / "native_syscall.c"), "-o", str(adapter_object),
    ], cwd=REPO, env=env)
    run(["llvm-ar", "r", str(sysroot / "usr" / "lib" / "libc.a"),
         str(adapter_object)], cwd=REPO, env=env)
    run(["llvm-ranlib", str(sysroot / "usr" / "lib" / "libc.a")],
        cwd=REPO, env=env)
    run(["llvm-nm", "-u", str(adapter_object)], cwd=REPO, env=env,
        log=output / "adapter-undefined.log")
    if (output / "adapter-undefined.log").read_text().strip():
        raise ValueError("native syscall adapter has unresolved symbols")

    license_dir = sysroot / "usr" / "share" / "licenses" / "musl"
    license_dir.mkdir(parents=True)
    shutil.copy2(source_copy / "COPYRIGHT", license_dir / "COPYRIGHT")
    compiler_version = subprocess.check_output(
        ["clang", "--version"], text=True).splitlines()[0]
    patch_records = patch_manifest["confirmed_upstream"]
    info = {
        "project": "OpenSillicon native musl sysroot",
        "musl_version": source_manifest["version"],
        "archive_sha256": source_manifest["archive_sha256"],
        "copyright_sha256": source_manifest["copyright_sha256"],
        "confirmed_patches": [
            {"file": item["file"], "sha256": item["sha256"]}
            for item in patch_records
        ],
        "local_adapter_files": {
            path.name: sha256(path)
            for path in sorted(overlays.iterdir()) if path.is_file()
        },
        "target": TARGET,
        "compiler": compiler_version,
        "linker": "LLD 22.1.8 via clang -fuse-ld=lld",
        "ar": "LLVM ar",
        "ranlib": "LLVM ranlib",
        "cflags": FLAGS,
        "mallocng_objects": MALLOCNG_OBJECTS,
        "mallocng_cflags_extra": MALLOCNG_INTEGER_FLAGS,
        "configure": [
            "--srcdir=source/musl-1.2.6",
            f"--target={TARGET}", "--build=x86_64-redhat-linux-gnu",
            "--prefix=/usr", "--syslibdir=/lib", "--disable-shared",
            "--disable-wrapper", "LIBCC= ", "CFLAGS=" + " ".join(FLAGS),
        ],
        "runtime_libraries": "none; LIBCC intentionally empty; no host libc or compiler runtime",
        "profile": "static single-thread x86_64; unsupported syscalls return ENOSYS",
    }
    (output / "build-info.json").write_text(
        json.dumps(info, indent=2, sort_keys=True) + "\n")
    print(f"native musl sysroot installed at {sysroot}")
    print("musl source was built; no system installation was modified")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"native musl build failed: {exc}", file=sys.stderr)
        sys.exit(1)
