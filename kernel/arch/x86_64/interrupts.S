.text
.macro ISR_NOERR number
	.global isr\number
isr\number:
	pushq $0
	pushq $\number
	jmp isr_common
.endm

.macro ISR_ERR number
	.global isr\number
isr\number:
	pushq $\number
	jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

.altmacro
.set vector, 32
.rept 224
	ISR_NOERR %vector
	.set vector, vector + 1
.endr
.noaltmacro

isr_common:
	cld
	pushq %rax
	pushq %rbx
	pushq %rcx
	pushq %rdx
	pushq %rbp
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	/* CS is always at byte offset 144 from this register frame. */
	movq %rsp, %rbx
	testb $3, 144(%rsp)
	jz 1f
	/* Capture user FS before entering C, then establish kernel FS == 0. */
	movl $0xc0000100, %ecx
	rdmsr
	shlq $32, %rdx
	orq %rdx, %rax
	movq %rax, %rdi
	xorl %eax, %eax
	xorl %edx, %edx
	movl $0xc0000100, %ecx
	wrmsr
	andq $-16, %rsp
	call process_capture_user_fs_base
	movq %rbx, %rsp
1:
	movq %rsp, %rdi
	movq %rsp, %rbx
	andq $-16, %rsp
	call interrupt_dispatch
	movq %rax, %rsp
	testb $3, 144(%rsp)
	jz 2f
	/* Restore only after dispatch chose the process that will return. */
	movq %rsp, %rbx
	andq $-16, %rsp
	call process_restore_user_fs_base
	movq %rbx, %rsp
2:
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $16, %rsp
	iretq

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
	.quad isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
	.quad isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
	.quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
	.quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
.altmacro
.macro ISR_TABLE_ENTRY number
	.quad isr\number
.endm
.set vector, 32
.rept 224
	ISR_TABLE_ENTRY %vector
	.set vector, vector + 1
.endr
.noaltmacro
