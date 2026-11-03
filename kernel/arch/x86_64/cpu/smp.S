.text
.global smp_ap_entry_trampoline
.type smp_ap_entry_trampoline,@function
smp_ap_entry_trampoline:
 cli
 cld
 lidt smp_ap_emergency_idtr(%rip)
	/* Limine jumps here; normalize RSP to the SysV function-entry ABI. */
	andq $-16,%rsp
	subq $8,%rsp
	movq $0,(%rsp)
 jmp smp_ap_entry
.size smp_ap_entry_trampoline,.-smp_ap_entry_trampoline

.global smp_ap_exception_halt
.type smp_ap_exception_halt,@function
smp_ap_exception_halt:
 cli
1: hlt
 jmp 1b
.size smp_ap_exception_halt,.-smp_ap_exception_halt

.global smp_ap_tlb_shootdown_entry
.type smp_ap_tlb_shootdown_entry,@function
smp_ap_tlb_shootdown_entry:
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
 movq %rsp,%rbx
 andq $-16,%rsp
 call smp_ap_tlb_shootdown_handler
 movq %rbx,%rsp
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
 iretq
.size smp_ap_tlb_shootdown_entry,.-smp_ap_tlb_shootdown_entry

.global smp_ap_idle_timer_entry
.type smp_ap_idle_timer_entry,@function
smp_ap_idle_timer_entry:
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
 movq %rsp,%rbx
 andq $-16,%rsp
 call smp_ap_idle_timer_handler
 movq %rbx,%rsp
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
 iretq
.size smp_ap_idle_timer_entry,.-smp_ap_idle_timer_entry

.global smp_ap_spurious_iret
.type smp_ap_spurious_iret,@function
smp_ap_spurious_iret:
 iretq
.size smp_ap_spurious_iret,.-smp_ap_spurious_iret

.global smp_ap_idle
.type smp_ap_idle,@function
smp_ap_idle:
 cli
 movq %rsi,%rsp
 andq $-16,%rsp
 call smp_ap_online
1: hlt
 jmp 1b
.size smp_ap_idle,.-smp_ap_idle
.section .note.GNU-stack,"",@progbits
