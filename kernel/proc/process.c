#include <stdint.h>

#include "arch/x86_64/fpu.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/user.h"
#include "kernel/elf.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/pmm.h"
#include "kernel/timer.h"
#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/vmm.h"
#include "kernel/vfs.h"

#define PROCESS_KERNEL_STACK_SIZE 65536U
#define PROCESS_STACK_TOP (0x00007ffffffff000ULL - 0x1000ULL)
#define PROCESS_STACK_PAGE (PROCESS_STACK_TOP - 4096ULL)
#define PROCESS_STACK_SIZE 4096ULL
#define AUXV_AT_NULL 0ULL
#define AUXV_AT_PHDR 3ULL
#define AUXV_AT_PHENT 4ULL
#define AUXV_AT_PHNUM 5ULL
#define AUXV_AT_PAGESZ 6ULL
#define AUXV_AT_BASE 7ULL
#define AUXV_AT_ENTRY 9ULL
#define PROCESS_WAIT_ANY UINT64_MAX
#define PROCESS_ERROR_ECHILD 10U
#define PROCESS_ERROR_EFAULT 14U
#define PROCESS_ERROR_EAGAIN 11U
#define PROCESS_ERROR_ENOMEM 12U
#define PROCESS_ERROR_EINVAL 22U
#define PROCESS_ERROR_EBUSY 16U
#define PROCESS_ERROR_ESRCH 3U
#define PROCESS_ERROR_EINTR 4U
#define PROCESS_ERROR_EACCES 13U
#define PROCESS_ERROR_ENOEXEC 8U
#define PROCESS_ERROR_EIO 5U
#define PROCESS_ERROR_EINVAL 22U
#define PROCESS_USER_BASE 0x10000ULL
#define PROCESS_USER_END 0x00007ffffffff000ULL

enum process_state {
	PROCESS_READY,
	PROCESS_RUNNING,
	PROCESS_BLOCKED,
	PROCESS_ZOMBIE
};

enum process_block_reason {
	PROCESS_BLOCK_NONE,
	PROCESS_BLOCK_WAIT,
	PROCESS_BLOCK_SLEEP,
	PROCESS_BLOCK_CHANNEL
};

struct process_tls_metadata {
	bool present;
	uint64_t image_address;
	uint64_t file_offset;
	uint64_t file_size;
	uint64_t memory_size;
	uint64_t alignment;
};

struct process {
	uint64_t id;
	enum process_state state;
	uint8_t exit_status;
	uint64_t waiting_for;
	uint64_t wait_status_address;
	uint64_t wake_tick;
	void *blocked_channel;
	enum process_block_reason block_reason;
	bool retry_pending;
	bool retry_in_progress;
	struct process *parent;
	struct process *previous;
	struct process *next;
	struct address_space *address_space;
	struct elf_loaded_image *loaded_image;
	struct process_tls_metadata tls;
	void *kernel_stack;
	struct interrupt_frame *saved_frame;
	uint64_t initial_user_stack;
	uint64_t user_fs_base;
	_Alignas(16) uint8_t fpu_state[ARCH_FPU_STATE_SIZE];
	struct fd_table descriptors;
	struct os_signal_state signals;
	bool signal_active;
	uint64_t signal_restorer;
	uint64_t signal_handler_stack;
	struct interrupt_frame signal_saved_frame;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	bool test_private_mapping_child;
	uint64_t test_private_mapping_base;
	uint64_t test_private_mapping_physical;
#endif
};

_Static_assert(_Alignof(struct process) >= 16,
	"process FXSAVE state must be 16-byte aligned");

static struct process *process_head;
static struct process *current_process;
static uint64_t next_process_id = 1;
static uint64_t user_preemptions;
static uint64_t pipe_blocked_ticks;
static bool initialized;
#ifdef KERNEL_TEST_SHM_LIFECYCLE
static uint64_t test_shm_reap_releases;
static uint64_t test_shm_exec_releases;
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
static bool test_private_mapping_armed;
static bool test_private_mapping_failed;
static uint64_t test_private_mapping_faults;
static uint64_t test_private_mapping_zombies;
static uint64_t test_private_mapping_reaps;
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
static bool test_user_fault_capture_armed;
static bool test_user_fault_capture_valid;
static uint64_t test_user_fault_vector;
static uint64_t test_user_fault_error;
#endif

static struct process *find_child(struct process *parent, uint64_t pid,
	bool zombie_only);

static struct process_tls_metadata
process_tls_from_plan(const struct elf_load_plan *plan)
{
	return (struct process_tls_metadata){
		.present = plan->tls_present,
		.image_address = plan->tls_image_address,
		.file_offset = plan->tls_file_offset,
		.file_size = plan->tls_file_size,
		.memory_size = plan->tls_memory_size,
		.alignment = plan->tls_alignment
	};
}

static bool
process_has_pending_signal(const struct process *process)
{
	unsigned int signo;

	for (signo = 1; signo < OS_SIGNAL_COUNT; signo++) {
		if ((process->signals.pending & (1ULL << signo)) != 0 &&
		    os_signal_valid(signo) &&
		    (signo == OS_SIGKILL ||
		    process->signals.actions[signo].handler != OS_SIGNAL_IGNORE))
			return true;
	}
	return false;
}

static void
process_set_interrupted_result(struct process *process)
{
	process->saved_frame->rax = PROCESS_ERROR_EINTR;
	process->saved_frame->rflags |= 1U;
	process->waiting_for = 0;
	process->wait_status_address = 0;
	process->wake_tick = 0;
	process->blocked_channel = NULL;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->retry_pending = false;
	process->retry_in_progress = false;
	process->state = PROCESS_READY;
}

static void
process_wake_retry(struct process *process)
{
	process->waiting_for = 0;
	process->wait_status_address = 0;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->retry_pending = true;
	process->state = PROCESS_READY;
	if (process->saved_frame->rip >= 2)
		process->saved_frame->rip -= 2;
}

static void
process_wake_for_signal(struct process *process)
{
	if (process->state != PROCESS_BLOCKED ||
	    process->block_reason == PROCESS_BLOCK_NONE)
		return;
	if (process->block_reason == PROCESS_BLOCK_WAIT &&
	    find_child(process, process->waiting_for, true) != NULL) {
		process_wake_retry(process);
		return;
	}
	if (process->block_reason == PROCESS_BLOCK_SLEEP &&
	    timer_ticks() >= process->wake_tick) {
		process->wake_tick = 0;
		process->block_reason = PROCESS_BLOCK_NONE;
		process->state = PROCESS_READY;
		return;
	}
	process_set_interrupted_result(process);
}

