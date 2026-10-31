#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "kernel/fd.h"

void process_system_init(void);
uint64_t process_run_elf(const void *image, size_t image_size);
uint64_t process_run_path(const char *path, bool startup_stack);
struct fd_table *process_current_fd_table(void);
uint64_t process_current_pid(void);
uint64_t process_user_preemptions(void);
uint64_t process_pipe_blocked_ticks(void);
struct interrupt_frame *process_timer_preempt(
	struct interrupt_frame *frame);
struct interrupt_frame *process_fork(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_exec(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_exec_path(struct interrupt_frame *frame,
	const char *path, uint64_t *error);
struct interrupt_frame *process_waitpid(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_sleep_ticks(struct interrupt_frame *frame,
	uint64_t ticks, uint64_t *error);
struct interrupt_frame *process_block_on_channel(
	struct interrupt_frame *frame, void *channel);
void process_wake_channel(void *channel);
void process_timer_wakeup(uint64_t ticks);
struct interrupt_frame *process_exit(struct interrupt_frame *frame,
	uint64_t status);
struct interrupt_frame *process_user_exception(
	struct interrupt_frame *frame);

#endif
