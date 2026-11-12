#include "include/os.h"
#include "abi/syscall.h"
#include "kernel/unix_socket.h"

static long
call(uint64_t number, uint64_t first, uint64_t second, uint64_t third)
{
	unsigned char failed;
	__asm__ volatile ("int $128; setc %1" : "+a" (number), "=qm" (failed) :
	    "D" (first), "S" (second), "d" (third) : "rcx", "r11", "memory");
	return failed ? -(long)number : (long)number;
}

static void
check(int success, unsigned int line)
{
	if (success) return;
	u_puts("PERMISSIONS GUEST FAIL at line ");
	u_put_u64(line);
	u_puts("\n");
	/* The supervising root process can stop QEMU after a failed child. */
	(void)u_power_control(POWER_CONTROL_POWEROFF);
	u_exit(1);
}
#define CHECK(condition) check((condition), __LINE__)

static uint64_t reowned_fd;
static uint8_t exec_image[256 * 1024];

static void
exec_saved_id_checks(void)
{
	struct u_stat status;
	CHECK(u_stat_path("/bin/sh", &status) == 0);
	CHECK(status.size != 0 && status.size <= sizeof(exec_image));
	long fd = u_open("/bin/sh", U_O_RDONLY);
	CHECK(fd >= 0);
	uint64_t offset = 0;
	while (offset < status.size) {
		size_t length = (size_t)(status.size - offset);
		if (length > 512) length = 512;
		long amount = u_read((uint64_t)fd, exec_image + offset, length);
		CHECK(amount > 0);
		offset += (uint64_t)amount;
	}
	CHECK(u_close((uint64_t)fd) == 0);
	fd = u_open_mode("/tmp/invalid-elf", U_O_CREAT | U_O_EXCL | U_O_WRONLY, 0755);
	CHECK(fd >= 0);
	CHECK(u_write((uint64_t)fd, "bad", 3) == 3);
	CHECK(u_close((uint64_t)fd) == 0);
	for (unsigned int memory = 0; memory < 2; memory++) {
		long child = u_fork();
		CHECK(child >= 0);
		if (child == 0) {
			struct os_credentials c;
			CHECK(call(SYSCALL_SETRESGID, 100, 200, 0) == 0);
			CHECK(call(SYSCALL_SETRESUID, 100, 200, 0) == 0);
			CHECK(u_exec_path("/missing-executable") == -2);
			CHECK(u_exec_path("/tmp/invalid-elf") == -8);
			CHECK(call(SYSCALL_EXEC, (uintptr_t)"bad", 3, 0) == -22);
			CHECK(u_getcredentials(&c) == 0);
			CHECK(c.saved_uid == 0 && c.saved_gid == 0);
			if (memory)
				(void)call(SYSCALL_EXEC, (uintptr_t)exec_image, status.size, 0);
			else
				(void)u_exec_path("/bin/sh");
			CHECK(0);
		}
		uint8_t child_status = 255;
		CHECK(u_waitpid((uint64_t)child, &child_status) == child && child_status == 0);
	}
}

static void
reserved_id_checks(void)
{
	const uint64_t operations[] = {
		SYSCALL_SETUID, SYSCALL_SETEUID, SYSCALL_SETGID, SYSCALL_SETEGID
	};
	for (unsigned int i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
		long child = u_fork();
		CHECK(child >= 0);
		if (child == 0) {
			struct os_credentials c;
			CHECK(call(operations[i], UINT32_MAX, 0, 0) == -22);
			CHECK(u_getcredentials(&c) == 0);
			CHECK(c.real_uid == 0 && c.effective_uid == 0 && c.saved_uid == 0);
			CHECK(c.real_gid == 0 && c.effective_gid == 0 && c.saved_gid == 0);
			u_exit(0);
		}
		uint8_t status = 255;
		CHECK(u_waitpid((uint64_t)child, &status) == child && status == 0);
	}
	CHECK(call(SYSCALL_SETRESUID, UINT32_MAX, UINT32_MAX, UINT32_MAX) == 0);
	CHECK(call(SYSCALL_SETRESGID, UINT32_MAX, UINT32_MAX, UINT32_MAX) == 0);
}

