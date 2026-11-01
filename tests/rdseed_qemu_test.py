#!/usr/bin/env python3
import subprocess
import sys
import time


PROFILES = {
	"rdseed": ("max,+rdseed,+rdrand", True, True),
	"rdrand-only": ("max,-rdseed,+rdrand", False, True),
	"rdseed-no-rdrand": ("max,+rdseed,-rdrand", True, False),
	"neither": ("max,-rdseed,-rdrand", False, False),
}


def main():
	if len(sys.argv) != 7 or sys.argv[5] not in PROFILES:
		print("usage: rdseed_qemu_test.py QEMU OVMF ISO DISK PROFILE LOG",
		    file=sys.stderr)
		return 2
	qemu, ovmf, iso, disk, profile, log_path = sys.argv[1:]
	cpu, expect_rdseed, expect_rdrand = PROFILES[profile]
	command = [qemu, "-machine", "q35", "-m", "256M", "-bios", ovmf,
	    "-cpu", cpu, "-cdrom", iso, "-drive", "file=" + disk +
	    ",if=none,id=rootdisk,format=raw", "-device",
	    "ide-hd,drive=rootdisk,bus=ide.0", "-serial", "file:" + log_path,
	    "-display", "none", "-no-reboot", "-no-shutdown", "-nic", "none"]
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
			if "RDSEED instruction" in contents:
				rdseed_line = next(line for line in contents.splitlines()
				    if "RDSEED instruction" in line)
				rdrand_line = next((line for line in contents.splitlines()
				    if "RDRAND instruction" in line), "")
				if ("[ OK ]" in rdseed_line) != expect_rdseed:
					raise RuntimeError("guest RDSEED status mismatched " + profile)
				if ("[ OK ]" in rdrand_line) != expect_rdrand:
					raise RuntimeError("guest RDRAND status mismatched " + profile)
				if expect_rdseed:
					if not any(marker in contents for marker in (
					    "RDSEED QEMU request completed bytes 8; data cleared",
					    "RDSEED QEMU request temporarily unavailable; boot continues")):
						time.sleep(0.05)
						continue
				else:
					if "RDSEED QEMU feature unsupported; boot continues" not in contents:
						time.sleep(0.05)
						continue
				print("RDSEED QEMU profile verified: " + profile)
				print("Guest feature markers: RDSEED=" + str(expect_rdseed) +
				    " RDRAND=" + str(expect_rdrand))
				if expect_rdseed:
					print("Guest bounded request marker observed; seed bytes not logged")
				print("QEMU intentionally terminated by harness after marker")
				return 0
			if process.poll() is not None:
				break
			time.sleep(0.05)
		try:
			stderr = process.communicate(timeout=1)[1]
		except subprocess.TimeoutExpired:
			stderr = ""
		print("RDSEED QEMU test failed; serial log follows", file=sys.stderr)
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
