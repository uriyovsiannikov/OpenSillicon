.text
.global user_enter
.type user_enter, @function
user_enter:
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	movq %rdi, %r12
	movq %rsi, %r13
	movq %rsp, %rdi
	/* Six saved registers leave RSP == 8 mod 16 at function entry. */
	subq $8, %rsp
	call arch_user_context_save
	addq $8, %rsp
	pushq $0x1b
	pushq %r13
	pushq $0x202
	pushq $0x23
	pushq %r12
	xorq %rax, %rax
	xorq %rbx, %rbx
	xorq %rcx, %rcx
	xorq %rdx, %rdx
	xorq %rsi, %rsi
	xorq %rdi, %rdi
	xorq %rbp, %rbp
	xorq %r8, %r8
	xorq %r9, %r9
	xorq %r10, %r10
	xorq %r11, %r11
	xorq %r12, %r12
	xorq %r13, %r13
	xorq %r14, %r14
	xorq %r15, %r15
	iretq
.size user_enter, .-user_enter

.global user_return_to_kernel
.type user_return_to_kernel, @function
user_return_to_kernel:
	/* The caller enters with RSP == 8 mod 16. The two-word return value
	 * supplies this CPU's saved RSP in RAX and exit status in RDX. */
	subq $8, %rsp
	call arch_user_context_return
	movq %rax, %rsp
	movq %rdx, %rax
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbx
	popq %rbp
	ret
.size user_return_to_kernel, .-user_return_to_kernel

.section .note.GNU-stack,"",@progbits