void
process_syscall_retry_begin(void)
{
	if (current_process == NULL)
		return;
	current_process->retry_in_progress = current_process->retry_pending;
	current_process->retry_pending = false;
}

void
process_capture_user_fs_base(uint64_t base)
{
	if (current_process != NULL)
		current_process->user_fs_base = base;
}

void
process_restore_user_fs_base(void)
{
	arch_fs_base_write(current_process == NULL ? 0 :
	    current_process->user_fs_base);
}

uint64_t
process_set_fs_base(uint64_t base)
{
	if (current_process == NULL)
		return PROCESS_ERROR_EINVAL;
	if (base == 0) {
		current_process->user_fs_base = 0;
		return 0;
	}
	if (base < PROCESS_USER_BASE || base >= PROCESS_USER_END ||
	    (base & (sizeof(uint64_t) - 1)) != 0)
		return PROCESS_ERROR_EINVAL;
	if (!vmm_validate_user_write(current_process->address_space, base,
	    sizeof(uint64_t)))
		return PROCESS_ERROR_EFAULT;
	current_process->user_fs_base = base;
	return 0;
}

static uint64_t
process_kernel_stack_top(const struct process *process)
{
	return (uintptr_t)process->kernel_stack + PROCESS_KERNEL_STACK_SIZE;
}

static void
insert_process(struct process *process)
{
	if (process_head == NULL) {
		process_head = process;
		process->next = process;
		process->previous = process;
		return;
	}
	process->next = process_head;
	process->previous = process_head->previous;
	process_head->previous->next = process;
	process_head->previous = process;
}

static void
remove_process(struct process *process)
{
	if (process->next == process) {
		process_head = NULL;
	} else {
		process->previous->next = process->next;
		process->next->previous = process->previous;
		if (process_head == process)
			process_head = process->next;
	}
	process->next = NULL;
	process->previous = NULL;
}

static void
release_loaded_image(struct elf_loaded_image *image)
{
	if (image == NULL)
		return;
	if (!address_space_destroy_with_pages(&image->address_space) ||
	    !kheap_free(image->page_addresses) || !kheap_free(image)) {
		klog("OpenSillicon panic: failed to release process image\n");
		kernel_halt();
	}
}

static void
release_process(struct process *process)
{
	fd_table_destroy(&process->descriptors);
	if (process->loaded_image != NULL) {
		release_loaded_image(process->loaded_image);
	} else if (process->address_space != NULL) {
		if (!address_space_destroy_with_pages(process->address_space) ||
		    !kheap_free(process->address_space)) {
			klog("OpenSillicon panic: failed to release process address space\n");
			kernel_halt();
		}
	}
	if (!kheap_free(process->kernel_stack) || !kheap_free(process)) {
		klog("OpenSillicon panic: failed to release process resources\n");
		kernel_halt();
	}
}

static struct process *
find_next_ready(struct process *after)
{
	struct process *process;

	if (process_head == NULL)
		return NULL;
	process = after == NULL ? process_head : after->next;
	do {
		if (process->state == PROCESS_READY)
			return process;
		process = process->next;
	} while (process != (after == NULL ? process_head : after->next));
	return NULL;
}

static struct interrupt_frame *
schedule_from(struct process *after)
{
	struct process *next;

	if (current_process != NULL)
		arch_fpu_save(current_process->fpu_state);
	for (;;) {
		next = find_next_ready(after);
		if (next != NULL)
			break;
		if (current_process != NULL && current_process->parent == NULL &&
		    current_process->state == PROCESS_ZOMBIE) {
			uint64_t status = current_process->exit_status;

			current_process = NULL;
			arch_fpu_restore_kernel_state();
			if (!address_space_activate(vmm_kernel_address_space())) {
				klog("OpenSillicon panic: cannot restore kernel address space\n");
				kernel_halt();
			}
			user_return_to_kernel(status);
		}
		if (process_head != NULL) {
			struct process *process = process_head;
			bool blocked_exists = false;

			do {
				if (process->state == PROCESS_BLOCKED) {
					blocked_exists = true;
					break;
				}
				process = process->next;
			} while (process != process_head);
			if (blocked_exists) {
				arch_fpu_restore_kernel_state();
				__asm__ volatile ("sti; hlt; cli" : : : "memory");
				continue;
			}
		}
		klog("OpenSillicon panic: no runnable process\n");
		kernel_halt();
	}
	current_process = next;
	current_process->state = PROCESS_RUNNING;
	if (current_process->saved_frame == NULL ||
	    !address_space_activate(current_process->address_space)) {
		klog("OpenSillicon panic: invalid process context\n");
		kernel_halt();
	}
	arch_fpu_restore(current_process->fpu_state);
	gdt_set_kernel_stack(process_kernel_stack_top(current_process));
	return current_process->saved_frame;
}

static void
initialize_user_frame(struct interrupt_frame *frame, uint64_t entry,
	uint64_t stack)
{
	uint64_t *words = (uint64_t *)frame;
	size_t i;

	for (i = 0; i < sizeof(*frame) / sizeof(*words); i++)
		words[i] = 0;
	frame->rip = entry;
	frame->cs = 0x23;
	frame->rflags = 0x202;
	frame->user_rsp = stack;
	frame->user_ss = 0x1b;
}

