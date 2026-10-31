ENTRY(_start)
PHDRS
{
	text PT_LOAD FLAGS(5);
	requests PT_LOAD FLAGS(6);
	rodata PT_LOAD FLAGS(4);
	data PT_LOAD FLAGS(6);
}
SECTIONS
{
	. = 0xffffffff80000000;
	__kernel_start = .;
	.text : ALIGN(4K) { *(.text .text.*) } :text
	/* Keep writable protocol requests separate from executable text. */
	.limine_requests_start : ALIGN(4K) { KEEP(*(.limine_requests_start)) } :requests
	.limine_requests : ALIGN(8) { KEEP(*(.limine_requests .limine_requests.*)) } :requests
	.limine_requests_end : ALIGN(8) { KEEP(*(.limine_requests_end)) } :requests
	.rodata : ALIGN(4K) { *(.rodata .rodata.*) } :rodata
	.data : ALIGN(4K) { *(.data .data.*) } :data
	.bss : ALIGN(4K) { *(COMMON) *(.bss .bss.*) } :data
	__kernel_end = .;
	/DISCARD/ : { *(.eh_frame .note .note.* .comment) }
}
