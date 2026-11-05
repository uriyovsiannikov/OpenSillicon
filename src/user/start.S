.text
.global _start
.type _start, @function
_start:
	orq %rax, %rax
	jne bad_start_state
	orq %rbx, %rbx
	jne bad_start_state
	orq %rcx, %rcx
	jne bad_start_state
	orq %rdx, %rdx
	jne bad_start_state
	orq %rsi, %rsi
	jne bad_start_state
	orq %rdi, %rdi
	jne bad_start_state
	orq %rbp, %rbp
	jne bad_start_state
	orq %r8, %r8
	jne bad_start_state
	orq %r9, %r9
	jne bad_start_state
	orq %r10, %r10
	jne bad_start_state
	orq %r11, %r11
	jne bad_start_state
	orq %r12, %r12
	jne bad_start_state
	orq %r13, %r13
	jne bad_start_state
	orq %r14, %r14
	jne bad_start_state
	orq %r15, %r15
	jne bad_start_state
	xorq %rbp, %rbp
	movq (%rsp), %rdi
	leaq 8(%rsp), %rsi
	movq %rsp, %rdx
	andq $-16, %rsp
	call user_main
	movq %rax, %rdi
	movq $1, %rax
	int $128
	ud2
bad_start_state:
	movq $1, %rdi
	movq $1, %rax
	int $128
	ud2
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
