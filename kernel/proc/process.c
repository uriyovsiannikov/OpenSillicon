#include <stdint.h>

#include "arch/x86_64/gdt.h"
#include "arch/x86_64/user.h"
#include "kernel/elf.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/pmm.h"
#include "kernel/timer.h"
#include "kernel/process.h"
#include "kernel/vmm.h"
#include "kernel/vfs.h"

#define PROCESS_KERNEL_STACK_SIZE 65536U
#define PROCESS_STACK_TOP (0x00007ffffffff000ULL - 0x1000ULL)
#define PROCESS_STACK_PAGE (PROCESS_STACK_TOP - 4096ULL)
#define PROCESS_WAIT_ANY UINT64_MAX
#define PROCESS_ERROR_ECHILD 10U
#define PROCESS_ERROR_EFAULT 14U
#define PROCESS_ERROR_EAGAIN 11U
#define PROCESS_ERROR_ENOMEM 12U
#define PROCESS_ERROR_EINVAL 22U
#define PROCESS_ERROR_EACCES 13U
#define PROCESS_ERROR_ENOEXEC 8U
#define PROCESS_ERROR_EIO 5U
#define PROCESS_ERROR_EINVAL 22U

enum process_state {
	PROCESS_READY,
	PROCESS_RUNNING,
	PROCESS_BLOCKED,
	PROCESS_ZOMBIE
};

struct process {
	uint64_t id;
	enum process_state state;
	uint8_t exit_status;
	uint64_t waiting_for;
	uint64_t wait_status_address;
	uint64_t wake_tick;
	void *blocked_channel;
	struct process *parent;
	struct process *previous;
	struct process *next;
	struct address_space *address_space;
	struct elf_loaded_image *loaded_image;
	void *kernel_stack;
	struct interrupt_frame *saved_frame;
	uint64_t initial_user_stack;
	struct fd_table descriptors;
};

