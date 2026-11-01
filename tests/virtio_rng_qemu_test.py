#!/usr/bin/env python3
import subprocess
import sys
import time


def main():
	if len(sys.argv) not in (7, 8) or sys.argv[3] not in ("present", "absent"):
		print("usage: virtio_rng_qemu_test.py QEMU OVMF present|absent ISO DISK LOG [CPU_PROFILE]",
		    file=sys.stderr)
		return 2
	qemu, ovmf, profile, iso, disk, log_path = sys.argv[1:7]
	cpu_profiles = {
		"max-rdrand-rdseed": ("max,+rdrand,+rdseed", True),
		"max-rdrand": ("max,+rdrand,-rdseed", True),
		"max-neither": ("max,-rdrand,-rdseed", False)
	}
	cpu_profile = sys.argv[7] if len(sys.argv) == 8 else None
	if cpu_profile is not None and cpu_profile not in cpu_profiles:
		print("unsupported CPU profile: " + cpu_profile, file=sys.stderr)
		return 2
	command = [qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
	    "-cdrom", iso, "-drive", "file=" + disk +
	    ",if=none,id=rootdisk,format=raw", "-device",
	    "ide-hd,drive=rootdisk,bus=ide.0", "-serial", "file:" + log_path,
	    "-display", "none", "-no-reboot", "-no-shutdown", "-nic", "none"]
	if cpu_profile is not None:
		command += ["-cpu", cpu_profiles[cpu_profile][0]]
	if profile == "present":
		command += ["-object", "rng-random,id=hostrng,filename=/dev/urandom",
		    "-device", "virtio-rng-pci,disable-modern=on,rng=hostrng"]
	expected = ("VirtIO RNG completion bytes 256" if profile == "present" else
	    "supported transitional device absent; boot continues")
	process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
	    stderr=subprocess.PIPE, text=True)
	deadline = time.monotonic() + 35
	try:
		while time.monotonic() < deadline:
			try:
				with open(log_path, encoding="ascii", errors="replace") as log:
					contents = log.read()
			except FileNotFoundError:
				contents = ""
			if expected in contents:
				if cpu_profile is not None:
					rdrand_enabled = "Initializing RDRAND instruction" in contents and "[ OK ]" in contents.split("Initializing RDRAND instruction", 1)[1].split("\n", 1)[0]
					if rdrand_enabled != cpu_profiles[cpu_profile][1]:
						raise RuntimeError("guest RDRAND detection did not match CPU profile")
				print("VirtIO RNG QEMU " + profile + " guest marker observed")
				if cpu_profile is not None:
					print("QEMU CPU profile verified: " + cpu_profile)
				print("QEMU intentionally terminated by the test harness after marker")
				return 0
			if process.poll() is not None:
				break
			time.sleep(0.05)
		try:
			stderr = process.communicate(timeout=1)[1]
		except subprocess.TimeoutExpired:
			stderr = ""
		print("VirtIO RNG QEMU " + profile + " failed; log follows", file=sys.stderr)
		try:
			with open(log_path, encoding="ascii", errors="replace") as log:
				print(log.read(), file=sys.stderr)
		except FileNotFoundError:
			pass
		if stderr:
			print(stderr, file=sys.stderr)
		return 1
	finally:
		if process.poll() is None:
			process.terminate()
			try:
				process.wait(timeout=3)
			except subprocess.TimeoutExpired:
				process.kill()
				process.wait()


if __name__ == "__main__":
	sys.exit(main())
