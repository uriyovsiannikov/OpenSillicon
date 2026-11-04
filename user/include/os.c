#include "os.h"
#include "abi/syscall.h"

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

static long
syscall4(uint64_t number, uint64_t first, uint64_t second, uint64_t third,
	uint64_t fourth)
{
	uint64_t result = number;
	register uint64_t r10 __asm__("r10") = fourth;
	unsigned char failed;
	if (runtime_marker != 1)
		return -22;
	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" (first), "S" (second), "d" (third), "r" (r10) :
	    "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

long u_write(uint64_t fd, const void *buffer, size_t length)
{ return syscall3(SYSCALL_WRITE, fd, (uintptr_t)buffer, length); }
long u_read(uint64_t fd, void *buffer, size_t length)
{ return syscall3(SYSCALL_READ, fd, (uintptr_t)buffer, length); }
long u_open(const char *path, uint64_t flags)
{ return syscall3(SYSCALL_OPEN, (uintptr_t)path, flags, 0); }
long u_close(uint64_t fd) { return syscall3(SYSCALL_CLOSE, fd, 0, 0); }
long u_stat_path(const char *path, struct u_stat *status)
{ return syscall3(SYSCALL_STAT, (uintptr_t)path, (uintptr_t)status, 0); }
long u_readdir(uint64_t fd, struct u_dirent *entry)
{ return syscall3(SYSCALL_READDIR, fd, (uintptr_t)entry, 0); }
long u_ioctl(uint64_t fd, uint64_t request, void *argument)
{ return syscall3(SYSCALL_IOCTL, fd, request, (uintptr_t)argument); }
long u_udp_socket(uint64_t family, uint64_t type, uint64_t protocol)
{ return syscall3(SYSCALL_UDP_SOCKET, family, type, protocol); }
long u_udp_bind(uint64_t fd, const struct u_udp_endpoint *endpoint)
{ return syscall3(SYSCALL_UDP_BIND, fd, (uintptr_t)endpoint, sizeof(*endpoint)); }
long u_udp_sendto(uint64_t fd, const void *data, size_t length,
	const struct u_udp_endpoint *destination)
{ return syscall4(SYSCALL_UDP_SENDTO, fd, (uintptr_t)data, length, (uintptr_t)destination); }
long u_udp_recvfrom(uint64_t fd, void *data, size_t capacity,
	struct u_udp_endpoint *source)
{ return syscall4(SYSCALL_UDP_RECVFROM, fd, (uintptr_t)data, capacity, (uintptr_t)source); }
long u_icmp_echo(struct u_icmp_echo_request *request)
{ return syscall3(SYSCALL_ICMP_ECHO, (uintptr_t)request, 0, sizeof(*request)); }
long u_fork(void) { return syscall3(SYSCALL_FORK, 0, 0, 0); }
long u_exec_path(const char *path) { return syscall3(SYSCALL_EXEC_PATH, (uintptr_t)path, 0, 0); }
long u_exec_path_argv(const char *path, size_t argc,
	const char *const argv[])
{ return syscall3(SYSCALL_EXEC_PATH_ARGV, (uintptr_t)path, argc, (uintptr_t)argv); }
long u_waitpid(uint64_t pid, uint8_t *status)
{ return syscall3(SYSCALL_WAITPID, pid, (uintptr_t)status, 0); }
long u_sleep_ticks(uint64_t ticks) { return syscall3(SYSCALL_SLEEP_TICKS, ticks, 0, 0); }
long u_power_control(uint64_t command)
{ return syscall3(SYSCALL_POWER_CONTROL, command, 0, 0); }
long u_pipe(uint32_t descriptors[2])
{ return syscall3(SYSCALL_PIPE, (uintptr_t)descriptors, 0, 0); }
long u_getpid(void) { return syscall3(SYSCALL_GETPID, 0, 0, 0); }
long u_process_snapshot(struct u_process_info *entries, size_t capacity)
{ return syscall3(SYSCALL_PROCESS_SNAPSHOT, (uintptr_t)entries, capacity, 0); }
long u_framebuffer_info(struct os_framebuffer_info *info)
{ return syscall3(SYSCALL_FRAMEBUFFER_INFO, (uintptr_t)info, 0, 0); }
long u_framebuffer_present(const void *pixels, size_t length)
{ return syscall3(SYSCALL_FRAMEBUFFER_PRESENT, (uintptr_t)pixels, length, 0); }
long u_keyboard_poll(struct os_keyboard_event *event)
{ return syscall3(SYSCALL_KEYBOARD_POLL, (uintptr_t)event, 0, 0); }
long u_kill(uint64_t pid, uint64_t signo)
{ return syscall3(SYSCALL_KILL, pid, signo, 0); }
long u_sigaction(uint64_t signo, const struct u_sigaction *new_action,
	struct u_sigaction *old_action)
{ return syscall3(SYSCALL_SIGACTION, signo, (uintptr_t)new_action, (uintptr_t)old_action); }
long u_shm_create(uint64_t size) { return syscall3(SYSCALL_SHM_CREATE, size, 0, 0); }
long u_shm_detach(uint64_t base) { return syscall3(SYSCALL_SHM_DETACH, base, 0, 0); }
long u_set_fs_base(uint64_t base) { return syscall3(SYSCALL_SET_FS_BASE, base, 0, 0); }
#define STRINGIFY_VALUE_INNER(value) #value
#define STRINGIFY_VALUE(value) STRINGIFY_VALUE_INNER(value)
__attribute__((naked, noreturn)) void u_sigreturn(void)
{
	__asm__ ("mov $" STRINGIFY_VALUE(SYSCALL_SIGRETURN) ", %eax\n"
	    "int $0x80\n"
	    "ud2\n");
}
_Noreturn void u_exit(uint64_t status)
{
	(void)syscall3(SYSCALL_EXIT, status, 0, 0);
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
