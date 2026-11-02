.text
.global __libos_raw_syscall
.type __libos_raw_syscall,@function
__libos_raw_syscall:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10
    mov %r9, %r8
    mov 8(%rsp), %r9
    int $0x80
    setc %r11b
    test %r11b, %r11b
    jz 1f
    neg %rax
1:
    ret
.size __libos_raw_syscall, .-__libos_raw_syscall
.section .note.GNU-stack,"",@progbits
