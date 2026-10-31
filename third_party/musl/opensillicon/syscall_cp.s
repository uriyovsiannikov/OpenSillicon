/* Cancellation-point syscalls use the same native adapter as __syscallN. */
.text
.global __cp_begin
.hidden __cp_begin
.global __cp_end
.hidden __cp_end
.global __cp_cancel
.hidden __cp_cancel
.hidden __cancel
.global __syscall_cp_asm
.hidden __syscall_cp_asm
.type __syscall_cp_asm,@function
__syscall_cp_asm:

__cp_begin:
	mov (%rdi),%eax
	test %eax,%eax
	jnz __cp_cancel

	/* Save syscall argument six before rearranging registers. */
	mov 16(%rsp),%rax
	sub $8,%rsp
	mov %rax,(%rsp)
	mov %rsi,%rdi
	mov %rdx,%rsi
	mov %rcx,%rdx
	mov %r8,%rcx
	mov %r9,%r8
	mov 16(%rsp),%r9
	call __opensillicon_syscall
	add $8,%rsp
__cp_end:
	ret
__cp_cancel:
	jmp __cancel
