.section .text
.global _start
_start:
	movq $0, %rax
	int $128
	jc fail1
	cmpq $1, %rax
	jne fail1

	movq $3, %rax
	int $128
	jc fail2
	cmpq $1, %rax
	jne fail2

	movq $2, %rax
	movq $1, %rdi
	leaq message(%rip), %rsi
	movq $message_end-message, %rdx
	int $128
	jc fail3
	cmpq $message_end-message, %rax
	jne fail3

	movq $2, %rax
	movq $9, %rdi
	leaq message(%rip), %rsi
	movq $1, %rdx
	int $128
	jnc fail4
	cmpq $9, %rax
	jne fail4

	movq $2, %rax
	movq $1, %rdi
	movq $0x400000000000, %rsi
	movq $1, %rdx
	int $128
	jnc fail4
	cmpq $14, %rax
	jne fail5

	movq $99, %rax
	int $128
	jnc fail5
	cmpq $78, %rax
	jne fail6

	cmpq $1, 0x401000
	je shm_success_phase

	/* The kernel placed a private page on the second page of the first
	 * candidate mapping. Failure after page one must roll back without moving
	 * the cursor. */
	movq $8192, %rdi
	movq $18, %rax
	int $128
	jnc fail7
	cmpq $12, %rax
	jne fail7
	movq $8192, %rdi
	movq $18, %rax
	int $128
	jnc fail7
	cmpq $12, %rax
	jne fail7
	movq $4096, %rdi
	movq $18, %rax
	int $128
	jc fail7
	movabsq $0x0000700000001000, %rcx
	cmpq %rcx, %rax
	jne fail7
	xorq %rdi, %rdi
	movq $1, %rax
	int $128
	ud2

shm_success_phase:
	xorq %rdi, %rdi
	movq $18, %rax
	int $128
	jnc fail8
	cmpq $22, %rax
	jne fail8
	movabsq $0xffffffffffffffff, %rdi
	movq $18, %rax
	int $128
	jnc fail8
	cmpq $22, %rax
	jne fail8
	movabsq $0x0000100000000000, %rdi
	movq $18, %rax
	int $128
	jnc fail8
	cmpq $12, %rax
	jne fail8
	movq $4096, %rdi
	movq $18, %rax
	int $128
	jc fail9
	movq %rax, %r12
	movabsq $0x0000700000003000, %rcx
	cmpq %rcx, %r12
	jne fail9
	movabsq $0x53484d5f54455354, %rcx
	movq %rcx, (%r12)
	cmpq %rcx, (%r12)
	jne fail10
	xorq %rdi, %rdi
	movq $19, %rax
	int $128
	jnc fail11
	cmpq $22, %rax
	jne fail11
	leaq 1(%r12), %rdi
	movq $19, %rax
	int $128
	jnc fail11
	cmpq $22, %rax
	jne fail11
	leaq 4096(%r12), %rdi
	movq $19, %rax
	int $128
	jnc fail11
	cmpq $22, %rax
	jne fail11
	movq %r12, %rdi
	movq $19, %rax
	int $128
	jc fail12
	testq %rax, %rax
	jne fail12
	movq %r12, %rdi
	movq $19, %rax
	int $128
	jnc fail12
	cmpq $22, %rax
	jne fail12
	movq $4097, %rdi
	movq $18, %rax
	int $128
	jc fail13
	movq %rax, %r12
	movabsq $0x0000700000005000, %rcx
	cmpq %rcx, %r12
	jne fail13
	movabsq $0x123456789abcdef0, %rcx
	movq %rcx, 4096(%r12)
	cmpq %rcx, 4096(%r12)
	jne fail14
	movq %r12, %rdi
	movq $19, %rax
	int $128
	jc fail15
	testq %rax, %rax
	jne fail15

	xorq %rdi, %rdi
	movq $1, %rax
	int $128
	ud2
fail1:
	movq $1, %rdi
	jmp exit
fail2:
	movq $2, %rdi
	jmp exit
fail3:
	movq $3, %rdi
	jmp exit
fail4:
	movq $4, %rdi
	jmp exit
fail5:
	movq $5, %rdi
	jmp exit
fail6:
	movq $6, %rdi
	jmp exit
fail7:
	movq $7, %rdi
	jmp exit
fail8:
	movq $8, %rdi
	jmp exit
fail9:
	movq $9, %rdi
	jmp exit
fail10:
	movq $10, %rdi
	jmp exit
fail11:
	movq $11, %rdi
	jmp exit
fail12:
	movq $12, %rdi
	jmp exit
fail13:
	movq $13, %rdi
	jmp exit
fail14:
	movq $14, %rdi
	jmp exit
fail15:
	movq $15, %rdi
exit:
	movq $1, %rax
	int $128
	ud2

	.org 0x400
.global user_fault_probe
user_fault_probe:
	movq 0xffffffff80000000, %rax
	xorq %rdi, %rdi
	movq $1, %rax
	int $128
	ud2

message:
	.ascii "OpenSillicon: Ring 3 syscall ABI passed\n"
message_end:

.section .note.GNU-stack,"",@progbits
