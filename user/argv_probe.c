#include "include/os.h"

#define STACK_LOW 0x00007fffffffd000ULL
#define STACK_HIGH 0x00007fffffffe000ULL

int
user_main(uint64_t argc, char **argv, const uint64_t *startup_stack)
{
	uint64_t i;
	size_t lengths[U_EXEC_ARG_MAX];
	if (argc == 0 || argc > U_EXEC_ARG_MAX || argv == 0 ||
	    startup_stack == 0 || (uintptr_t)startup_stack < STACK_LOW ||
	    (uintptr_t)startup_stack >= STACK_HIGH ||
	    ((uintptr_t)startup_stack & 15U) != 0 ||
	    startup_stack[0] != argc || (uintptr_t)argv !=
	    (uintptr_t)(startup_stack + 1) || argv[argc] != 0 ||
	    startup_stack[argc + 1] != 0 || startup_stack[argc + 2] != 0)
		return 1;
	for (i = 0; i < argc; i++) {
		uintptr_t address = (uintptr_t)argv[i];
		size_t length = 0;
		if (address < STACK_LOW || address >= STACK_HIGH)
			return 1;
		while (address + length < STACK_HIGH && argv[i][length] != '\0')
			length++;
		if (address + length >= STACK_HIGH || length > U_EXEC_ARG_BYTES_MAX)
			return 1;
		lengths[i] = length;
	}
	u_puts("argv argc=");
	u_put_u64(argc);
	u_puts("\n");
	for (i = 0; i < argc; i++) {
		u_puts("argv[");
		u_put_u64(i);
		u_puts("]=");
		if (lengths[i] <= 128)
			u_putn(argv[i], lengths[i]);
		else {
			u_puts("<");
			u_put_u64(lengths[i]);
			u_puts(" bytes>");
		}
		u_puts("\n");
	}
	return 0;
}