static bool
build_initial_stack(const struct elf_loaded_image *image, uint64_t physical,
	uint64_t *stack_pointer, size_t argc, const char *const argv[])
{
	volatile uint64_t *stack;
	volatile uint8_t *stack_bytes;
	uint64_t top = PROCESS_STACK_TOP;
	uint64_t words[PROCESS_EXEC_ARG_MAX + 17U];
	uint64_t byte_count;
	uint64_t rsp;
	uint64_t offset;
	uint64_t string_cursor;
	uint64_t string_bytes = 0;
	uint64_t hhdm = vmm_hhdm_offset();
	uint64_t i;

	if (image == NULL || stack_pointer == NULL || argc > PROCESS_EXEC_ARG_MAX ||
	    (argc != 0 && argv == NULL) ||
	    image->plan.program_header_address == 0 ||
	    image->plan.program_header_entry_size != 56 ||
	    image->plan.program_header_count == 0 ||
	    image->entry != image->plan.entry)
		return false;
	for (i = 0; i < argc; i++) {
		uint64_t length = 0;
		if (argv[i] == NULL)
			return false;
		while (length <= PROCESS_EXEC_ARG_BYTES_MAX && argv[i][length] != '\0')
			length++;
		if (length > PROCESS_EXEC_ARG_BYTES_MAX ||
		    string_bytes > PROCESS_EXEC_ARG_BYTES_MAX - length - 1)
			return false;
		string_bytes += length + 1;
	}
	byte_count = (argc + 17U) * sizeof(uint64_t);
	if (byte_count > top || string_bytes > PROCESS_STACK_SIZE - byte_count - 15)
		return false;
	rsp = (top - byte_count) & ~15ULL;
	if (rsp < PROCESS_STACK_PAGE || rsp > top ||
	    byte_count > top - rsp || hhdm > UINT64_MAX - physical ||
	    PROCESS_STACK_SIZE - 1 > UINT64_MAX - (hhdm + physical))
		return false;
	stack = (volatile uint64_t *)(uintptr_t)(hhdm + physical);
	stack_bytes = (volatile uint8_t *)(uintptr_t)(hhdm + physical);
	offset = (rsp - PROCESS_STACK_PAGE) / sizeof(uint64_t);
	if ((rsp - PROCESS_STACK_PAGE) % sizeof(uint64_t) != 0 ||
	    offset > PROCESS_STACK_SIZE / sizeof(uint64_t) ||
	    argc + 17U >
	    PROCESS_STACK_SIZE / sizeof(uint64_t) - offset)
		return false;
	words[0] = argc;
	string_cursor = top - string_bytes;
	for (i = 0; i < argc; i++) {
		uint64_t j = 0;
		words[1 + i] = string_cursor;
		for (;;) {
			uint8_t byte = (uint8_t)argv[i][j];
			stack_bytes[string_cursor - PROCESS_STACK_PAGE + j] = byte;
			j++;
			if (byte == 0)
				break;
		}
		string_cursor += j;
	}
	words[1 + argc] = 0;
	words[2 + argc] = 0;
	words[3 + argc] = AUXV_AT_PHDR;
	words[4 + argc] = image->plan.program_header_address;
	words[5 + argc] = AUXV_AT_PHENT;
	words[6 + argc] = image->plan.program_header_entry_size;
	words[7 + argc] = AUXV_AT_PHNUM;
	words[8 + argc] = image->plan.program_header_count;
	words[9 + argc] = AUXV_AT_PAGESZ;
	words[10 + argc] = PROCESS_STACK_SIZE;
	words[11 + argc] = AUXV_AT_ENTRY;
	words[12 + argc] = image->entry;
	words[13 + argc] = AUXV_AT_BASE;
	words[14 + argc] = 0;
	words[15 + argc] = AUXV_AT_NULL;
	words[16 + argc] = 0;
	for (i = 0; i < argc + 17U; i++)
		stack[offset + i] = words[i];
	*stack_pointer = rsp;
	return true;
}

static bool
map_process_stack(struct elf_loaded_image *image, bool startup_stack,
	uint64_t *stack_pointer, size_t argc, const char *const argv[])
{
	uint64_t physical;
	uint64_t initial_stack = PROCESS_STACK_TOP;
	uint64_t hhdm = vmm_hhdm_offset();
	volatile uint8_t *stack;
	uint64_t i;

	if (image == NULL || stack_pointer == NULL || !pmm_alloc_page(&physical))
		return false;
	if (hhdm > UINT64_MAX - physical ||
	    PROCESS_STACK_SIZE - 1 > UINT64_MAX - (hhdm + physical)) {
		(void)pmm_free_page(physical);
		return false;
	}
	stack = (volatile uint8_t *)(uintptr_t)(hhdm + physical);
	for (i = 0; i < PROCESS_STACK_SIZE; i++)
		stack[i] = 0;
	if (startup_stack &&
	    !build_initial_stack(image, physical, &initial_stack, argc, argv)) {
		(void)pmm_free_page(physical);
		return false;
	}
	if (!vmm_map_page(&image->address_space, PROCESS_STACK_PAGE, physical,
	    VMM_USER | VMM_WRITE)) {
		(void)pmm_free_page(physical);
		return false;
	}
	*stack_pointer = initial_stack;
	return true;
}

void
process_system_init(void)
{
	process_head = NULL;
	current_process = NULL;
	next_process_id = 1;
	user_preemptions = 0;
	pipe_blocked_ticks = 0;
#ifdef KERNEL_TEST_SHM_LIFECYCLE
	test_shm_reap_releases = 0;
	test_shm_exec_releases = 0;
#endif
	initialized = true;
}

struct fd_table *
process_current_fd_table(void)
{
	return current_process == NULL ? NULL : &current_process->descriptors;
}

uint64_t
process_current_pid(void)
{
	return current_process == NULL ? 1 : current_process->id;
}

uint64_t
process_kill(uint64_t pid, unsigned int signo)
{
	struct process *process;

	if (pid == 0 || (int64_t)pid < 0)
		return PROCESS_ERROR_EINVAL;
	if (!os_signal_valid(signo))
		return PROCESS_ERROR_EINVAL;
	if (process_head == NULL)
		return PROCESS_ERROR_ESRCH;
	process = process_head;
	do {
		if (process->id == pid && process->state != PROCESS_ZOMBIE) {
			if (signo != OS_SIGKILL &&
			    process->signals.actions[signo].handler ==
			    OS_SIGNAL_IGNORE)
				return 0;
			(void)os_signal_queue(&process->signals, signo);
			process_wake_for_signal(process);
			return 0;
		}
		process = process->next;
	} while (process != process_head);
	return PROCESS_ERROR_ESRCH;
}

