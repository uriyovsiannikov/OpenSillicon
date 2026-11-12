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
signal_permission_checks(void)
{
	uint32_t ready[2], gate[2];
	char byte = 0;
	CHECK(u_pipe(ready) == 0 && u_pipe(gate) == 0);
	long target = u_fork();
	CHECK(target >= 0);
	if (target == 0) {
		CHECK(call(SYSCALL_SETRESUID, 100, 200, 300) == 0);
		CHECK(u_write(ready[1], "R", 1) == 1);
		(void)u_read(gate[0], &byte, 1);
		CHECK(0);
	}
	CHECK(u_read(ready[0], &byte, 1) == 1 && byte == 'R');
	const struct {
		uint32_t real, effective, saved;
		long expected;
	} cases[] = {
		{400, 400, 100, -1}, /* Sender's saved UID alone is not authority. */
		{400, 200, 400, -1}, /* Target's effective UID alone is not sufficient. */
		{100, 400, 400, 0},
		{300, 400, 400, 0},
		{400, 100, 400, 0},
		{400, 300, 400, 0},
		{400, 0, 400, 0},
		{0, 400, 400, -1} /* Privileged bypass requires effective UID 0. */
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		long sender = u_fork();
		CHECK(sender >= 0);
		if (sender == 0) {
			CHECK(call(SYSCALL_SETRESUID, cases[i].real, cases[i].effective,
			    cases[i].saved) == 0);
			CHECK(u_kill((uint64_t)target, 0) == cases[i].expected);
			if (cases[i].expected != 0)
				CHECK(u_kill((uint64_t)target, U_SIGKILL) == -1);
			u_exit(0);
		}
		uint8_t status = 255;
		CHECK(u_waitpid((uint64_t)sender, &status) == sender && status == 0);
		CHECK(u_kill((uint64_t)target, 0) == 0);
	}
	CHECK(u_kill((uint64_t)target, (1ULL << 32) + U_SIGKILL) == -22);
	CHECK(u_kill((uint64_t)target, 32) == -22);
	CHECK(u_kill(0, 0) == -22);
	CHECK(u_kill((uint64_t)target, U_SIGKILL) == 0);
	uint8_t status = 0;
	CHECK(u_waitpid((uint64_t)target, &status) == target && status == 128 + U_SIGKILL);
	CHECK(u_kill((uint64_t)target, 0) == -3);
	CHECK(u_close(ready[0]) == 0 && u_close(ready[1]) == 0);
	CHECK(u_close(gate[0]) == 0 && u_close(gate[1]) == 0);
}

static void
res_id_transaction_checks(void)
{
	long child = u_fork();
	CHECK(child >= 0);
	if (child == 0) {
		struct os_credentials c;
		CHECK(call(SYSCALL_SETRESGID, 100, 0, 300) == 0);
		CHECK(call(SYSCALL_SETRESUID, 100, 200, 300) == 0);
		for (unsigned int group = 0; group < 2; group++) {
			uint64_t operation = group ? SYSCALL_SETRESGID : SYSCALL_SETRESUID;
			for (unsigned int position = 0; position < 3; position++) {
				uint32_t ids[3] = {100, group ? 0 : 200, 300};
				ids[position] = 400;
				CHECK(call(operation, ids[0], ids[1], ids[2]) == -1);
				CHECK(u_getcredentials(&c) == 0);
				CHECK(c.real_uid == 100 && c.effective_uid == 200 && c.saved_uid == 300);
				CHECK(c.real_gid == 100 && c.effective_gid == 0 && c.saved_gid == 300);
			}
		}
		CHECK(call(SYSCALL_SETRESUID, 300, 100, 200) == 0);
		CHECK(call(SYSCALL_SETRESGID, 300, 100, 0) == 0);
		CHECK(call(SYSCALL_SETRESUID, UINT32_MAX, 300, UINT32_MAX) == 0);
		CHECK(u_getcredentials(&c) == 0);
		CHECK(c.real_uid == 300 && c.effective_uid == 300 && c.saved_uid == 200);
		CHECK(c.real_gid == 300 && c.effective_gid == 100 && c.saved_gid == 0);
		u_exit(0);
	}
	uint8_t status = 255;
	CHECK(u_waitpid((uint64_t)child, &status) == child && status == 0);
}

