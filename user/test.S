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
