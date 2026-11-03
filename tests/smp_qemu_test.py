#!/usr/bin/env python3
"""Boot four virtual CPUs and require Limine AP online acknowledgements."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import re

parser = argparse.ArgumentParser()
parser.add_argument("--qemu", required=True)
parser.add_argument("--bios", required=True)
parser.add_argument("--build-dir", required=True)
parser.add_argument("--cpus", type=int, default=4)
parser.add_argument("--expected-online", type=int, default=4)
parser.add_argument("--failed-cpu", type=int)
parser.add_argument("--failed-stage", type=int, default=1)
parser.add_argument("--tlb-rounds", type=int)
parser.add_argument("--expect-unavailable", action="store_true")
a = parser.parse_args()
if a.cpus < 1 or (not a.expect_unavailable and
                  (a.expected_online < 1 or a.expected_online > a.cpus)):
    raise SystemExit("invalid CPU count expectations")
if a.failed_cpu is not None and not (0 < a.failed_cpu < a.cpus):
    raise SystemExit("failed CPU must identify an AP")
if a.expect_unavailable and a.failed_cpu is not None:
    raise SystemExit("unavailable-topology test cannot inject an AP failure")
build = pathlib.Path(a.build_dir)
with tempfile.TemporaryDirectory(prefix="opensillicon-smp-") as temp_dir:
    temp = pathlib.Path(temp_dir)
    serial_log = temp / "serial.log"
    root_image = temp / "root.img"
    shutil.copyfile(build / "root.img", root_image)
    command = [a.qemu, "-machine", "q35", "-smp", str(a.cpus), "-m", "256M",
               "-bios", a.bios, "-cdrom", str(build / "opensillicon.iso"),
               "-drive", f"file={root_image},if=none,id=rootdisk,format=raw",
               "-device", "ide-hd,drive=rootdisk,bus=ide.0", "-serial",
               f"file:{serial_log}", "-display", "none", "-no-reboot", "-no-shutdown"]
    try:
        result = subprocess.run(command, timeout=40, check=False,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE, text=True)
        exit_status = result.returncode
        qemu_stderr = result.stderr
    except subprocess.TimeoutExpired:
        exit_status = 124
        qemu_stderr = ""
    contents = serial_log.read_text(errors="replace")
    if a.expect_unavailable:
        marker = (f"OpenSillicon: SMP topology rejected; discovered {a.cpus} "
                  f"exceeds maximum 8; continuing on BSP")
    else:
        marker_kind = "verified" if a.expected_online == a.cpus else "partial"
        marker = (f"OpenSillicon: SMP bootstrap {marker_kind} discovered "
                  f"{a.cpus} online {a.expected_online}")
    if marker not in contents:
        print(contents[-5000:])
        if qemu_stderr:
            print(qemu_stderr[-5000:])
        raise SystemExit(f"SMP marker missing; QEMU exit status {exit_status}")
    if not a.expect_unavailable and \
            "OpenSillicon: SMP TLB shootdown acknowledged by all online APs" \
            not in contents:
        print(contents[-5000:])
        raise SystemExit("AP TLB shootdown acknowledgements were not verified")
    if not a.expect_unavailable and a.cpus > 1 and \
            "OpenSillicon: AP idle timer ticks verified" not in contents:
        print(contents[-5000:])
        raise SystemExit("AP local idle timer interrupts were not verified")
    if a.tlb_rounds is not None:
        marker = ("OpenSillicon: repeated SMP TLB shootdowns acknowledged "
                  f"rounds {a.tlb_rounds}")
        if marker not in contents:
            print(contents[-5000:])
            raise SystemExit("repeated AP TLB shootdown acknowledgements were not verified")
    if "OpenSillicon: launching /sbin/init from SilliconFS" not in contents:
        print(contents[-5000:])
        raise SystemExit("guest did not continue through VMM and init setup")
    if "OpenSillicon shell ready" not in contents:
        print(contents[-5000:])
        raise SystemExit("user process did not reach the shell-ready state")
    if "OpenSillicon: Round Robin scheduler passed" not in contents:
        print(contents[-5000:])
        raise SystemExit("BSP cooperative scheduler test did not complete")
    if "OpenSillicon: active address-space destruction blocked" not in contents:
        print(contents[-5000:])
        raise SystemExit("VMM allowed destruction of the current address space")
    if "OpenSillicon panic:" in contents:
        print(contents[-5000:])
        raise SystemExit("guest reported a kernel panic after the SMP marker")
    if a.expect_unavailable:
        if "SMP bootstrap " in contents or "SMP cpu logical " in contents:
            raise SystemExit("kernel reported partially initialized SMP state")
        print(marker)
        print("BSP continued through init launch without AP startup")
        if exit_status == 124:
            print("QEMU harness timeout after success marker (expected)")
        elif exit_status == 0:
            print("QEMU exited normally after the success marker")
        else:
            raise SystemExit(f"QEMU exited unexpectedly with status {exit_status}")
        raise SystemExit(0)
    records = re.findall(
        r"SMP cpu logical (\d+) processor (\d+) bsp (\d+) apic (\d+) state (\d+) stage (\d+) per_cpu (\d+) gdt (\d+) tss (\d+) active_space (\d+) stack_top (\d+) idle_timer_ticks (\d+)",
        contents)
    if len(records) != a.cpus:
        raise SystemExit(f"expected {a.cpus} CPU records, found {len(records)}")
    if sorted(int(record[0]) for record in records) != list(range(a.cpus)):
        raise SystemExit(f"logical CPU indices are not contiguous: {records}")
    if sum(int(record[2]) for record in records) != 1 or records[0][2] != "1":
        raise SystemExit(f"expected logical CPU 0 to be the only BSP: {records}")
    for column, label in ((1, "ACPI processor IDs"), (3, "APIC IDs"),
                          (6, "per-CPU addresses"), (7, "GDT addresses"),
                          (8, "TSS addresses"), (10, "stack tops")):
        values = [record[column] for record in records]
        if len(set(values)) != a.cpus:
            raise SystemExit(f"CPU {label} are not unique: {values}")
    active_spaces = {record[9] for record in records if int(record[4]) == 3}
    if len(active_spaces) != 1 or "0" in active_spaces:
        raise SystemExit(f"online CPUs did not enter idle on the shared kernel address space: {active_spaces}")
    states = {int(record[0]): int(record[4]) for record in records}
    stages = {int(record[0]): int(record[5]) for record in records}
    expected_states = {
        cpu: (4 if cpu == a.failed_cpu else 3)
        for cpu in range(a.cpus)
    }
    expected_online = sum(state == 3 for state in expected_states.values())
    if expected_online != a.expected_online or states != expected_states:
        raise SystemExit(f"unexpected online/failed states: {states}")
    expected_stages = {
        cpu: (a.failed_stage if cpu == a.failed_cpu else 3)
        for cpu in range(a.cpus)
    }
    if stages != expected_stages:
        raise SystemExit(f"unexpected CPU initialization stages: {stages}")
    idle_ticks = {int(record[0]): int(record[11]) for record in records}
    if any(idle_ticks[cpu] == 0 for cpu, state in states.items()
           if cpu != 0 and state == 3):
        raise SystemExit(f"online AP did not receive its local idle timer: {idle_ticks}")
    print(marker)
    if exit_status == 124:
        print("QEMU harness timeout after success marker (expected)")
    elif exit_status == 0:
        print("QEMU exited normally after the success marker")
    else:
        raise SystemExit(f"QEMU exited unexpectedly with status {exit_status}")