static long
stat_at(uint64_t fd, const char *path, uint64_t *inode)
{
	uint8_t status[144];
	uint64_t number = path == 0 ? SYSCALL_FSTAT : SYSCALL_FSTATAT;
	unsigned char failed;
	register uint64_t flags __asm__("r10") = 0;
	uint64_t second = path == 0 ? (uintptr_t)status : (uintptr_t)path;
	__asm__ volatile ("int $128; setc %1" : "+a" (number), "=qm" (failed) :
	    "D" (fd), "S" (second), "d" ((uintptr_t)status), "r" (flags) :
	    "rcx", "r11", "memory");
	if (failed) return -(long)number;
	__builtin_memcpy(inode, status + 8, sizeof(*inode));
	return 0;
}

static void
directory_fd_checks(void)
{
	uint64_t original = 0, found = 0;
	CHECK(u_mkdir("/tmp/anchor", 0755) == 0);
	long fd = u_open("/tmp/anchor", U_O_RDONLY | U_O_DIRECTORY);
	CHECK(fd >= 0);
	CHECK(stat_at((uint64_t)fd, 0, &original) == 0);
	CHECK(u_unlink("/tmp/anchor", 1) == 0);
	CHECK(u_mkdir("/tmp/anchor", 0755) == 0);
	CHECK(stat_at((uint64_t)fd, ".", &found) == 0 && found == original);
	CHECK(u_close((uint64_t)fd) == 0);
	CHECK(u_unlink("/tmp/anchor", 1) == 0);
	CHECK(u_mkdir("/tmp/anchor-private", 0700) == 0);
	CHECK(u_mkdir("/tmp/anchor-private/child", 0777) == 0);
	CHECK(call(SYSCALL_CHMOD, (uintptr_t)"/tmp/anchor-private/child", 0777, 0) == 0);
	long file = u_open_mode("/tmp/anchor-private/child/file", U_O_CREAT | U_O_WRONLY, 0644);
	CHECK(file >= 0 && u_close((uint64_t)file) == 0);
	fd = u_open("/tmp/anchor-private/child", U_O_RDONLY | U_O_DIRECTORY);
	CHECK(fd >= 0);
	for (unsigned int revoked = 0; revoked < 2; revoked++) {
		CHECK(call(SYSCALL_CHMOD, (uintptr_t)"/tmp/anchor-private/child",
		    revoked ? 0700 : 0777, 0) == 0);
		long child = u_fork();
		CHECK(child >= 0);
		if (child == 0) {
			CHECK(u_setgid(300) == 0 && u_setuid(300) == 0);
			CHECK(stat_at((uint64_t)fd, ".", &found) == (revoked ? -13 : 0));
			if (!revoked) {
				CHECK(stat_at((uint64_t)fd, "file", &found) == 0);
				CHECK(stat_at((uint64_t)fd, "../.", &found) == -13);
			}
			u_exit(0);
		}
		uint8_t status = 255;
		CHECK(u_waitpid((uint64_t)child, &status) == child && status == 0);
	}
	CHECK(call(SYSCALL_CHMOD, (uintptr_t)"/tmp/anchor-private/child", 0777, 0) == 0);
	CHECK(stat_at((uint64_t)fd, 0, &original) == 0);
	uint64_t parent_inode = 0;
	CHECK(stat_at((uint64_t)-100, "/tmp/anchor-private", &parent_inode) == 0);
	CHECK(u_unlink("/tmp/anchor-private/child/file", 0) == 0);
	CHECK(u_unlink("/tmp/anchor-private/child", 1) == 0);
	CHECK(u_unlink("/tmp/anchor-private", 1) == 0);
	CHECK(u_mkdir("/tmp/anchor-private", 0755) == 0);
	CHECK(u_mkdir("/tmp/anchor-private/child", 0755) == 0);
	CHECK(stat_at((uint64_t)fd, ".", &found) == 0 && found == original);
	CHECK(stat_at((uint64_t)fd, "..", &found) == 0 && found == parent_inode);
	CHECK(u_close((uint64_t)fd) == 0);
	fd = u_open("/", U_O_RDONLY | U_O_DIRECTORY);
	CHECK(fd >= 0 && stat_at((uint64_t)fd, 0, &original) == 0);
	CHECK(stat_at((uint64_t)fd, "dev/..", &found) == 0 && found == original);
	CHECK(stat_at((uint64_t)fd, "init/..", &found) == 0 && found == original);
	CHECK(stat_at((uint64_t)fd, "dev/../proc/self", &found) == 0);
	CHECK(u_close((uint64_t)fd) == 0);
}

