# CycloneTCP port

CycloneTCP and CycloneCommon are vendored together under
`src/kernel/net/cyclonetcp/` (`common/` contains CycloneCommon and
`opensillicon/` contains this kernel port).
The imported revisions are `506a4a8` and `56ba9b9`, respectively. Their
upstream GPL-2.0-or-later notices are preserved.

The kernel build includes CycloneTCP's Ethernet, IPv4, ARP, ICMP, TCP, and UDP
core plus an OpenSillicon no-RTOS port. The `net_device` adapter transmits
through e1000, passes received frames to `nicProcessPacket`, and advances the
stack from the SMP-serialized deferred network service. Cyclone timer time
uses the kernel's monotonic clock in milliseconds. The mutex port uses atomic
inter-CPU serialization and per-CPU interrupt-state tracking to prevent task
preemption while a CPU holds a Cyclone lock. Stack work must remain outside interrupt
context.

When CycloneTCP initializes, the existing UDP socket and ICMP echo syscall
interfaces use CycloneTCP for packet processing, route selection, ARP, and
transmission. UDP callbacks copy datagrams into the existing kernel socket
queues, preserving the file descriptor and syscall ABI. Network attachment
now fails if CycloneTCP cannot initialize. The previous native IPv4/UDP
parser and experimental TCP runtime are no longer linked into the kernel.

IPv4 `SOCK_STREAM` sockets use Cyclone's TCP sockets through a bounded kernel
descriptor adapter. The current syscall/libos surface includes bind, listen,
connect, accept, shutdown, stream read/write, endpoint queries, polling, and
the implemented socket options. The network service polls the NIC and runs
Cyclone's deferred task processing outside interrupt context, including while
the scheduler waits for runnable work.

The port currently has one DHCP-configured IPv4 interface and one e1000
device. The address, subnet mask, and gateway come from Cyclone's DHCP client;
the DHCP resolver is exposed to libos and used by IPv4 DNS lookups. Network
operations requiring a route remain unavailable until configuration arrives.
IPv6, TCP urgent data, ancillary socket messages, and blocking socket calls
are not implemented. TCP stream I/O uses nonblocking Cyclone operations
internally; callers receive `EAGAIN` when
the operation cannot make progress and can use `poll`/`select` before retrying.
The Cyclone socket limit and kernel descriptor limit are both fixed.
