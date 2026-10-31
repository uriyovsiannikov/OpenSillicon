.section .text
.global _start
_start:
	movq $0, %rax
	int $128
	jc fail
	cmpq $1, %rax
	jne fail
	leaq exec_child_image(%rip), %rdi
	movq $64, %rsi
	movq $5, %rax
	int $128
	jnc fail
	cmpq $22, %rax
	jne fail
	movq $4, %rax
	int $128
	jc fail
	testq %rax, %rax
	jz child
	movq %rax, %r12
	movq $500000000, %rcx
spin:
	loop spin
	movq $6, %rax
	movq %r12, %rdi
	movq $0x400000000000, %rsi
	int $128
	jnc fail
	cmpq $14, %rax
	jne fail
	movq $6, %rax
	movq %r12, %rdi
	leaq -1(%rsp), %rsi
	int $128
	jc fail
	cmpq %r12, %rax
	jne fail
	cmpb $42, -1(%rsp)
	jne fail
	movq $4, %rax
	int $128
	jc fail
	testq %rax, %rax
	jz child_wait_any
	movq %rax, %r13
	movq $6, %rax
	movq $-1, %rdi
	leaq -1(%rsp), %rsi
	int $128
	jc fail
	cmpq %r13, %rax
	jne fail
	cmpb $7, -1(%rsp)
	jne fail
	movq $2, %rax
	movq $1, %rdi
	leaq message(%rip), %rsi
	movq $message_end-message, %rdx
	int $128
	jc fail
	cmpq $message_end-message, %rax
	jne fail
	xorq %rdi, %rdi
	movq $1, %rax
	int $128
	ud2
child:
	leaq exec_child_image(%rip), %rdi
	movq $exec_child_image_end-exec_child_image, %rsi
	movq $5, %rax
	int $128
	jc fail
	jmp fail

child_wait_any:
	movq $7, %rdi
	movq $1, %rax
	int $128
	ud2
fail:
	movq $1, %rdi
	movq $1, %rax
	int $128
	ud2

message:
	.ascii "OpenSillicon: static ELF64 fork, exec, and wait passed\n"
message_end:

.section .rodata
.align 16
exec_child_image:
	.incbin "build/exec_child.elf"
exec_child_image_end:

.section .note.GNU-stack,"",@progbits