struct interrupt_frame *
process_sigaction(struct interrupt_frame *frame, unsigned int signo,
	uint64_t new_address, uint64_t old_address, uint64_t *error)
{
	static const uint8_t restorer_bytes[] = {
		0xb8, 0x11, 0x00, 0x00, 0x00, 0xcd, 0x80, 0x0f, 0x0b
	};
	struct os_signal_action new_action;
	struct os_signal_action old_action;
	uint8_t actual_restorer[sizeof(restorer_bytes)];
	struct process *process = current_process;
	size_t i;

	if (process == NULL || !os_signal_valid(signo) ||
	    (new_address == 0 && old_address == 0)) {
		*error = PROCESS_ERROR_EINVAL;
		return frame;
	}
	if (old_address != 0 && !vmm_validate_user_write(process->address_space,
	    old_address, sizeof(old_action))) {
		*error = PROCESS_ERROR_EFAULT;
		return frame;
	}
	if (new_address != 0 && !vmm_copy_from_user(process->address_space,
	    &new_action, new_address, sizeof(new_action))) {
		*error = PROCESS_ERROR_EFAULT;
		return frame;
	}
	if (new_address != 0 && !os_signal_action_valid(signo, &new_action)) {
		*error = PROCESS_ERROR_EINVAL;
		return frame;
	}
	if (new_address != 0 && new_action.handler > OS_SIGNAL_IGNORE &&
	    (!vmm_validate_user_exec(process->address_space, new_action.handler, 1) ||
	    !vmm_validate_user_exec(process->address_space, new_action.restorer,
	    sizeof(restorer_bytes)) ||
	    !vmm_copy_from_user(process->address_space, actual_restorer,
	    new_action.restorer, sizeof(actual_restorer)))) {
		*error = PROCESS_ERROR_EINVAL;
		return frame;
	}
	if (new_address != 0 && new_action.handler > OS_SIGNAL_IGNORE) {
		for (i = 0; i < sizeof(restorer_bytes); i++) {
			if (actual_restorer[i] != restorer_bytes[i]) {
				*error = PROCESS_ERROR_EINVAL;
				return frame;
			}
		}
	}
	old_action = process->signals.actions[signo];
	if (old_address != 0 && !vmm_copy_to_user(process->address_space,
	    old_address, &old_action, sizeof(old_action))) {
		*error = PROCESS_ERROR_EFAULT;
		return frame;
	}
	if (new_address != 0)
		(void)os_signal_set_action(&process->signals, signo, &new_action, NULL);
	frame->rax = 0;
	*error = 0;
	return frame;
}

struct interrupt_frame *
process_prepare_user_return(struct interrupt_frame *frame)
{
	static const uint8_t restorer_bytes[] = {
		0xb8, 0x11, 0x00, 0x00, 0x00, 0xcd, 0x80, 0x0f, 0x0b
	};

	while (frame != NULL && (frame->cs & 3U) == 3U &&
	    current_process != NULL) {
		struct os_signal_action action;
		unsigned int signo;

		if (current_process->retry_pending)
			return frame;
		current_process->retry_in_progress = false;
		signo = os_signal_take(&current_process->signals);
		if (signo == 0)
			return frame;
		action = current_process->signals.actions[signo];
		if (action.handler == OS_SIGNAL_IGNORE && signo != OS_SIGKILL)
			continue;
		if (signo == OS_SIGKILL || action.handler == OS_SIGNAL_DEFAULT) {
			frame = process_exit(frame, 128U + signo);
			continue;
		}
		if (current_process->signal_active) {
			(void)os_signal_queue(&current_process->signals, signo);
			return frame;
		}
		{
			uint64_t old_stack = frame->user_rsp;
			uint64_t new_stack;
			uint64_t return_address = action.restorer;
			uint8_t actual_restorer[sizeof(restorer_bytes)];
			size_t i;

			if (!os_signal_user_stack(old_stack, &new_stack)) {
				frame = process_exit(frame, 128U + 14U);
				continue;
			}
			if (!vmm_validate_user_write(current_process->address_space,
			    new_stack, (size_t)(old_stack - new_stack)) ||
			    !vmm_validate_user_exec(current_process->address_space,
			    action.handler, 1) ||
			    !vmm_validate_user_exec(current_process->address_space,
			    action.restorer, sizeof(restorer_bytes)) ||
			    !vmm_copy_from_user(current_process->address_space,
			    actual_restorer, action.restorer, sizeof(actual_restorer))) {
				frame = process_exit(frame, 128U + 14U);
				continue;
			}
			for (i = 0; i < sizeof(restorer_bytes); i++) {
				if (actual_restorer[i] != restorer_bytes[i])
					break;
			}
			if (i != sizeof(restorer_bytes) ||
			    !vmm_copy_to_user(current_process->address_space, new_stack,
			    &return_address, sizeof(return_address))) {
				frame = process_exit(frame, 128U + 14U);
				continue;
			}
			current_process->signal_saved_frame = *frame;
			current_process->signal_active = true;
			current_process->signal_restorer = action.restorer;
			current_process->signal_handler_stack = new_stack;
			frame->rip = action.handler;
			frame->rdi = signo;
			frame->user_rsp = new_stack;
			return frame;
		}
	}
	return frame;
}

struct interrupt_frame *
process_sigreturn(struct interrupt_frame *frame, uint64_t *error)
{
	static const uint8_t restorer_bytes[] = {
		0xb8, 0x11, 0x00, 0x00, 0x00, 0xcd, 0x80, 0x0f, 0x0b
	};
	struct process *process = current_process;
	uint8_t actual_restorer[sizeof(restorer_bytes)];
	size_t i;

	if (process == NULL || !process->signal_active) {
		*error = PROCESS_ERROR_EINVAL;
		return frame;
	}
	if (process->signal_restorer > UINT64_MAX - 7 ||
	    frame->vector != 128U ||
	    frame->rax != SYSCALL_SIGRETURN ||
	    frame->rip != process->signal_restorer + 7 ||
	    frame->user_rsp != process->signal_handler_stack + 8 ||
	    !vmm_validate_user_exec(process->address_space,
	    process->signal_restorer, sizeof(restorer_bytes)) ||
	    !vmm_copy_from_user(process->address_space, actual_restorer,
	    process->signal_restorer, sizeof(actual_restorer)))
		return process_exit(frame, 128U + 14U);
	for (i = 0; i < sizeof(restorer_bytes); i++) {
		if (actual_restorer[i] != restorer_bytes[i])
			return process_exit(frame, 128U + 14U);
	}
	process->signal_active = false;
	process->signal_restorer = 0;
	process->signal_handler_stack = 0;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->retry_pending = false;
	process->retry_in_progress = false;
	*error = 0;
	return &process->signal_saved_frame;
}

uint64_t
process_user_preemptions(void)
{
	return user_preemptions;
}

uint64_t
process_pipe_blocked_ticks(void)
{
	return pipe_blocked_ticks;
}

#ifdef KERNEL_TEST_SHM_LIFECYCLE
uint64_t
process_test_shm_reap_releases(void)
{
	return test_shm_reap_releases;
}