static struct process *process_head;
static struct process *current_process;
static uint64_t next_process_id = 1;
static uint64_t user_preemptions;
static uint64_t pipe_blocked_ticks;
static bool initialized;

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

	for (;;) {
		next = find_next_ready(after);
		if (next != NULL)
			break;
		if (current_process != NULL && current_process->parent == NULL &&
		    current_process->state == PROCESS_ZOMBIE) {
			uint64_t status = current_process->exit_status;

			current_process = NULL;
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
map_process_stack(struct elf_loaded_image *image)
{
	uint64_t physical;
	volatile uint8_t *stack;
	uint64_t i;

	if (!pmm_alloc_page(&physical))
		return false;
	if (vmm_hhdm_offset() > UINT64_MAX - physical ||
	    4095 > UINT64_MAX - (vmm_hhdm_offset() + physical)) {
		(void)pmm_free_page(physical);
		return false;
	}
	stack = (volatile uint8_t *)(uintptr_t)(vmm_hhdm_offset() + physical);
	for (i = 0; i < 4096; i++)
		stack[i] = 0;
	if (!vmm_map_page(&image->address_space, PROCESS_STACK_PAGE, physical,
	    VMM_USER | VMM_WRITE)) {
		(void)pmm_free_page(physical);
		return false;
	}
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
process_user_preemptions(void)
{
	return user_preemptions;
}

uint64_t
process_pipe_blocked_ticks(void)
{
	return pipe_blocked_ticks;
}

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
	root->parent = NULL;
	root->address_space = &root->loaded_image->address_space;
	root->saved_frame = NULL;
	/* The zeroed stack page supplies argc, argv[0], and envp for init. */
	root->initial_user_stack = startup_stack ? PROCESS_STACK_TOP - 3 *
	    sizeof(uint64_t) : PROCESS_STACK_TOP;
	fd_table_init_standard(&root->descriptors);
	if (!map_process_stack(root->loaded_image)) {
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
	status = user_enter(root->loaded_image->entry, root->initial_user_stack);
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
	result = process_run_elf_internal(image, (size_t)status.size,
	    startup_stack);
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
	child = kheap_alloc(sizeof(*child));
	if (child == NULL) {
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
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
	child->parent = current_process;
	child->loaded_image = NULL;
	child->initial_user_stack = current_process->initial_user_stack;
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
	child->saved_frame = child_frame;
	insert_process(child);
	frame->rax = child->id;
	frame->rflags &= ~1ULL;
	*error = 0;
	return frame;
}

static struct interrupt_frame *
replace_process_image(struct interrupt_frame *frame, const void *image,
	size_t image_size, uint64_t invalid_elf_error, uint64_t *error)
{
	struct process *process = current_process;
	struct elf_loaded_image *replacement;
	uint64_t new_entry;
	uint64_t stack_physical;
	volatile uint8_t *stack;
	uint64_t i;
	struct elf_loaded_image *old_image;
	struct address_space *old_space;
	struct interrupt_frame *new_frame;
	uintptr_t stack_top;

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
	if (!pmm_alloc_page(&stack_physical)) {
		release_loaded_image(replacement);
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	if (vmm_hhdm_offset() > UINT64_MAX - stack_physical ||
	    4095 > UINT64_MAX - (vmm_hhdm_offset() + stack_physical)) {
		(void)pmm_free_page(stack_physical);
		release_loaded_image(replacement);
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	stack = (volatile uint8_t *)(uintptr_t)(vmm_hhdm_offset() + stack_physical);
	for (i = 0; i < 4096; i++)
		stack[i] = 0;
	if (!vmm_map_page(&replacement->address_space, PROCESS_STACK_PAGE,
	    stack_physical, VMM_USER | VMM_WRITE)) {
		(void)pmm_free_page(stack_physical);
		release_loaded_image(replacement);
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	new_entry = replacement->entry;
	old_image = process->loaded_image;
	old_space = process->address_space;
	if (!address_space_activate(&replacement->address_space)) {
		release_loaded_image(replacement);
		*error = PROCESS_ERROR_ENOMEM;
		return frame;
	}
	if (old_image != NULL) {
		release_loaded_image(old_image);
	} else if (!address_space_destroy_with_pages(old_space) ||
	    !kheap_free(old_space)) {
		klog("OpenSillicon panic: exec could not release old address space\n");
		kernel_halt();
	}
	process->loaded_image = replacement;
	process->address_space = &replacement->address_space;
	process->initial_user_stack = PROCESS_STACK_TOP;
	stack_top = process_kernel_stack_top(process);
	new_frame = (struct interrupt_frame *)(stack_top - sizeof(*new_frame));
	initialize_user_frame(new_frame, new_entry, PROCESS_STACK_TOP);
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
	    PROCESS_ERROR_EINVAL, error);
	(void)kheap_free(image_copy);
	return result;
}

struct interrupt_frame *
process_exec_path(struct interrupt_frame *frame, const char *path,
	uint64_t *error)
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
	    PROCESS_ERROR_ENOEXEC, error);
	(void)kheap_free(image);
	return result;
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
	if (status_address != 0 && !vmm_copy_to_user(parent->address_space,
	    status_address, &child->exit_status, sizeof(child->exit_status))) {
		*error = PROCESS_ERROR_EFAULT;
		return false;
	}
	remove_process(child);
	release_process(child);
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
	parent->state = PROCESS_BLOCKED;
	parent->saved_frame = frame;
	parent->waiting_for = pid;
	parent->wait_status_address = frame->rsi;
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
	current_process->wake_tick = now + ticks;
	current_process->state = PROCESS_BLOCKED;
	current_process->saved_frame = frame;
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
	current_process->blocked_channel = channel;
	current_process->state = PROCESS_BLOCKED;
	current_process->saved_frame = frame;
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
			if (process->saved_frame->rip >= 2)
				process->saved_frame->rip -= 2;
			process->state = PROCESS_READY;
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
	process->exit_status = status & 0xffU;
	process->state = PROCESS_ZOMBIE;
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
		parent->saved_frame->rip -= 2;
		parent->waiting_for = 0;
		parent->wait_status_address = 0;
		parent->state = PROCESS_READY;
	}
	return schedule_from(process);
}

struct interrupt_frame *
process_user_exception(struct interrupt_frame *frame)
{
	if (current_process == NULL)
		user_return_to_kernel(0x100U + frame->vector);
	return process_exit(frame, 128U + frame->vector);
}
