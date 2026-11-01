ENTRY(_start)
PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS(5);
	data PT_LOAD FLAGS(6);
}
SECTIONS
{
	. = 0x400000;
	.text : { *(.text*) *(.rodata*) } :text
	. = ALIGN(0x1000);
	.data : { *(.data*) } :data
	.bss : { *(.bss*) *(COMMON) } :data
	/DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
