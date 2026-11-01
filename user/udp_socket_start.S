.text
.global _start
.type _start, @function
_start:
	xorq %rdi, %rdi
	xorq %rsi, %rsi
	xorq %rdx, %rdx
	andq $-16, %rsp
	call user_main
	movq %rax, %rdi
	movq $1, %rax
	int $128
	ud2
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
