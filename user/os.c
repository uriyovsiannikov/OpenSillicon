#include "include/os.h"

static volatile uint64_t runtime_marker = 1;

static long
syscall3(uint64_t number, uint64_t first, uint64_t second, uint64_t third)
{
	uint64_t result = number;
	unsigned char failed;
	if (runtime_marker != 1)
		return -22;

	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" (first), "S" (second), "d" (third) : "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

long u_write(uint64_t fd, const void *buffer, size_t length)
{ return syscall3(2, fd, (uintptr_t)buffer, length); }
long u_read(uint64_t fd, void *buffer, size_t length)
{ return syscall3(9, fd, (uintptr_t)buffer, length); }
long u_open(const char *path, uint64_t flags)
{ return syscall3(7, (uintptr_t)path, flags, 0); }
long u_close(uint64_t fd) { return syscall3(8, fd, 0, 0); }
long u_stat_path(const char *path, struct u_stat *status)
{ return syscall3(10, (uintptr_t)path, (uintptr_t)status, 0); }
long u_readdir(uint64_t fd, struct u_dirent *entry)
{ return syscall3(11, fd, (uintptr_t)entry, 0); }
long u_fork(void) { return syscall3(4, 0, 0, 0); }
long u_exec_path(const char *path) { return syscall3(12, (uintptr_t)path, 0, 0); }
long u_waitpid(uint64_t pid, uint8_t *status)
{ return syscall3(6, pid, (uintptr_t)status, 0); }
long u_sleep_ticks(uint64_t ticks) { return syscall3(13, ticks, 0, 0); }
_Noreturn void u_exit(uint64_t status)
{
	(void)syscall3(1, status, 0, 0);
	__asm__ volatile ("ud2");
	__builtin_unreachable();
}

void u_putn(const char *string, size_t length) { (void)u_write(1, string, length); }
void u_puts(const char *string) { u_putn(string, u_strlen(string)); }

void
u_put_u64(uint64_t value)
{
	char digits[20];
	size_t count = 0;
	do {
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	while (count != 0)
		u_putn(&digits[--count], 1);
}
