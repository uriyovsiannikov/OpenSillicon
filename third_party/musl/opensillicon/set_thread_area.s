/* musl passes its x86_64 Variant-II thread pointer here. */
.text
.global __set_thread_area
.hidden __set_thread_area
.type __set_thread_area,@function
__set_thread_area:
	mov %rdi,%rdx
	mov $158,%edi              /* musl's internal Linux SYS_arch_prctl id */
	mov $0x1002,%esi           /* ARCH_SET_FS */
	sub $8,%rsp
	call __opensillicon_syscall
	add $8,%rsp
	ret
