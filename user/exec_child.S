.section .text
.global _start
_start:
	movq $2, %rax
	movq $1, %rdi
	leaq message(%rip), %rsi
	movq $message_end-message, %rdx
	int $128
	jc fail
	cmpq $message_end-message, %rax
	jne fail
	movq $42, %rdi
	movq $1, %rax
	int $128
	ud2
fail:
	movq $43, %rdi
	movq $1, %rax
	int $128
	ud2

message:
	.ascii "OpenSillicon: fork inherited serial descriptor\n"
message_end:

.section .note.GNU-stack,"",@progbits
