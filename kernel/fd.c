#include <stdint.h>

#include "kernel/fd.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

#define FD_ERROR_EBADF 9
#define FD_ERROR_EINVAL 22
#define FD_ERROR_EOVERFLOW 75
#define FD_ERROR_EIO 5
#define FD_ERROR_EMFILE 24

static struct fd_object *get_object(struct fd_table *table,
	uint64_t descriptor);

static int
console_write(void *context, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	(void)context;
	(void)offset;
	if (buffer == NULL && length != 0)
		return -FD_ERROR_EINVAL;
	if (write_length == NULL)
		return -FD_ERROR_EINVAL;
	if (!tty_write(buffer, length) || !serial_write(buffer, length))
		return -FD_ERROR_EIO;
	*write_length = length;
	return 0;
}

static int
console_read(void *context, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	(void)context;
	(void)offset;
	return tty_read(buffer, length, read_length);
}

static const struct fd_operations console_operations = {
	.read = console_read,
	.write = console_write
};

static struct fd_object standard_input = {
	.operations = &console_operations,
	.references = UINT32_MAX,
	.immortal = true
};

static struct fd_object standard_output = {
	.operations = &console_operations,
	.references = UINT32_MAX,
	.immortal = true
};

static struct fd_object standard_error = {
	.operations = &console_operations,
	.references = UINT32_MAX,
	.immortal = true
};

struct fd_object *
fd_object_create(const struct fd_operations *operations, void *context)
{
	struct fd_object *object;

	if (operations == NULL)
		return NULL;
	object = kheap_alloc(sizeof(*object));
	if (object == NULL)
		return NULL;
	*object = (struct fd_object){
		.operations = operations,
		.context = context,
		.references = 1
	};
	return object;
}

void
fd_object_get(struct fd_object *object)
{
	if (object == NULL || object->immortal)
		return;
	if (object->references == UINT32_MAX) {
		klog("OpenSillicon panic: file reference count overflow\n");
		kernel_halt();
	}
	object->references++;
}

void
fd_object_put(struct fd_object *object)
{
	if (object == NULL || object->immortal)
		return;
	if (object->references == 0) {
		klog("OpenSillicon panic: file reference count underflow\n");
		kernel_halt();
	}
	object->references--;
	if (object->references != 0)
		return;
	if (object->operations->close != NULL)
		object->operations->close(object->context);
	if (!kheap_free(object)) {
		klog("OpenSillicon panic: cannot release file object\n");
		kernel_halt();
	}
}

void
fd_table_init_standard(struct fd_table *table)
{
	unsigned int i;

	if (table == NULL)
		return;
	for (i = 0; i < FD_TABLE_CAPACITY; i++)
		table->entries[i] = NULL;
	table->entries[0] = &standard_input;
	table->entries[1] = &standard_output;
	table->entries[2] = &standard_error;
}

bool
fd_table_clone(struct fd_table *destination, const struct fd_table *source)
{
	unsigned int i;

	if (destination == NULL || source == NULL || destination == source)
		return false;
	for (i = 0; i < FD_TABLE_CAPACITY; i++) {
		destination->entries[i] = source->entries[i];
		fd_object_get(destination->entries[i]);
	}
	return true;
}

void
fd_table_destroy(struct fd_table *table)
{
	unsigned int i;

	if (table == NULL)
		return;
	for (i = 0; i < FD_TABLE_CAPACITY; i++) {
		struct fd_object *object = table->entries[i];

		table->entries[i] = NULL;
		fd_object_put(object);
	}
}

int
fd_install(struct fd_table *table, struct fd_object *object)
{
	unsigned int i;

	if (table == NULL || object == NULL)
		return -FD_ERROR_EINVAL;
	for (i = 3; i < FD_TABLE_CAPACITY; i++) {
		if (table->entries[i] == NULL) {
			table->entries[i] = object;
			return (int)i;
		}
	}
	return -FD_ERROR_EMFILE;
}

