.text
.globl _setjmp
.type _setjmp,@function
_setjmp:
	movq %rbx, 0(%rdi)
	movq %rbp, 8(%rdi)
	movq %r12, 16(%rdi)
	movq %r13, 24(%rdi)
	movq %r14, 32(%rdi)
	movq %r15, 40(%rdi)
	leaq 8(%rsp), %rax
	movq %rax, 48(%rdi)
	movq (%rsp), %rax
	movq %rax, 56(%rdi)
	xorl %eax, %eax
	ret
.size _setjmp, .-_setjmp

.globl __sigsetjmp
.type __sigsetjmp,@function
__sigsetjmp:
	jmp _setjmp
.size __sigsetjmp, .-__sigsetjmp

.globl longjmp
.type longjmp,@function
longjmp:
	movq %rdi, %rdx
	movl %esi, %eax
	testl %eax, %eax
	jne 1f
	movl $1, %eax
1:
	movq 56(%rdx), %rcx
	movq 48(%rdx), %rsp
	movq 0(%rdx), %rbx
	movq 8(%rdx), %rbp
	movq 16(%rdx), %r12
	movq 24(%rdx), %r13
	movq 32(%rdx), %r14
	movq 40(%rdx), %r15
	jmp *%rcx
.size longjmp, .-longjmp

.globl siglongjmp
.type siglongjmp,@function
siglongjmp:
	jmp longjmp
.size siglongjmp, .-siglongjmp
