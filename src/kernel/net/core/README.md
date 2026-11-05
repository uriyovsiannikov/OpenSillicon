# Network core

`netbuf.c` and `netdev.c` provide packet-buffer and network-device support used
by the kernel's network service and CycloneTCP adapter. Protocol processing is
provided by CycloneTCP; the older `netstack.c` IPv4 parser is retained as
unlinked experimental code and is not the active protocol stack.
