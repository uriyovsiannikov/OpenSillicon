.section .text
.global _start
_start:
	/* stat reads through the initramfs mount. */
	leaq motd_path(%rip), %rdi
	leaq status_buffer(%rip), %rsi
	movq $10, %rax
	int $128
	jc fail
	cmpq $45, status_buffer(%rip)
	jne fail
	cmpl $1, status_buffer+12(%rip)
	jne fail

	/* A bad user destination must not advance the open-file offset. */
	leaq motd_path(%rip), %rdi
	xorl %esi, %esi
	movq $7, %rax
	int $128
	jc fail
	movq %rax, %r12
	movq %r12, %rdi
	movq $0x400000000000, %rsi
	movq $1, %rdx
	movq $9, %rax
	int $128
	jnc fail
	cmpq $14, %rax
	jne fail
	movq %r12, %rdi
	leaq read_buffer(%rip), %rsi
	movq $motd_end-motd_text, %rdx
	movq $9, %rax
	int $128
	jc fail
	cmpq $motd_end-motd_text, %rax
	jne fail
	leaq read_buffer(%rip), %rsi
	leaq motd_text(%rip), %rdi
	movq $motd_end-motd_text, %rcx
	repe cmpsb
	jne fail
	movq %r12, %rdi
	movq $8, %rax
	int $128
	jc fail

	/* The archive mount rejects write opens. */
	leaq motd_path(%rip), %rdi
	movq $1, %rsi
	movq $7, %rax
	int $128
	jnc fail
	cmpq $30, %rax
	jne fail

	/* Directory entries advance one per readdir call. */
	leaq etc_path(%rip), %rdi
	movq $0x10000, %rsi
	movq $7, %rax
	int $128
	jc fail
	movq %rax, %r12
	movq %r12, %rdi
	leaq dirent_buffer(%rip), %rsi
	movq $11, %rax
	int $128
	jc fail
	cmpq $1, %rax
	jne fail
	cmpb $'m', dirent_buffer+24(%rip)
	jne fail
	cmpb $'o', dirent_buffer+25(%rip)
	jne fail
	cmpb $'t', dirent_buffer+26(%rip)
	jne fail
	cmpb $'d', dirent_buffer+27(%rip)
	jne fail
	movq %r12, %rdi
	leaq dirent_buffer(%rip), %rsi
	movq $11, %rax
	int $128
	jc fail
	testq %rax, %rax
	jne fail
	movq %r12, %rdi
	movq $8, %rax
	int $128
	jc fail

	/* Create, write, close, reopen, and read a root filesystem file. */
	leaq ramfs_path(%rip), %rdi
	movq $0x242, %rsi
	movq $0644, %rdx
	movq $7, %rax
	int $128
	jc fail
	movq %rax, %r12
	movq %r12, %rdi
	leaq ramfs_text(%rip), %rsi
	movq $ramfs_end-ramfs_text, %rdx
	movq $2, %rax
	int $128
	jc fail
	cmpq $ramfs_end-ramfs_text, %rax
	jne fail
	movq %r12, %rdi
	movq $8, %rax
	int $128
	jc fail
	leaq ramfs_path(%rip), %rdi
	xorl %esi, %esi
	movq $7, %rax
	int $128
	jc fail
	movq %rax, %r12
	movq %r12, %rdi
	leaq read_buffer(%rip), %rsi
	movq $ramfs_end-ramfs_text, %rdx
	movq $9, %rax
	int $128
	jc fail
	cmpq $ramfs_end-ramfs_text, %rax
	jne fail
	leaq read_buffer(%rip), %rsi
	leaq ramfs_text(%rip), %rdi
	movq $ramfs_end-ramfs_text, %rcx
	repe cmpsb
	jne fail
	movq %r12, %rdi
	movq $8, %rax
	int $128
	jc fail

	/* Missing files return ENOENT and invalid paths return EFAULT. */
	leaq missing_path(%rip), %rdi
	xorl %esi, %esi
	movq $7, %rax
	int $128
	jnc fail
	cmpq $2, %rax
	jne fail
	movq $0x400000000000, %rdi
	xorl %esi, %esi
	movq $7, %rax
	int $128
	jnc fail
	cmpq $14, %rax
	jne fail

	/* exec_path failures must leave this process running. */
	leaq missing_path(%rip), %rdi
	movq $12, %rax
	int $128
	jnc fail
	cmpq $2, %rax
	jne fail
	leaq relative_path(%rip), %rdi
	movq $12, %rax
	int $128
	jnc fail
	cmpq $22, %rax
	jne fail
	leaq non_elf_path(%rip), %rdi
	movq $12, %rax
	int $128
	jnc fail
	cmpq $8, %rax
	jne fail
	leaq motd_path(%rip), %rdi
	movq $12, %rax
	int $128
	jnc fail
	cmpq $13, %rax
	jne fail
	movq $0x400000000000, %rdi
	movq $12, %rax
	int $128
	jnc fail
	cmpq $14, %rax
	jne fail

	/* Successful syscall 12 replaces this process image. */
	leaq exec_path(%rip), %rdi
	movq $12, %rax
	int $128
	ud2
	ud2

fail:
	movq $1, %rdi
	movq $1, %rax
	int $128
	ud2

.section .rodata
motd_path:
	.asciz "/init/etc/motd"
etc_path:
	.asciz "/init/etc"
ramfs_path:
	.asciz "/tmp/phase8.txt"
missing_path:
	.asciz "/init/etc/missing"
relative_path:
	.asciz "init/bin/exec_path_child"
non_elf_path:
	.asciz "/init/bin/not_elf"
exec_path:
	.asciz "/init/bin/exec_path_child"
motd_text:
	.ascii "OpenSillicon initramfs is mounted read-only.\n"
motd_end:
ramfs_text:
	.ascii "RAMFS read/write passed"
ramfs_end:

.section .bss
.align 16
status_buffer:
	.skip 16
read_buffer:
	.skip 128
dirent_buffer:
	.skip 280

.section .note.GNU-stack,"",@progbits
