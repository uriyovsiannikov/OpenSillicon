.text
.global gdt_load
.type gdt_load, @function
gdt_load:
	lgdt (%rdi)
	pushq $0x08
	leaq 1f(%rip), %rax
	pushq %rax
	lretq
1:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	xorw %ax, %ax
	movw %ax, %fs
	movw %ax, %gs
	ret
