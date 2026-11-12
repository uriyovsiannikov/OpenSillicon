Inter-process communication code lets processes exchange data through kernel-managed endpoints. The available mechanisms include buffered pipes and local Unix-domain sockets.

Unix pathname sockets are backed by socket inodes in RAMFS or SilliconFS.
Binding requires write and search access to the parent directory and creates
the inode with mode `0777 & ~umask`. Connecting checks directory search access
and write access to the socket inode. `chmod`, `chown`, `stat`, directory
listing and sticky-directory `unlink` use the normal VFS rules. Closing a
socket leaves its pathname in place until `unlink`; unlinking a live socket
prevents new connections through that inode without breaking existing ones.
Abstract addresses (a leading NUL byte) remain a separate byte-string namespace
without filesystem permissions; services can authenticate peers with
`SO_PEERCRED`.

Run `sh src/tests/unix_peercred.sh` for host-side RAMFS and credential tests,
or `sh src/tests/permissions_guest.sh` for syscall tests in QEMU followed by
validation of persisted SilliconFS socket inodes. The QEMU test uses a separate
disk under `src/build/permissions-guest/`.
