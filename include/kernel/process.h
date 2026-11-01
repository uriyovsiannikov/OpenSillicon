#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "kernel/fd.h"
#include "kernel/signal_state.h"

#define PROCESS_EXEC_ARG_MAX 16U
#define PROCESS_EXEC_ARG_BYTES_MAX 2048U
#define PROCESS_SNAPSHOT_MAX 128U

struct process_snapshot_entry {
	uint64_t pid;
	uint64_t parent_pid;
	uint32_t state;
	uint8_t exit_status;
	uint8_t reserved[3];
};

void process_system_init(void);
uint64_t process_run_elf(const void *image, size_t image_size);
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
uint64_t process_test_private_mapping_lifecycle(const void *image,
	size_t image_size);
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
void process_test_arm_user_fault_capture(void);
bool process_test_user_fault_capture(uint64_t *vector, uint64_t *error_code);
#endif
uint64_t process_run_path(const char *path, bool startup_stack);
struct fd_table *process_current_fd_table(void);
uint64_t process_current_pid(void);
int process_snapshot(struct process_snapshot_entry *entries, size_t capacity);
void process_syscall_retry_begin(void);
void process_capture_user_fs_base(uint64_t base);
void process_restore_user_fs_base(void);
uint64_t process_set_fs_base(uint64_t base);
uint64_t process_kill(uint64_t pid, unsigned int signo);
struct interrupt_frame *process_sigaction(struct interrupt_frame *frame,
	unsigned int signo, uint64_t new_action, uint64_t old_action,
	uint64_t *error);
uint64_t process_user_preemptions(void);
uint64_t process_pipe_blocked_ticks(void);
#ifdef KERNEL_TEST_SHM_LIFECYCLE
uint64_t process_test_shm_reap_releases(void);
uint64_t process_test_shm_exec_releases(void);
#endif
struct interrupt_frame *process_timer_preempt(
	struct interrupt_frame *frame);
struct interrupt_frame *process_fork(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_exec(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_exec_path(struct interrupt_frame *frame,
	const char *path, uint64_t *error);
struct interrupt_frame *process_exec_path_argv(struct interrupt_frame *frame,
	const char *path, size_t argc, const char *const argv[], uint64_t *error);
struct interrupt_frame *process_sigreturn(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_prepare_user_return(
	struct interrupt_frame *frame);
struct interrupt_frame *process_waitpid(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_sleep_ticks(struct interrupt_frame *frame,
	uint64_t ticks, uint64_t *error);
struct interrupt_frame *process_nanosleep(struct interrupt_frame *frame,
	uint64_t ticks, int64_t seconds, int64_t nanoseconds,
	uint64_t remaining_address, uint64_t *error);
struct interrupt_frame *process_block_on_channel(
	struct interrupt_frame *frame, void *channel);
void process_wake_channel(void *channel);
void process_timer_wakeup(uint64_t ticks);
struct interrupt_frame *process_exit(struct interrupt_frame *frame,
	uint64_t status);
struct interrupt_frame *process_user_exception(
	struct interrupt_frame *frame);

#endif