static void
inet_bind_checks(int privileged)
{
	const uint16_t ports[] = {1, 1023, 1024, 0};
	for (unsigned int i = 0; i < sizeof(ports) / sizeof(ports[0]); i++) {
		struct u_tcp_endpoint tcp = {.port = ports[i]};
		struct u_udp_endpoint udp = {.port = ports[i]};
		long expected = !privileged && ports[i] != 0 && ports[i] < 1024 ?
		    -13 : 0;
		long fd = u_tcp_socket(2, 1, 0);
		CHECK(fd >= 0);
		CHECK(u_tcp_bind((uint64_t)fd, &tcp) == expected);
		CHECK(u_close((uint64_t)fd) == 0);
		fd = u_udp_socket(2, 2, 0);
		CHECK(fd >= 0);
		CHECK(u_udp_bind((uint64_t)fd, &udp) == expected);
		CHECK(u_close((uint64_t)fd) == 0);
	}
}

static void
pathname_socket_checks(void)
{
	struct u_stat status;
	struct unix_socket_address denied = {
		.family = 1, .path = "/closed/service"
	};
	long fd = call(SYSCALL_UNIX_SOCKET, 1, 1, 0);
	CHECK(fd >= 0);
	CHECK(call(SYSCALL_UNIX_BIND, (uint64_t)fd, (uintptr_t)&denied,
	    2 + sizeof("/closed/service")) == -13);
	struct unix_socket_address protected = {
		.family = 1, .path = "/tmp/protected-socket"
	};
	CHECK(call(SYSCALL_UNIX_CONNECT, (uint64_t)fd, (uintptr_t)&protected,
	    2 + sizeof("/tmp/protected-socket")) == -13);
	CHECK(u_close((uint64_t)fd) == 0);
	CHECK(call(SYSCALL_UMASK, 0077, 0, 0) >= 0);
	struct unix_socket_address own = {.family = 1, .path = "/tmp/own-socket"};
	fd = call(SYSCALL_UNIX_SOCKET, 1, 1, 0);
	CHECK(fd >= 0);
	CHECK(call(SYSCALL_UNIX_BIND, (uint64_t)fd, (uintptr_t)&own,
	    2 + sizeof("/tmp/own-socket")) == 0);
	CHECK(u_stat_path("/tmp/own-socket", &status) == 0);
	CHECK(status.type == 4 && status.mode == 0700 && status.uid == 300);
	CHECK(u_open("/tmp/own-socket", U_O_RDONLY) == -6);
	CHECK(call(SYSCALL_UNIX_LISTEN, (uint64_t)fd, 1, 0) == 0);
	long client = call(SYSCALL_UNIX_SOCKET, 1, 1, 0);
	CHECK(client >= 0);
	struct unix_socket_address alias = {
		.family = 1, .path = "/tmp/./own-socket"
	};
	CHECK(call(SYSCALL_UNIX_CONNECT, (uint64_t)client, (uintptr_t)&alias,
	    2 + sizeof("/tmp/./own-socket")) == 0);
	long accepted = call(SYSCALL_UNIX_ACCEPT, (uint64_t)fd, 0, 0);
	CHECK(accepted >= 0);
	CHECK(u_close((uint64_t)accepted) == 0);
	CHECK(u_close((uint64_t)client) == 0);
	CHECK(u_unlink("/tmp/own-socket", 0) == 0);
	client = call(SYSCALL_UNIX_SOCKET, 1, 1, 0);
	CHECK(client >= 0);
	CHECK(call(SYSCALL_UNIX_CONNECT, (uint64_t)client, (uintptr_t)&own,
	    2 + sizeof("/tmp/own-socket")) == -2);
	long replacement = call(SYSCALL_UNIX_SOCKET, 1, 1, 0);
	CHECK(replacement >= 0);
	CHECK(call(SYSCALL_UNIX_BIND, (uint64_t)replacement, (uintptr_t)&own,
	    2 + sizeof("/tmp/own-socket")) == 0);
	CHECK(call(SYSCALL_UNIX_LISTEN, (uint64_t)replacement, 1, 0) == 0);
	CHECK(call(SYSCALL_UNIX_CONNECT, (uint64_t)client, (uintptr_t)&own,
	    2 + sizeof("/tmp/own-socket")) == 0);
	accepted = call(SYSCALL_UNIX_ACCEPT, (uint64_t)replacement, 0, 0);
	CHECK(accepted >= 0);
	CHECK(u_close((uint64_t)accepted) == 0);
	CHECK(u_close((uint64_t)client) == 0);
	CHECK(u_close((uint64_t)replacement) == 0);
	CHECK(u_close((uint64_t)fd) == 0);
	fd = call(SYSCALL_UNIX_SOCKET, 1, 1, 0);
	CHECK(fd >= 0);
	CHECK(call(SYSCALL_UNIX_BIND, (uint64_t)fd, (uintptr_t)&own,
	    2 + sizeof("/tmp/own-socket")) == -98);
	CHECK(u_close((uint64_t)fd) == 0);
	CHECK(u_unlink("/tmp/own-socket", 0) == 0);
	CHECK(call(SYSCALL_UMASK, 0022, 0, 0) >= 0);
}