static void
setid_write_checks(void)
{
	const char *path = "/tmp/setid-write";
	struct u_stat status;
	long writer = u_open_mode(path, U_O_CREAT | U_O_RDWR, 06777);
	CHECK(writer >= 0);
	long reader = u_open(path, U_O_RDONLY);
	CHECK(reader >= 0);
	CHECK(u_write((uint64_t)writer, "R", 1) == 1);
	CHECK(u_stat_path(path, &status) == 0 && status.mode == 06755);
	for (unsigned int operation = 0; operation < 6; operation++) {
		CHECK(call(SYSCALL_CHMOD, (uintptr_t)path, 06777, 0) == 0);
		long child = u_fork();
		CHECK(child >= 0);
		if (child == 0) {
			CHECK(u_setgid(300) == 0 && u_setuid(300) == 0);
			if (operation == 0)
				CHECK(u_write((uint64_t)writer, "U", 1) == 1);
			else if (operation == 1) {
				struct { uint64_t base, length; } vector = {(uintptr_t)"V", 1};
				CHECK(call(SYSCALL_WRITEV, (uint64_t)writer, (uintptr_t)&vector, 1) == 1);
			} else if (operation == 2) {
				long fd = u_open(path, U_O_WRONLY | U_O_TRUNC);
				CHECK(fd >= 0 && u_close((uint64_t)fd) == 0);
			} else if (operation == 3)
				CHECK(u_write((uint64_t)writer, "", 0) == 0);
			else if (operation == 4)
				CHECK(u_write((uint64_t)writer, (const void *)1, 1) == -14);
			else
				CHECK(u_write((uint64_t)reader, "X", 1) == -9);
			CHECK(u_stat_path(path, &status) == 0);
			CHECK(status.mode == (operation < 3 ? 0777U : 06777U));
			u_exit(0);
		}
		uint8_t child_status = 255;
		CHECK(u_waitpid((uint64_t)child, &child_status) == child && child_status == 0);
	}
	CHECK(u_close((uint64_t)reader) == 0);
	CHECK(u_close((uint64_t)writer) == 0);
}

static void
dot_removal_checks(void)
{
	struct u_stat status;
	CHECK(u_mkdir("/tmp/dotdir", 0755) == 0);
	CHECK(u_unlink("/tmp/dotdir/.", 1) == -22);
	CHECK(u_unlink("/tmp/dotdir/.//", 1) == -22);
	CHECK(u_unlink("/tmp/dotdir/.", 0) == -21);
	CHECK(u_chdir("/tmp/dotdir") == 0);
	CHECK(u_unlink(".", 1) == -22);
	CHECK(u_unlink("..", 1) == -22);
	CHECK(u_chdir("/") == 0);
	CHECK(u_stat_path("/tmp/dotdir", &status) == 0);
	CHECK(u_unlink("/tmp/dotdir", 1) == 0);
}

static void
trailing_slash_checks(void)
{
	struct u_stat status;
	char cwd[64];
	long fd = u_open_mode("/tmp/slash-file", U_O_CREAT | U_O_EXCL | U_O_WRONLY, 0644);
	CHECK(fd >= 0 && u_close((uint64_t)fd) == 0);
	CHECK(call(SYSCALL_CHMOD, (uintptr_t)"/tmp/slash-file/", 0777, 0) == -20);
	CHECK(u_unlink("/tmp/slash-file/", 0) == -20);
	CHECK(u_stat_path("/tmp/slash-file", &status) == 0 && status.mode == 0644);
	CHECK(u_open_mode("/tmp/slash-missing/", U_O_CREAT | U_O_WRONLY, 0600) == -20);
	CHECK(u_stat_path("/tmp/slash-missing", &status) == -2);
	CHECK(u_mkdir("/tmp/slash-dir/", 0755) == 0);
	CHECK(u_chdir("/tmp/slash-dir/") == 0);
	CHECK(u_getcwd(cwd, sizeof(cwd)) == 0 && u_streq(cwd, "/tmp/slash-dir"));
	CHECK(u_chdir("/") == 0);
	CHECK(u_unlink("/tmp/slash-dir/", 1) == 0);
	CHECK(u_unlink("/dev/", 1) == -16);
	CHECK(u_unlink("/tmp/slash-file", 0) == 0);
}

