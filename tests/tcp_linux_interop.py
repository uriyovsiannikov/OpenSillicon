#!/usr/bin/env python3
"""Run Linux TCP interoperability wholly inside a disposable user/net namespace."""

import os
import json
import select
import signal
import subprocess
import sys
import time


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, text=True)


def run_case(helper, helper_mode, peer_mode, linux_mac):
    capture = subprocess.Popen(["tcpdump", "-l", "-Z", "root", "-n", "-e",
                               "-vv", "-i", "lin0", "tcp port 19090"],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
    endpoint = None
    peer = None
    try:
        time.sleep(0.2)
        if helper_mode == "passive":
            endpoint = subprocess.Popen([helper, "osi0", linux_mac, helper_mode],
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True)
            time.sleep(0.2)
        peer = subprocess.Popen([sys.executable, "tests/tcp_linux_peer.py",
                                 peer_mode], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, bufsize=1)
        ready, _, _ = select.select([peer.stdout], [], [], 5)
        if not ready:
            raise RuntimeError("Linux TCP peer did not become ready")
        ready_line = peer.stdout.readline().strip()
        print("Linux peer: " + ready_line)
        expected_ready = ("LINUX_LISTENER_READY" if peer_mode == "server"
                          else "LINUX_CLIENT_READY")
        if ready_line != expected_ready:
            raise RuntimeError("unexpected Linux peer startup output: " + ready_line)
        if helper_mode == "active":
            endpoint = subprocess.Popen([helper, "osi0", linux_mac, helper_mode],
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True)
        endpoint_output, _ = endpoint.communicate(timeout=55)
        print(endpoint_output, end="")
        if endpoint.returncode != 0:
            raise RuntimeError("OpenSillicon packet endpoint exit %d" % endpoint.returncode)
        peer_output, _ = peer.communicate(timeout=5)
        print(peer_output, end="")
        if peer.returncode != 0:
            raise RuntimeError("Linux peer exit %d" % peer.returncode)
    finally:
        if capture.poll() is None:
            time.sleep(0.25)  # let tcpdump drain the namespace capture queue
            capture.send_signal(signal.SIGINT)
        try:
            capture_output, _ = capture.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            capture.kill()
            capture_output, _ = capture.communicate()
        print("isolated tcpdump trace:\n" + capture_output, end="")
        for process in (endpoint, peer):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: tcp_linux_interop.py PATH_TO_C_HELPER")
    helper = os.path.abspath(sys.argv[1])
    if not os.path.isfile(helper):
        raise RuntimeError("interop helper is not built: " + helper)
    # The caller places this script inside a disposable user+network
    # namespace. All links, addresses, routes, and neighbors below are local
    # to that namespace and vanish when this process exits.
    run("ip", "link", "set", "lo", "up")
    run("ip", "link", "add", "osi0", "type", "veth", "peer", "name", "lin0")
    run("ip", "link", "set", "osi0", "up")
    run("ip", "addr", "add", "10.55.0.2/24", "dev", "lin0")
    run("ip", "link", "set", "lin0", "up")
    # Avoid seeing pre-offload TCP checksums on the veth packet tap. This only
    # changes the ephemeral test interface, never a host interface.
    run("ethtool", "-K", "lin0", "tx", "off", "rx", "off")
    osi_mac = json.loads(subprocess.run(
        ["ip", "-j", "link", "show", "dev", "osi0"], check=True,
        stdout=subprocess.PIPE, text=True).stdout)[0]["address"]
    linux_mac = json.loads(subprocess.run(
        ["ip", "-j", "link", "show", "dev", "lin0"], check=True,
        stdout=subprocess.PIPE, text=True).stdout)[0]["address"]
    run("ip", "neigh", "add", "10.55.0.1", "lladdr", osi_mac,
        "nud", "permanent", "dev", "lin0")
    print("namespace: private user/network namespace; veth osi0<->lin0")
    print("configuration: OpenSillicon 10.55.0.1, Linux 10.55.0.2; isolated peer")
    print("MACs: osi0=%s lin0=%s; static neighbor only inside namespace" %
          (osi_mac, linux_mac))
    run_case(helper, "active", "server", linux_mac)
    run_case(helper, "passive", "passive-client", linux_mac)
    print("independent Linux TCP active/passive interoperability tests passed")
    # Namespace teardown removes only this namespace and its veth pair.


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("TCP Linux interoperability unavailable/failed: %s" % error,
              file=sys.stderr)
        raise SystemExit(1)