static void
create_file(const char *path)
{
	long fd = u_open_mode(path, U_O_CREAT | U_O_EXCL | U_O_WRONLY, 0644);
	CHECK(fd >= 0);
	CHECK(u_close((uint64_t)fd) == 0);
}

static void
open_unlink_lifetime(void)
{
	char byte = 0;
	struct u_stat status;
	long writer = u_open_mode("/tmp/lifetime", U_O_CREAT | U_O_RDWR, 0600);
	CHECK(writer >= 0);
	long reader = u_open("/tmp/lifetime", U_O_RDONLY);
	CHECK(reader >= 0);
	CHECK(u_unlink("/tmp/lifetime", 0) == 0);
	create_file("/tmp/lifetime");
	CHECK(u_write((uint64_t)writer, "X", 1) == 1);
	CHECK(u_read((uint64_t)reader, &byte, 1) == 1 && byte == 'X');
	CHECK(u_stat_path("/tmp/lifetime", &status) == 0 && status.size == 0);
	CHECK(u_close((uint64_t)writer) == 0);
	CHECK(u_close((uint64_t)reader) == 0);
	create_file("/tmp/after-close");
	CHECK(u_stat_path("/tmp/after-close", &status) == 0 && status.size == 0);
}

static void
unprivileged_checks(void)
{
	struct os_credentials c;
	struct u_stat status;
	CHECK(call(SYSCALL_SETRESGID, 100, 200, 300) == 0);
	CHECK(call(SYSCALL_SETRESUID, 100, 200, 300) == 0);
	CHECK(call(SYSCALL_SETEUID, 200, 0, 0) == 0);
	CHECK(call(SYSCALL_SETEGID, 200, 0, 0) == 0);
	CHECK(u_open("/closed/../public", U_O_RDONLY) == -13);
	CHECK(u_chdir("/closed/..") == -13);
	/* access() must use real UID 100 even while the effective UID is 200. */
	CHECK(call(SYSCALL_ACCESS, (uintptr_t)"/closed/../public", 4, 0) == 0);
	CHECK(u_setuid(300) == 0);
	CHECK(u_setgid(300) == 0);
	CHECK(u_getcredentials(&c) == 0);
	CHECK(c.real_uid == 100 && c.effective_uid == 300 && c.saved_uid == 300);
	CHECK(c.real_gid == 100 && c.effective_gid == 300 && c.saved_gid == 300);
	/* A chown through another lookup revokes fchmod on an existing FD. */
	CHECK(call(SYSCALL_FCHMOD, reowned_fd, 0777, 0) == -13);
	CHECK(u_setuid(400) == -1);
	CHECK(u_setgid(400) == -1);
	inet_bind_checks(0);
	pathname_socket_checks();
	CHECK(u_unlink("/tmp/root-file", 0) == -1);
	CHECK(u_unlink("/tmp/root-dir", 1) == -1);
	CHECK(u_framebuffer_present((const void *)1, 16) == -1);
	struct os_framebuffer_rect rect = { .source = 1, .width = 1, .height = 1 };
	CHECK(u_framebuffer_present_rect(&rect) == -1);
	long fd = u_open_mode("/group/created", U_O_CREAT | U_O_EXCL | U_O_WRONLY,
	    02755);
	CHECK(fd >= 0);
	CHECK(u_close((uint64_t)fd) == 0);
	CHECK(u_stat_path("/group/created", &status) == 0);
	CHECK(status.gid == 500 && status.mode == 0755);
	CHECK(u_stat_path("/public/../public", &status) == -20);
	CHECK(u_stat_path("/missing/../public", &status) == -2);
	CHECK(u_chdir("/tmp") == 0);
	create_file("own-file");
	CHECK(u_unlink("./own-file", 0) == 0);
	u_exit(0);
}