static void
console_input_checks(void)
{
	int enabled = 0, acquire = 1;
	CHECK(u_ioctl(1, 0x4f530003U, &enabled) == 0);
	CHECK(u_ioctl(0, 0x4f530004U, &acquire) == 0);
	long child = u_fork();
	CHECK(child >= 0);
	if (child == 0) {
		char byte;
		struct {
			uint32_t input, output, control, local;
			uint8_t chars[32];
			uint32_t input_speed, output_speed;
		} termios = {0};
		CHECK(u_setgid(300) == 0 && u_setuid(300) == 0);
		CHECK(u_ioctl(0, 0x4f530004U, &acquire) == -1);
		CHECK(u_ioctl(0, U_TIOCSRAW, 0) == -1);
		CHECK(u_ioctl(0, U_TIOCCANON, 0) == -1);
		CHECK(u_ioctl(0, 0x5402U, &termios) == -1);
		CHECK(u_read(0, &byte, 1) == -1);
		u_exit(0);
	}
	uint8_t status = 255;
	CHECK(u_waitpid((uint64_t)child, &status) == child && status == 0);
	CHECK(u_ioctl(0, U_TIOCSRAW, 0) == 0);
	CHECK(u_ioctl(0, U_TIOCCANON, 0) == 0);
	enabled = 1;
	CHECK(u_ioctl(1, 0x4f530003U, &enabled) == 0);
}

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
	fd = u_open_mode("/tmp/execute-only", U_O_CREAT | U_O_EXCL | U_O_WRONLY, 0111);
	CHECK(fd >= 0);
	for (offset = 0; offset < status.size;) {
		size_t length = (size_t)(status.size - offset);
		if (length > 512) length = 512;
		CHECK(u_write((uint64_t)fd, exec_image + offset, length) == (long)length);
		offset += length;
	}
	CHECK(u_close((uint64_t)fd) == 0);
	fd = u_open_mode("/tmp/no-execute", U_O_CREAT | U_O_EXCL | U_O_WRONLY, 0644);
	CHECK(fd >= 0 && u_close((uint64_t)fd) == 0);
	for (unsigned int memory = 0; memory < 3; memory++) {
		long child = u_fork();
		CHECK(child >= 0);
		if (child == 0) {
			struct os_credentials c;
			CHECK(call(SYSCALL_SETRESGID, 100, 200, 0) == 0);
			CHECK(call(SYSCALL_SETRESUID, 100, 200, 0) == 0);
			CHECK(u_exec_path("/missing-executable") == -2);
			CHECK(u_open("/tmp/execute-only", U_O_RDONLY) == -13);
			CHECK(u_exec_path("/tmp/execute-only/") == -20);
			CHECK(u_exec_path("/tmp/no-execute") == -13);
			CHECK(u_exec_path("/tmp") == -13);
			CHECK(u_exec_path("/tmp/invalid-elf") == -8);
			CHECK(call(SYSCALL_EXEC, (uintptr_t)"bad", 3, 0) == -22);
			CHECK(u_getcredentials(&c) == 0);
			CHECK(c.saved_uid == 0 && c.saved_gid == 0);
			if (memory == 1)
				(void)call(SYSCALL_EXEC, (uintptr_t)exec_image, status.size, 0);
			else
				(void)u_exec_path(memory == 2 ? "/tmp/execute-only" : "/bin/sh");
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
	res_id_transaction_checks();
	signal_permission_checks();
	directory_fd_checks();
	setid_write_checks();
	dot_removal_checks();
	trailing_slash_checks();
	console_input_checks();
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