uint64_t
process_test_shm_exec_releases(void)
{
	return test_shm_exec_releases;
}
#endif

static uint64_t
process_run_elf_internal(const void *image, size_t image_size,
	bool startup_stack)
{
	struct process *root;
	uint64_t status;

	if (!initialized || process_head != NULL || image == NULL)
		return UINT64_MAX;
	root = kheap_alloc(sizeof(*root));
	if (root == NULL)
		return UINT64_MAX;
	root->loaded_image = kheap_alloc(sizeof(*root->loaded_image));
	root->kernel_stack = kheap_alloc(PROCESS_KERNEL_STACK_SIZE);
	if (root->loaded_image == NULL || root->kernel_stack == NULL ||
	    !elf_image_load(image, image_size, root->loaded_image)) {
		if (root->loaded_image != NULL)
			(void)kheap_free(root->loaded_image);
		if (root->kernel_stack != NULL)
			(void)kheap_free(root->kernel_stack);
		(void)kheap_free(root);
		return UINT64_MAX;
	}
	root->id = next_process_id++;
	root->state = PROCESS_RUNNING;
	root->exit_status = 0;
	root->waiting_for = 0;
	root->wait_status_address = 0;
	root->wake_tick = 0;
	root->blocked_channel = NULL;
	root->block_reason = PROCESS_BLOCK_NONE;
	root->retry_pending = false;
	root->retry_in_progress = false;
	root->parent = NULL;
	root->address_space = &root->loaded_image->address_space;
	root->tls = process_tls_from_plan(&root->loaded_image->plan);
	root->saved_frame = NULL;
	os_signal_state_init(&root->signals);
	root->signal_active = false;
	root->signal_restorer = 0;
	root->signal_handler_stack = 0;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	root->test_private_mapping_child = false;
	root->test_private_mapping_base = 0;
	root->test_private_mapping_physical = 0;
#endif
	root->initial_user_stack = PROCESS_STACK_TOP;
	root->user_fs_base = 0;
	arch_fpu_init_process_state(root->fpu_state);
	fd_table_init_standard(&root->descriptors);
	if (!map_process_stack(root->loaded_image, startup_stack,
	    &root->initial_user_stack, 0, NULL)) {
		release_process(root);
		return UINT64_MAX;
	}
	insert_process(root);
	current_process = root;
	if (!address_space_activate(root->address_space)) {
		klog("OpenSillicon panic: failed to activate initial process\n");
		kernel_halt();
	}
	gdt_set_kernel_stack(process_kernel_stack_top(root));
	arch_fpu_restore(root->fpu_state);
	status = user_enter(root->loaded_image->entry, root->initial_user_stack);
	arch_fpu_restore_kernel_state();
	gdt_restore_kernel_stack();
	if (!address_space_activate(vmm_kernel_address_space())) {
		klog("OpenSillicon panic: failed to restore kernel mappings\n");
		kernel_halt();
	}
	while (process_head != NULL) {
		struct process *process = process_head;

		remove_process(process);
		release_process(process);
	}
	current_process = NULL;
	return status;
}

uint64_t
process_run_elf(const void *image, size_t image_size)
{
	return process_run_elf_internal(image, image_size, false);
}

#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
uint64_t
process_test_private_mapping_lifecycle(const void *image, size_t image_size)
{
	uint64_t status;

	test_private_mapping_armed = true;
	test_private_mapping_failed = false;
	test_private_mapping_faults = 0;
	test_private_mapping_zombies = 0;
	test_private_mapping_reaps = 0;
	status = process_run_elf_internal(image, image_size, false);
	if (status != 0 || test_private_mapping_armed ||
	    test_private_mapping_failed || test_private_mapping_faults != 1 ||
	    test_private_mapping_zombies != 1 || test_private_mapping_reaps != 1)
		return UINT64_MAX;
	return status;
}
#endif

#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
void
process_test_arm_user_fault_capture(void)
{
	test_user_fault_capture_armed = true;
	test_user_fault_capture_valid = false;
	test_user_fault_vector = UINT64_MAX;
	test_user_fault_error = UINT64_MAX;
}

bool
process_test_user_fault_capture(uint64_t *vector, uint64_t *error_code)
{
	if (!test_user_fault_capture_valid || vector == NULL || error_code == NULL)
		return false;
	*vector = test_user_fault_vector;
	*error_code = test_user_fault_error;
	test_user_fault_capture_valid = false;
	return true;
}
#endif

uint64_t
process_run_path(const char *path, bool startup_stack)
{
	struct vfs_stat status;
	struct fd_object *file = NULL;
	void *image;
	size_t offset = 0;
	int error;
	uint64_t result;

	if (path == NULL || vfs_stat_path(path, &status) != 0 ||
	    status.type != VNODE_REGULAR || (status.mode & 0111U) == 0 ||
	    status.size == 0 || status.size > SIZE_MAX ||
	    vfs_open(path, VFS_O_RDONLY, 0, &file) != 0)
		return UINT64_MAX;
	image = kheap_alloc((size_t)status.size);
	if (image == NULL) {
		fd_object_put(file);
		return UINT64_MAX;
	}
	while (offset < status.size) {
		size_t amount = 0;

		error = fd_object_read(file, (uint8_t *)image + offset,
		    (size_t)(status.size - offset), &amount);
		if (error != 0 || amount == 0) {
			(void)kheap_free(image);
			fd_object_put(file);
			return UINT64_MAX;
		}
		offset += amount;
	}
	fd_object_put(file);
	/* Path-based launches always receive the standard argc/envp/auxv stack. */
	(void)startup_stack;
	result = process_run_elf_internal(image, (size_t)status.size, true);
	(void)kheap_free(image);
	return result;
}

struct interrupt_frame *
process_timer_preempt(struct interrupt_frame *frame)
{
	struct process *next;

	if (current_process == NULL || (frame->cs & 3U) != 3U)
		return frame;
	current_process->saved_frame = frame;
	current_process->state = PROCESS_READY;
	next = find_next_ready(current_process);
	if (next != NULL && next != current_process)
		user_preemptions++;
	return schedule_from(current_process);
}