int
fd_install_pair(struct fd_table *table, struct fd_object *first,
	struct fd_object *second, int *first_descriptor,
	int *second_descriptor)
{
	unsigned int i;
	int first_slot = -1;
	int second_slot = -1;

	if (table == NULL || first == NULL || second == NULL || first == second ||
	    first_descriptor == NULL || second_descriptor == NULL)
		return -FD_ERROR_EINVAL;
	for (i = 3; i < FD_TABLE_CAPACITY; i++) {
		if (table->entries[i] != NULL)
			continue;
		if (first_slot < 0)
			first_slot = (int)i;
		else {
			second_slot = (int)i;
			break;
		}
	}
	if (second_slot < 0)
		return -FD_ERROR_EMFILE;
	table->entries[first_slot] = first;
	table->entries[second_slot] = second;
	*first_descriptor = first_slot;
	*second_descriptor = second_slot;
	return 0;
}

struct fd_object *
fd_object_for_descriptor(struct fd_table *table, uint64_t descriptor)
{
	return get_object(table, descriptor);
}

int
fd_close(struct fd_table *table, uint64_t descriptor)
{
	struct fd_object *object;

	if (table == NULL || descriptor >= FD_TABLE_CAPACITY ||
	    table->entries[descriptor] == NULL)
		return -FD_ERROR_EBADF;
	object = table->entries[descriptor];
	table->entries[descriptor] = NULL;
	fd_object_put(object);
	return 0;
}

static struct fd_object *
get_object(struct fd_table *table, uint64_t descriptor)
{
	if (table == NULL || descriptor >= FD_TABLE_CAPACITY)
		return NULL;
	return table->entries[descriptor];
}

int
fd_object_read(struct fd_object *object, void *buffer, size_t length,
	size_t *read_length)
{
	uint64_t offset;
	size_t amount = 0;
	int error;

	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (object->operations->read == NULL)
		return -9;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -FD_ERROR_EINVAL;
	offset = object->offset;
	error = object->operations->read(object->context, offset, buffer, length,
	    &amount);
	if (error != 0)
		return error;
	if (amount > length || offset > UINT64_MAX - amount)
		return -FD_ERROR_EIO;
	object->offset = offset + amount;
	*read_length = amount;
	return 0;
}

int
fd_read(struct fd_table *table, uint64_t descriptor, void *buffer,
	size_t length, size_t *read_length)
{
	return fd_object_read(get_object(table, descriptor), buffer, length,
	    read_length);
}

int
fd_object_write(struct fd_object *object, const void *buffer, size_t length,
	size_t *write_length)
{
	uint64_t offset;
	size_t amount = 0;
	int error;

	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (object->operations->write == NULL)
		return -9;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -FD_ERROR_EINVAL;
	offset = object->offset;
	error = object->operations->write(object->context, offset, buffer, length,
	    &amount);
	if (error != 0)
		return error;
	if (amount > length || offset > UINT64_MAX - amount)
		return -FD_ERROR_EIO;
	object->offset = offset + amount;
	*write_length = amount;
	return 0;
}

int
fd_write(struct fd_table *table, uint64_t descriptor, const void *buffer,
	size_t length, size_t *write_length)
{
	return fd_object_write(get_object(table, descriptor), buffer, length,
	    write_length);
}

int
fd_readdir(struct fd_table *table, uint64_t descriptor,
	struct vfs_dirent *entry)
{
	struct fd_object *object = get_object(table, descriptor);
	int result;

	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (object->operations->readdir == NULL)
		return -9;
	if (entry == NULL)
		return -FD_ERROR_EINVAL;
	result = object->operations->readdir(object->context, object->offset,
	    entry);
	if (result > 0) {
		if (object->offset == UINT64_MAX)
			return -FD_ERROR_EOVERFLOW;
		object->offset++;
	}
	return result;
}

uint64_t
fd_get_offset(const struct fd_table *table, uint64_t descriptor)
{
	if (table == NULL || descriptor >= FD_TABLE_CAPACITY ||
	    table->entries[descriptor] == NULL)
		return UINT64_MAX;
	return table->entries[descriptor]->offset;
}

bool
fd_set_offset(struct fd_table *table, uint64_t descriptor, uint64_t offset)
{
	if (table == NULL || descriptor >= FD_TABLE_CAPACITY ||
	    table->entries[descriptor] == NULL)
		return false;
	table->entries[descriptor]->offset = offset;
	return true;
}
