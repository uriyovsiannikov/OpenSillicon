.section .text
.global _start
_start:
	movq $1, %rdi
	movq $18, %rax
	int $128
	jc fail
	movq %rax, %r14
	movabsq $0x53484d5f4c495631, %r15
	movq %r15, (%r14)
	movq $4, %rax
	int $128
	jc fail
	testq %rax, %rax
	jz fail
	movq %rax, %r12
	leaq -1(%rsp), %rsi
	movq %r12, %rdi
	movq $6, %rax
	int $128
	jc fail
	cmpq %r12, %rax
	jne fail
	cmpb $142, -1(%rsp)
	jne fail
	cmpq %r15, (%r14)
	jne fail
	movq %r14, %rdi
	movq $19, %rax
	int $128
	jc fail
	testq %rax, %rax
	jne fail
	xorq %rdi, %rdi
	movq $1, %rax
	int $128
	ud2
fail:
	movq $1, %rdi
	movq $1, %rax
	int $128
	ud2

.section .note.GNU-stack,"",@progbits
