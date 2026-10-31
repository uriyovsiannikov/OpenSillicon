#include "kernel/fd.h"
#include "kernel/log.h"

static bool
console_write(const char *buffer, size_t length)
{
	return serial_write(buffer, length);
}

static const struct file_operations console_operations = {
	.write = console_write
};

void
fd_table_init_standard(struct fd_table *table)
{
	if (table == NULL)
		return;
	table->entries[0].operations = NULL;
	table->entries[1].operations = &console_operations;
	table->entries[2].operations = &console_operations;
}

bool
fd_write(struct fd_table *table, uint64_t descriptor, const char *buffer,
	size_t length)
{
	const struct file_operations *operations;

	if (table == NULL || descriptor >= FD_TABLE_CAPACITY ||
	    (buffer == NULL && length != 0))
		return false;
	operations = table->entries[descriptor].operations;
	return operations != NULL && operations->write != NULL &&
	    operations->write(buffer, length);
}