struct interrupt_frame *
process_fork(struct interrupt_frame *frame, uint64_t *error)
{
	struct process *child;
	uintptr_t stack_top;
	struct interrupt_frame *child_frame;
	uint64_t *source_words = (uint64_t *)frame;
	uint64_t *destination_words;
	size_t i;

	if (current_process == NULL) {
		*error = PROCESS_ERROR_ECHILD;
		return frame;
	}
	if (current_process->signal_active) {
		*error = PROCESS_ERROR_EBUSY;
		return frame;
	}
	child = kheap_alloc(sizeof(*child));
	if (child == NULL) {
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	arch_fpu_save(current_process->fpu_state);
	{
		size_t byte;

		for (byte = 0; byte < ARCH_FPU_STATE_SIZE; byte++)
			child->fpu_state[byte] = current_process->fpu_state[byte];
	}
	child->kernel_stack = kheap_alloc(PROCESS_KERNEL_STACK_SIZE);
	child->address_space = kheap_alloc(sizeof(*child->address_space));
	if (child->kernel_stack == NULL || child->address_space == NULL ||
	    !address_space_clone(current_process->address_space,
	    child->address_space)) {
		if (child->address_space != NULL)
			(void)kheap_free(child->address_space);
		if (child->kernel_stack != NULL)
			(void)kheap_free(child->kernel_stack);
		(void)kheap_free(child);
		*error = PROCESS_ERROR_EAGAIN;
		return frame;
	}
	child->id = next_process_id++;
	child->state = PROCESS_READY;
	child->exit_status = 0;
	child->waiting_for = 0;
	child->wait_status_address = 0;
	child->wake_tick = 0;
	child->blocked_channel = NULL;
	child->block_reason = PROCESS_BLOCK_NONE;
	child->retry_pending = false;
	child->retry_in_progress = false;
	child->parent = current_process;
	child->loaded_image = NULL;
	child->tls = current_process->tls;
	child->initial_user_stack = current_process->initial_user_stack;
	child->user_fs_base = current_process->user_fs_base;
	os_signal_fork_state(&current_process->signals, &child->signals);
	child->signal_active = false;
	child->signal_restorer = 0;
	child->signal_handler_stack = 0;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	child->test_private_mapping_child = false;
	child->test_private_mapping_base = 0;
	child->test_private_mapping_physical = 0;
#endif
	if (!fd_table_clone(&child->descriptors, &current_process->descriptors)) {
		(void)address_space_destroy_with_pages(child->address_space);
		(void)kheap_free(child->address_space);
		(void)kheap_free(child->kernel_stack);
		(void)kheap_free(child);
		*error = PROCESS_ERROR_EAGAIN;
		return frame;
	}
	stack_top = process_kernel_stack_top(child);
	child_frame = (struct interrupt_frame *)(stack_top - sizeof(*child_frame));
	destination_words = (uint64_t *)child_frame;
	for (i = 0; i < sizeof(*frame) / sizeof(*source_words); i++)
		destination_words[i] = source_words[i];
	child_frame->rax = 0;
	child_frame->rflags &= ~1ULL;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (test_private_mapping_armed) {
		static const uint8_t instruction = 0xc3;
		static const uint64_t marker = 0x4e585f4c49564531ULL;
		uint64_t base;

		if (vmm_private_map(child->address_space, 4096, &base) != 0 ||
		    !vmm_copy_to_user(child->address_space, base, &instruction,
		    sizeof(instruction)) ||
		    !vmm_copy_to_user(child->address_space, base + 8, &marker,
		    sizeof(marker))) {
			test_private_mapping_failed = true;
			fd_table_destroy(&child->descriptors);
			if (!address_space_destroy_with_pages(child->address_space))
				kernel_halt();
			(void)kheap_free(child->address_space);
			(void)kheap_free(child->kernel_stack);
			(void)kheap_free(child);
			*error = PROCESS_ERROR_EAGAIN;
			return frame;
		}
		child->test_private_mapping_child = true;
		child->test_private_mapping_base = base;
		child->test_private_mapping_physical =
		    child->address_space->private_mappings->physical_pages[0];
		child_frame->rip = base;
		test_private_mapping_armed = false;
	}
#endif
	child->saved_frame = child_frame;
	insert_process(child);
	frame->rax = child->id;
	frame->rflags &= ~1ULL;
	*error = 0;
	return frame;
}

static struct interrupt_frame *
replace_process_image(struct interrupt_frame *frame, const void *image,
	size_t image_size, uint64_t invalid_elf_error, uint64_t *error,
	size_t argc, const char *const argv[])
{
	struct process *process = current_process;
	struct elf_loaded_image *replacement;
	uint64_t new_entry;
	struct elf_loaded_image *old_image;
	struct address_space *old_space;
	struct interrupt_frame *new_frame;
	uintptr_t stack_top;
#ifdef KERNEL_TEST_FS_BASE
	bool had_user_fs_base;
#endif

	if (process == NULL || image == NULL || image_size == 0) {
		*error = PROCESS_ERROR_EINVAL;
		return frame;
	}
	replacement = kheap_alloc(sizeof(*replacement));
	if (replacement == NULL || !elf_image_load(image, image_size,
	    replacement)) {
		if (replacement != NULL)
			(void)kheap_free(replacement);
		*error = invalid_elf_error;
		return frame;
	}
	if (!map_process_stack(replacement, true, &stack_top, argc, argv)) {
		release_loaded_image(replacement);
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	new_entry = replacement->entry;
	old_image = process->loaded_image;
	old_space = process->address_space;
#ifdef KERNEL_TEST_FS_BASE
	had_user_fs_base = process->user_fs_base != 0;
#endif
	if (!address_space_activate(&replacement->address_space)) {
		release_loaded_image(replacement);
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	process->loaded_image = replacement;
	process->address_space = &replacement->address_space;
	process->tls = process_tls_from_plan(&replacement->plan);
	process->user_fs_base = 0;
	arch_fpu_init_process_state(process->fpu_state);
	arch_fpu_restore(process->fpu_state);
#ifdef KERNEL_TEST_FS_BASE
	if (had_user_fs_base) {
		if (process->user_fs_base != 0 || arch_fs_base_read() != 0)
			kernel_halt();
		klog("OpenSillicon test: successful exec reset FS base\n");
	}
#endif
	if (old_image != NULL) {
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		uint64_t live_before = shm_test_live_pages();
#endif
		release_loaded_image(old_image);
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		if (shm_test_live_pages() < live_before)
			test_shm_exec_releases++;
#endif
	} else {
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		uint64_t live_before = shm_test_live_pages();
#endif
		if (!address_space_destroy_with_pages(old_space) ||
		    !kheap_free(old_space)) {
			klog("OpenSillicon panic: exec could not release old address space\n");
			kernel_halt();
		}
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		if (shm_test_live_pages() < live_before)
			test_shm_exec_releases++;
#endif
	}
	process->initial_user_stack = stack_top;
	os_signal_exec_state(&process->signals);
	process->signal_active = false;
	process->signal_restorer = 0;
	process->signal_handler_stack = 0;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->retry_pending = false;
	process->retry_in_progress = false;
	new_frame = (struct interrupt_frame *)(process_kernel_stack_top(process) -
	    sizeof(*new_frame));
	initialize_user_frame(new_frame, new_entry, process->initial_user_stack);
	process->saved_frame = new_frame;
	*error = 0;
	return new_frame;
}

struct interrupt_frame *
process_exec(struct interrupt_frame *frame, uint64_t *error)
{
	void *image_copy;
	struct interrupt_frame *result;
	uint64_t image_size = frame->rsi;

	if (current_process == NULL || image_size == 0 || image_size > SIZE_MAX) {
		*error = PROCESS_ERROR_EFAULT;
		return frame;
	}
	image_copy = kheap_alloc((size_t)image_size);
	if (image_copy == NULL) {
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	if (!vmm_copy_from_user(current_process->address_space, image_copy,
	    frame->rdi, (size_t)image_size)) {
		(void)kheap_free(image_copy);
		*error = PROCESS_ERROR_EFAULT;
		return frame;
	}
	result = replace_process_image(frame, image_copy, (size_t)image_size,
	    PROCESS_ERROR_EINVAL, error, 0, NULL);
	(void)kheap_free(image_copy);
	return result;
}

struct interrupt_frame *
process_exec_path_argv(struct interrupt_frame *frame, const char *path,
	size_t argc, const char *const argv[], uint64_t *error)
{
	struct vfs_stat status;
	struct fd_object *file = NULL;
	void *image;
	struct interrupt_frame *result;
	size_t offset = 0;
	int rc;

	if (current_process == NULL || path == NULL) {
		*error = PROCESS_ERROR_EFAULT;
		return frame;
	}
	rc = vfs_stat_path(path, &status);
	if (rc != 0) {
		*error = (uint64_t)-rc;
		return frame;
	}
	if (status.type != VNODE_REGULAR || (status.mode & 0111U) == 0) {
		*error = PROCESS_ERROR_EACCES;
		return frame;
	}
	if (status.size == 0 || status.size > SIZE_MAX) {
		*error = PROCESS_ERROR_ENOEXEC;
		return frame;
	}
	rc = vfs_open(path, VFS_O_RDONLY, 0, &file);
	if (rc != 0) {
		*error = (uint64_t)-rc;
		return frame;
	}
	image = kheap_alloc((size_t)status.size);
	if (image == NULL) {
		fd_object_put(file);
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	while (offset < status.size) {
		size_t amount = 0;

		rc = fd_object_read(file, (uint8_t *)image + offset,
		    (size_t)(status.size - offset), &amount);
		if (rc != 0 || amount == 0) {
			(void)kheap_free(image);
			fd_object_put(file);
			*error = rc == 0 ? PROCESS_ERROR_EIO : (uint64_t)-rc;
			return frame;
		}
		offset += amount;
	}
	fd_object_put(file);
	result = replace_process_image(frame, image, (size_t)status.size,
	    PROCESS_ERROR_ENOEXEC, error, argc, argv);
	(void)kheap_free(image);
	return result;
}

struct interrupt_frame *
process_exec_path(struct interrupt_frame *frame, const char *path,
	uint64_t *error)
{
	return process_exec_path_argv(frame, path, 0, NULL, error);
}

static struct process *
find_child(struct process *parent, uint64_t pid, bool zombie_only)
{
	struct process *process;

	if (process_head == NULL)
		return NULL;
	process = process_head;
	do {
		if (process->parent == parent &&
		    (pid == PROCESS_WAIT_ANY || process->id == pid) &&
		    (!zombie_only || process->state == PROCESS_ZOMBIE))
			return process;
		process = process->next;
	} while (process != process_head);
	return NULL;
}

static bool
reap_child(struct process *parent, struct process *child,
	uint64_t status_address, uint64_t *error)
{
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	bool test_mapping_child = child->test_private_mapping_child;
	uint64_t test_mapping_physical = child->test_private_mapping_physical;
#endif

	if (status_address != 0 && !vmm_copy_to_user(parent->address_space,
	    status_address, &child->exit_status, sizeof(child->exit_status))) {
		*error = PROCESS_ERROR_EFAULT;
		return false;
	}
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (test_mapping_child &&
	    (child->state != PROCESS_ZOMBIE ||
	    child->address_space->private_mappings == NULL ||
	    child->address_space->private_mappings->base !=
	    child->test_private_mapping_base ||
	    !pmm_test_page_is_allocated(test_mapping_physical)))
		test_private_mapping_failed = true;
#endif
	remove_process(child);
#ifdef KERNEL_TEST_SHM_LIFECYCLE
	{
		uint64_t live_before = shm_test_live_pages();

		release_process(child);
		if (shm_test_live_pages() < live_before)
			test_shm_reap_releases++;
	}
#else
	release_process(child);
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (test_mapping_child) {
		if (pmm_test_page_is_allocated(test_mapping_physical))
			test_private_mapping_failed = true;
		test_private_mapping_reaps++;
	}
#endif
	*error = 0;
	return true;
}

struct interrupt_frame *
process_waitpid(struct interrupt_frame *frame, uint64_t *error)
{
	struct process *parent = current_process;
	uint64_t pid = frame->rdi;
	struct process *child;

	if (parent == NULL || (pid != PROCESS_WAIT_ANY &&
	    (int64_t)pid <= 0)) {
		*error = PROCESS_ERROR_ECHILD;
		return frame;
	}
	child = find_child(parent, pid, false);
	if (child == NULL) {
		*error = PROCESS_ERROR_ECHILD;
		return frame;
	}
	if (child->state == PROCESS_ZOMBIE) {
		uint64_t child_id = child->id;

		if (!reap_child(parent, child, frame->rsi, error))
			return frame;
		frame->rax = child_id;
		frame->rflags &= ~1ULL;
		return frame;
	}
	if (process_has_pending_signal(parent)) {
		frame->rax = PROCESS_ERROR_EINTR;
		frame->rflags |= 1U;
		parent->retry_in_progress = false;
		*error = 0;
		return frame;
	}
	parent->state = PROCESS_BLOCKED;
	parent->saved_frame = frame;
	parent->waiting_for = pid;
	parent->wait_status_address = frame->rsi;
	parent->block_reason = PROCESS_BLOCK_WAIT;
	parent->retry_pending = false;
	parent->retry_in_progress = false;
	*error = 0;
	return schedule_from(parent);
}

struct interrupt_frame *
process_sleep_ticks(struct interrupt_frame *frame, uint64_t ticks,
	uint64_t *error)
{
	uint64_t now;

	if (current_process == NULL) {
		*error = PROCESS_ERROR_ECHILD;
		return frame;
	}
	if (ticks == 0) {
		frame->rax = 0;
		frame->rflags &= ~1ULL;
		*error = 0;
		return frame;
	}
	now = timer_ticks();
	if (ticks > UINT64_MAX - now) {
		*error = PROCESS_ERROR_EINVAL;
		return frame;
	}
	if (process_has_pending_signal(current_process)) {
		frame->rax = PROCESS_ERROR_EINTR;
		frame->rflags |= 1U;
		current_process->retry_in_progress = false;
		*error = 0;
		return frame;
	}
	current_process->wake_tick = now + ticks;
	current_process->state = PROCESS_BLOCKED;
	current_process->saved_frame = frame;
	current_process->block_reason = PROCESS_BLOCK_SLEEP;
	current_process->retry_pending = false;
	current_process->retry_in_progress = false;
	frame->rax = 0;
	frame->rflags &= ~1ULL;
	*error = 0;
	return schedule_from(current_process);
}

struct interrupt_frame *
process_block_on_channel(struct interrupt_frame *frame, void *channel)
{
	if (current_process == NULL || channel == NULL) {
		frame->rax = PROCESS_ERROR_EINVAL;
		frame->rflags |= 1U;
		return frame;
	}
	if (process_has_pending_signal(current_process)) {
		frame->rax = PROCESS_ERROR_EINTR;
		frame->rflags |= 1U;
		current_process->retry_in_progress = false;
		return frame;
	}
	current_process->blocked_channel = channel;
	current_process->state = PROCESS_BLOCKED;
	current_process->saved_frame = frame;
	current_process->block_reason = PROCESS_BLOCK_CHANNEL;
	current_process->retry_pending = false;
	current_process->retry_in_progress = false;
	return schedule_from(current_process);
}

void
process_wake_channel(void *channel)
{
	struct process *process;

	if (channel == NULL || process_head == NULL)
		return;
	process = process_head;
	do {
		if (process->state == PROCESS_BLOCKED &&
		    process->blocked_channel == channel) {
			process->blocked_channel = NULL;
			process_wake_retry(process);
		}
		process = process->next;
	} while (process != process_head);
}

void
process_timer_wakeup(uint64_t ticks)
{
	struct process *process;

	if (process_head == NULL)
		return;
	process = process_head;
	do {
		if (process->state == PROCESS_BLOCKED &&
		    process->blocked_channel != NULL && pipe_blocked_ticks != UINT64_MAX)
			pipe_blocked_ticks++;
		if (process->state == PROCESS_BLOCKED && process->wake_tick != 0 &&
		    ticks >= process->wake_tick) {
			process->wake_tick = 0;
			process->blocked_channel = NULL;
			process->block_reason = PROCESS_BLOCK_NONE;
			process->state = PROCESS_READY;
		}
		process = process->next;
	} while (process != process_head);
}

struct interrupt_frame *
process_exit(struct interrupt_frame *frame, uint64_t status)
{
	struct process *process = current_process;
	struct process *parent;
	struct process *child;

	(void)frame;
	if (process == NULL)
		user_return_to_kernel(status);
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (process->test_private_mapping_child) {
		uint64_t marker = 0;
		uint64_t changed_marker = 0x4e585f4146544552ULL;

		if (status != 128U + 14U || frame == NULL || frame->vector != 14 ||
		    frame->rip != process->test_private_mapping_base ||
		    frame->error_code != 0x15U ||
		    !vmm_validate_user_write(process->address_space,
		    process->test_private_mapping_base, 1) ||
		    vmm_validate_user_exec(process->address_space,
		    process->test_private_mapping_base, 1) ||
		    !vmm_copy_from_user(process->address_space, &marker,
		    process->test_private_mapping_base + 8, sizeof(marker)) ||
		    marker != 0x4e585f4c49564531ULL ||
		    !vmm_copy_to_user(process->address_space,
		    process->test_private_mapping_base + 16, &changed_marker,
		    sizeof(changed_marker)) ||
		    !vmm_copy_from_user(process->address_space, &marker,
		    process->test_private_mapping_base + 16, sizeof(marker)) ||
		    marker != changed_marker ||
		    !pmm_test_page_is_allocated(
		    process->test_private_mapping_physical))
			test_private_mapping_failed = true;
		else
			test_private_mapping_faults++;
	}
#endif
	process->exit_status = status & 0xffU;
	process->state = PROCESS_ZOMBIE;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (process->test_private_mapping_child) {
		if (process->address_space->private_mappings == NULL ||
		    process->address_space->private_mappings->base !=
		    process->test_private_mapping_base ||
		    !pmm_test_page_is_allocated(
		    process->test_private_mapping_physical))
			test_private_mapping_failed = true;
		else
			test_private_mapping_zombies++;
	}
#endif
	fd_table_destroy(&process->descriptors);
	parent = process->parent;
	if (process_head != NULL) {
		child = process_head;
		do {
			if (child->parent == process)
				child->parent = parent;
			child = child->next;
		} while (child != process_head);
	}
	if (parent != NULL && parent->state == PROCESS_BLOCKED &&
	    (parent->waiting_for == PROCESS_WAIT_ANY ||
	    parent->waiting_for == process->id)) {
		process_wake_retry(parent);
	}
	return schedule_from(process);
}

struct interrupt_frame *
process_user_exception(struct interrupt_frame *frame)
{
	if (current_process == NULL) {
#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
		if (test_user_fault_capture_armed) {
			test_user_fault_vector = frame->vector;
			test_user_fault_error = frame->error_code;
			test_user_fault_capture_valid = true;
			test_user_fault_capture_armed = false;
		}
#endif
		user_return_to_kernel(0x100U + frame->vector);
	}
	return process_exit(frame, 128U + frame->vector);
}
