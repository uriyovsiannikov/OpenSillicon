ENTRY(_start)
SECTIONS
{
	. = 0x400000;
	.text : { *(.text) } :text
	.rodata : { *(.rodata*) *(.eh_frame*) *(.note.gnu.property) } :text
	. = ALIGN(0x1000);
	.data : { *(.data*) } :data
	.bss : { *(.bss*) } :data
}
PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}
