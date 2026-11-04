#include "include/os.h"
#include "logo_pixels.h"

struct startup_phdr {
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t virtual_address;
	uint64_t physical_address;
	uint64_t file_size;
	uint64_t memory_size;
	uint64_t alignment;
};

extern void _start(void);

#define TLS_INITIAL_VALUE 0x544c535f494e4954ULL
#define TLS_PARENT_VALUE 0x544c535f50415245ULL
#define TLS_CHILD_VALUE 0x544c535f4348494cULL
#define TLS_STORAGE_WORDS 512U

static __thread volatile uint64_t tls_initialized = TLS_INITIAL_VALUE;
static __thread volatile uint64_t tls_zero;
static uint64_t tls_storage[TLS_STORAGE_WORDS] __attribute__((aligned(16)));

static uint32_t init_logo_pixels[INIT_LOGO_WIDTH * INIT_LOGO_HEIGHT];

static size_t
cpu_brand(char *buffer, size_t capacity)
{
	uint32_t eax, ebx, ecx, edx;
	uint32_t words[12];
	size_t length = 0;
	__asm__ volatile ("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx) : "a" (0x80000000U), "c" (0));
	if (eax < 0x80000004U || capacity < sizeof(words))
		return 0;
	for (uint32_t leaf = 0; leaf < 3; leaf++) {
		uint32_t query = 0x80000002U + leaf;
		__asm__ volatile ("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx),
		    "=d" (edx) : "a" (query), "c" (0));
		words[leaf * 4U] = eax;
		words[leaf * 4U + 1U] = ebx;
		words[leaf * 4U + 2U] = ecx;
		words[leaf * 4U + 3U] = edx;
	}
	for (size_t i = 0; i < sizeof(words); i++) {
		char character = (char)(words[i / 4U] >> ((i % 4U) * 8U));
		if (character == '\0')
			break;
		buffer[length++] = character;
	}
	while (length != 0 && buffer[length - 1U] == ' ')
		length--;
	return length;
}

static uint32_t
logo_channel(uint8_t value, uint8_t size, uint8_t shift)
{
	uint32_t maximum = (1U << size) - 1U;
	return ((uint32_t)value * maximum + 127U) / 255U << shift;
}

static void
show_init_banner(void)
{
	struct os_framebuffer_info info;
	struct os_framebuffer_rect rect;
	char processor[48];
	size_t processor_length = cpu_brand(processor, sizeof(processor));
	if (u_framebuffer_info(&info) == 0 && info.bits_per_pixel == 32 &&
	    info.red_mask_size != 0 && info.red_mask_size <= 8 &&
	    info.green_mask_size != 0 && info.green_mask_size <= 8 &&
	    info.blue_mask_size != 0 && info.blue_mask_size <= 8 &&
	    INIT_LOGO_WIDTH + 12U <= info.width &&
	    INIT_LOGO_HEIGHT + 12U <= info.height) {
		for (size_t i = 0; i < INIT_LOGO_WIDTH * INIT_LOGO_HEIGHT; i++) {
			uint8_t packed = init_logo_gray4[i / 2U];
			uint8_t gray = (uint8_t)(((i & 1U) == 0 ? packed >> 4 :
			    packed & 0x0fU) * 17U);
			init_logo_pixels[i] = logo_channel(gray, info.red_mask_size,
			    info.red_mask_shift) |
			    logo_channel(gray, info.green_mask_size,
			    info.green_mask_shift) |
			    logo_channel(gray, info.blue_mask_size,
			    info.blue_mask_shift);
		}
		rect = (struct os_framebuffer_rect){
			.source = (uintptr_t)init_logo_pixels,
			.x = 12,
			.y = 12,
			.width = INIT_LOGO_WIDTH,
			.height = INIT_LOGO_HEIGHT,
			.source_pitch = INIT_LOGO_WIDTH * sizeof(uint32_t)
		};
		(void)u_framebuffer_present_rect(&rect);
	}
	u_puts("\033[3;16H\033[36mWelcome! Running on ");
	if (processor_length != 0)
		u_putn(processor, processor_length);
	else
		u_puts("unknown CPU");
	u_puts("\033[0m\033[6;1H");
}

static void
init_status(const char *component, int success, const char *detail)
{
	size_t length;
	size_t i;

	u_puts("\033[34m*\033[0m Initializing ");
	u_puts(component);
	u_puts(" ...");
	length = u_strlen(component);
	for (i = length + 20; i < 46; i++)
		u_puts(" ");
	u_puts(success ? " \033[32m[ OK ]\033[0m\n" :
	    " \033[31m[ ER ]\033[0m");
	if (!success && detail != 0) {
		u_puts(" ");
		u_puts(detail);
	}
	if (!success)
		u_puts("\n");
}

static int
verify_startup_stack(const uint64_t *stack)
{
	const struct startup_phdr *phdr;
	uint64_t phnum;

	if (stack == 0 || ((uintptr_t)stack & 15U) != 0 || stack[0] != 0 ||
	    stack[1] != 0 || stack[2] != 0 || stack[3] != 3 ||
	    stack[5] != 4 || stack[6] != sizeof(*phdr) || stack[7] != 5 ||
	    stack[9] != 6 || stack[10] != 4096 || stack[11] != 9 ||
	    stack[12] != (uintptr_t)&_start || stack[13] != 7 ||
	    stack[14] != 0 || stack[15] != 0 || stack[16] != 0)
		return 0;
	phnum = stack[8];
	if (phnum == 0 || stack[4] == 0)
		return 0;
	phdr = (const struct startup_phdr *)(uintptr_t)stack[4];
	return phdr[0].type == 1 && (phdr[0].flags & 4U) != 0 &&
	    phdr[0].offset == 0 &&
	    phdr[0].virtual_address <= UINT64_MAX - 64 &&
	    phdr[0].virtual_address + 64 == stack[4] &&
	    phdr[0].file_size >= 64 + phnum * sizeof(*phdr);
}

static const struct startup_phdr *
find_tls_header(const uint64_t *stack)
{
	const struct startup_phdr *headers =
	    (const struct startup_phdr *)(uintptr_t)stack[4];
	uint64_t count = stack[8];
	uint64_t i;

	for (i = 0; i < count; i++) {
		if (headers[i].type == 7)
			return &headers[i];
	}
	return 0;
}

static int
initialize_tls_at(const uint64_t *stack, size_t tp_word)
{
	const struct startup_phdr *tls = find_tls_header(stack);
	uintptr_t thread_pointer;
	uintptr_t alignment;
	uintptr_t rounded_size;
	uint8_t *destination;
	const uint8_t *source;
	uint64_t i;

	if (tls == 0 || tls->memory_size < sizeof(uint64_t) ||
	    tls->file_size > tls->memory_size || tls->alignment == 0 ||
	    (tls->alignment & (tls->alignment - 1)) != 0 ||
	    tp_word >= TLS_STORAGE_WORDS)
		return 0;
	alignment = tls->alignment < 8 ? 8 : tls->alignment;
	if (tls->memory_size > UINTPTR_MAX - alignment + 1)
		return 0;
	rounded_size = (tls->memory_size + alignment - 1) & ~(alignment - 1);
	thread_pointer = ((uintptr_t)&tls_storage[tp_word]) & ~(alignment - 1);
	if (thread_pointer < (uintptr_t)&tls_storage[0] + rounded_size ||
	    thread_pointer + sizeof(uint64_t) >
	    (uintptr_t)&tls_storage[TLS_STORAGE_WORDS])
		return 0;
	destination = (uint8_t *)(thread_pointer - rounded_size);
	source = (const uint8_t *)(uintptr_t)tls->virtual_address;
	for (i = 0; i < tls->memory_size; i++)
		destination[i] = 0;
	for (i = 0; i < tls->file_size; i++)
		destination[i] = source[i];
	*(uintptr_t *)thread_pointer = thread_pointer;
	if (u_set_fs_base(thread_pointer) != 0)
		return 0;
	return 1;
}

static int
verify_tls_state(void)
{
	return tls_initialized == TLS_INITIAL_VALUE && tls_zero == 0;
}

static int
verify_fs_base_validation(void)
{
	uintptr_t saved_thread_pointer = (uintptr_t)&tls_storage[256];
	int valid = u_set_fs_base(0) == 0 &&
	    u_set_fs_base(0xffff800000000000ULL) == -22 &&
	    u_set_fs_base(saved_thread_pointer + 1) == -22 &&
	    u_set_fs_base((uintptr_t)&_start) == -14 &&
	    u_set_fs_base(0x0000500000000000ULL) == -14 &&
	    u_set_fs_base(saved_thread_pointer) == 0;

	return valid && verify_tls_state();
}

static void
start_sleep_regression(const uint64_t *startup_stack)
{
	uint8_t child_status = 0xff;
	long child;

	if (u_sleep_ticks(0) != 0 || u_sleep_ticks(UINT64_MAX) != -22) {
		u_puts("OpenSillicon init: sleep syscall boundary test failed\n");
		u_exit(1);
	}
	child = u_fork();
	if (child < 0) {
		u_puts("OpenSillicon init: sleep test fork failed\n");
		u_exit(1);
	}
	if (child == 0) {
		if (!verify_startup_stack(startup_stack) ||
		    !verify_tls_state() ||
		    u_exec_path("/no/such/startup-image") != -2 ||
		    !verify_startup_stack(startup_stack) ||
		    !verify_tls_state() ||
		    !initialize_tls_at(startup_stack, 384))
			u_exit(1);
		tls_initialized = TLS_CHILD_VALUE;
		for (volatile uint64_t spin = 0; spin < 2000000; spin++) {
			if (tls_initialized != TLS_CHILD_VALUE || tls_zero != 0)
				u_exit(1);
		}
		if (u_sleep_ticks(1) != 0)
			u_exit(1);
		if (tls_initialized != TLS_CHILD_VALUE || tls_zero != 0)
			u_exit(1);
		u_exit(0);
	}
	tls_initialized = TLS_PARENT_VALUE;
	for (volatile uint64_t spin = 0; spin < 2000000; spin++) {
		if (tls_initialized != TLS_PARENT_VALUE || tls_zero != 0)
			u_exit(1);
	}
	if (u_sleep_ticks(5) != 0 || tls_initialized != TLS_PARENT_VALUE ||
	    u_waitpid((uint64_t)child,
	    &child_status) != child || child_status != 0) {
		u_puts("OpenSillicon init: timer sleep regression failed\n");
		u_exit(1);
	}
}

int
user_main(uint64_t argc, char **argv, const uint64_t *startup_stack)
{
	(void)argv;
	show_init_banner();
	if (argc != 0) {
		u_puts("OpenSillicon init: unexpected argv\n");
		u_exit(1);
	}
	if (!verify_startup_stack(startup_stack)) {
		u_puts("OpenSillicon init: malformed ELF startup stack\n");
		u_exit(1);
	}
	if (!initialize_tls_at(startup_stack, 256) || !verify_tls_state() ||
	    !verify_fs_base_validation()) {
		u_puts("OpenSillicon init: TLS/FS-base setup failed\n");
		u_exit(1);
	}
	start_sleep_regression(startup_stack);
	init_status("init", 1, 0);
	for (;;) {
		long child = u_fork();
		uint8_t status = 0xff;

		if (child == 0) {
			(void)u_exec_path("/bin/sh");
			init_status("/bin/sh", 0, "exec failed");
			u_exit(127);
		}
		if (child > 0) {
			init_status("/bin/sh", 1, 0);
			if (u_waitpid((uint64_t)child, &status) < 0) {
				init_status("/bin/sh", 0, "waitpid failed");
				status = 255;
			}
			if (status != 0)
				init_status("/bin/sh", 0, "exited with an error");
		}
		else
			init_status("/bin/sh", 0, "fork failed");
		if (u_sleep_ticks(10) != 0) {
			u_puts("OpenSillicon init: restart delay failed\n");
			u_exit(1);
		}
	}
}
