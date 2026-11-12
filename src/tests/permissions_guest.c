#include "include/os.h"
#include "abi/syscall.h"

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
	u_exit(1);
}
#define CHECK(condition) check((condition), __LINE__)

static void
create_file(const char *path)
{
	long fd = u_open_mode(path, U_O_CREAT | U_O_EXCL | U_O_WRONLY, 0644);
	CHECK(fd >= 0);
	CHECK(u_close((uint64_t)fd) == 0);
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
	CHECK(u_setuid(400) == -1);
	CHECK(u_setgid(400) == -1);
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
	CHECK(u_mkdir("/closed", 0700) == 0);
	CHECK(call(SYSCALL_CHOWN, (uintptr_t)"/closed", 100, 100) == 0);
	CHECK(u_mkdir("/group", 0777) == 0);
	CHECK(call(SYSCALL_CHOWN, (uintptr_t)"/group", 100, 500) == 0);
	CHECK(call(SYSCALL_CHMOD, (uintptr_t)"/group", 02777, 0) == 0);
	create_file("/public");
	create_file("/tmp/root-file");
	CHECK(u_mkdir("/tmp/root-dir", 0755) == 0);
	long child = u_fork();
	CHECK(child >= 0);
	if (child == 0) unprivileged_checks();
	uint8_t status = 255;
	CHECK(u_waitpid((uint64_t)child, &status) == child);
	CHECK(status == 0);
	CHECK(u_unlink("/tmp/root-file", 0) == 0);
	CHECK(u_unlink("/tmp/root-dir", 1) == 0);
	u_puts("PERMISSIONS GUEST PASS\n");
	(void)u_power_control(POWER_CONTROL_POWEROFF);
	return 0;
}
