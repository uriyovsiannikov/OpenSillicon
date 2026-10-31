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
	movq %rsp, user_saved_kernel_rsp(%rip)
	pushq $0x1b
	pushq %r13
	pushq $0x202
	pushq $0x23
	pushq %r12
	iretq
.size user_enter, .-user_enter

.global user_return_to_kernel
.type user_return_to_kernel, @function
user_return_to_kernel:
	movq %rdi, user_saved_exit_status(%rip)
	movq user_saved_kernel_rsp(%rip), %rsp
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbx
	popq %rbp
	movq user_saved_exit_status(%rip), %rax
	ret
.size user_return_to_kernel, .-user_return_to_kernel

.section .note.GNU-stack,"",@progbits
