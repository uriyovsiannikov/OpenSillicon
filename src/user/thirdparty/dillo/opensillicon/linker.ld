ENTRY(_start)
SECTIONS
{
	. = 0x400000;
	.text : { *(.text) } :text
	.rodata : { *(.rodata*) *(.eh_frame*) *(.note.gnu.property) } :text
	. = ALIGN(0x1000);
	.tdata : { *(.tdata .tdata.*) } :data :tls
	.tbss (NOLOAD) : { *(.tbss .tbss.*) *(.tcommon) } :data :tls
	.init_array : { __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; } :data
	.data : { *(.data*) } :data
	.bss : { *(.bss*) } :data
}
PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS(5);
	data PT_LOAD FLAGS(6);
	tls PT_TLS FLAGS(4);
}