uint64_t
user_main(uint64_t argc, char **argv, const uint64_t *startup_stack)
{
	(void)argc; (void)argv; (void)startup_stack;
	struct os_credentials initial;
	CHECK(u_getcredentials(&initial) == 0);
	if (initial.effective_uid == 200) {
		/* This is the replacement image in exec_saved_id_checks(). */
		CHECK(initial.real_uid == 100 && initial.saved_uid == 200);
		CHECK(initial.real_gid == 100 && initial.effective_gid == 200 &&
		    initial.saved_gid == 200);
		CHECK(call(SYSCALL_SETEUID, 0, 0, 0) == -1);
		CHECK(call(SYSCALL_SETEGID, 0, 0, 0) == -1);
		u_exit(0);
	}
	reserved_id_checks();
	exec_saved_id_checks();
	inet_bind_checks(1);
	open_unlink_lifetime();
	CHECK(u_mkdir("/closed", 0700) == 0);
	CHECK(call(SYSCALL_CHOWN, (uintptr_t)"/closed", 100, 100) == 0);
	CHECK(u_mkdir("/group", 0777) == 0);
	CHECK(call(SYSCALL_CHOWN, (uintptr_t)"/group", 100, 500) == 0);
	CHECK(call(SYSCALL_CHMOD, (uintptr_t)"/group", 02777, 0) == 0);
	create_file("/public");
	create_file("/tmp/root-file");
	struct unix_socket_address protected = {
		.family = 1, .path = "/tmp/protected-socket"
	};
	long socket = call(SYSCALL_UNIX_SOCKET, 1, 1, 0);
	CHECK(socket >= 0);
	CHECK(call(SYSCALL_UNIX_BIND, (uint64_t)socket, (uintptr_t)&protected,
	    2 + sizeof("/tmp/protected-socket")) == 0);
	CHECK(call(SYSCALL_CHMOD, (uintptr_t)"/tmp/protected-socket", 0600, 0) == 0);
	CHECK(call(SYSCALL_UNIX_LISTEN, (uint64_t)socket, 1, 0) == 0);
	create_file("/tmp/reowned");
	CHECK(call(SYSCALL_CHOWN, (uintptr_t)"/tmp/reowned", 300, 300) == 0);
	long descriptor = u_open("/tmp/reowned", U_O_RDONLY);
	CHECK(descriptor >= 0);
	reowned_fd = (uint64_t)descriptor;
	CHECK(call(SYSCALL_CHOWN, (uintptr_t)"/tmp/reowned", 0, 0) == 0);
	CHECK(u_mkdir("/tmp/root-dir", 0755) == 0);
	long child = u_fork();
	CHECK(child >= 0);
	if (child == 0) unprivileged_checks();
	uint8_t status = 255;
	CHECK(u_waitpid((uint64_t)child, &status) == child);
	CHECK(status == 0);
	CHECK(u_close(reowned_fd) == 0);
	CHECK(u_close((uint64_t)socket) == 0);
	CHECK(u_unlink("/tmp/root-file", 0) == 0);
	CHECK(u_unlink("/tmp/root-dir", 1) == 0);
	u_puts("PERMISSIONS GUEST PASS\n");
	(void)u_power_control(POWER_CONTROL_POWEROFF);
	return 0;
}
