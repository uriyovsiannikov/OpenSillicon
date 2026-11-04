.text
.global user_context_roundtrip
.type user_context_roundtrip,@function
user_context_roundtrip:
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	movq %rdi, %r12
	movq %rsp, %rdi
	subq $8, %rsp
	call arch_user_context_save
	addq $8, %rsp
	movq %r12, %rdi
	subq $8, %rsp
	call user_return_to_kernel
	ud2
.size user_context_roundtrip,.-user_context_roundtrip
.section .note.GNU-stack,"",@progbits
