# TCP sockets

`tcp_socket.c` adapts the kernel's file descriptor and syscall interfaces to
CycloneTCP's TCP sockets. CycloneTCP handles TCP packet processing, connection
state, retransmission, and timers; its port and build integration are documented
in `../cyclone/README.md`.

The older `tcp_core.c`, `tcp_runtime.c`, and `tcp_wire.c` sources are remnants of
the experimental in-tree TCP implementation. They are not part of the kernel
build and must not be used by the CycloneTCP socket adapter.
