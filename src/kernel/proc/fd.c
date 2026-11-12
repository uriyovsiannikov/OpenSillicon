#include <stdint.h>

#include "kernel/fd.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/process.h"
#include "kernel/terminal.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

#define FD_ERROR_EBADF 9
#define FD_ERROR_EINVAL 22
#define FD_ERROR_ESPIPE 29
#define FD_ERROR_ENOTDIR 20
#define FD_ERROR_EOVERFLOW 75
#define FD_ERROR_EIO 5
#define FD_ERROR_EMFILE 24
#define FD_ERROR_EOPNOTSUPP 95

static struct fd_object *get_object(struct fd_table *table,
	uint64_t descriptor);
static bool console_framebuffer_enabled;
static uint8_t fd_readiness_channel;

void
fd_readiness_wake(void)
{
	process_wake_channel(&fd_readiness_channel);
}

void *
fd_readiness_wait_channel(void)
{
	return &fd_readiness_channel;
}

void
fd_console_framebuffer_enable(bool enable)
{
	console_framebuffer_enabled = enable;
}

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
	if ((console_framebuffer_enabled && !tty_write(buffer, length)) ||
	    !serial_write(buffer, length))
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

static bool
console_is_terminal(void *context)
{
	(void)context;
	return true;
}

static const struct fd_operations console_operations = {
	.read = console_read,
	.write = console_write,
	.is_terminal = console_is_terminal
};

static struct fd_object standard_input = {
	.operations = &console_operations,
	.status_flags = 0,
	.references = UINT32_MAX,
	.immortal = true
};

static struct fd_object standard_output = {
	.operations = &console_operations,
	.status_flags = 1,
	.references = UINT32_MAX,
	.immortal = true
};

static struct fd_object standard_error = {
	.operations = &console_operations,
	.status_flags = 1,
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
	if (__atomic_add_fetch(&object->references, 1, __ATOMIC_RELAXED) == 0) {
		klog("OpenSillicon panic: file reference count overflow\n");
		kernel_halt();
	}
}

void
fd_object_put(struct fd_object *object)
{
	if (object == NULL || object->immortal)
		return;
	uint32_t previous = __atomic_fetch_sub(&object->references, 1,
	    __ATOMIC_ACQ_REL);
	if (previous == 0) {
		klog("OpenSillicon panic: file reference count underflow\n");
		kernel_halt();
	}
	if (previous != 1)
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
	for (i = 0; i < FD_TABLE_CAPACITY; i++) {
		table->entries[i] = NULL;
		table->descriptor_flags[i] = 0;
	}
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
		destination->descriptor_flags[i] = source->descriptor_flags[i];
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
		table->descriptor_flags[i] = 0;
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
			table->descriptor_flags[i] = 0;
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
	table->descriptor_flags[first_slot] = 0;
	table->descriptor_flags[second_slot] = 0;
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
fd_get_descriptor_flags(struct fd_table *table, uint64_t descriptor,
	uint32_t *flags)
{
	if (table == NULL || flags == NULL || descriptor >= FD_TABLE_CAPACITY ||
	    table->entries[descriptor] == NULL)
		return -FD_ERROR_EBADF;
	*flags = table->descriptor_flags[descriptor];
	return 0;
}

int
fd_set_descriptor_flags(struct fd_table *table, uint64_t descriptor,
	uint32_t flags)
{
	if (table == NULL || descriptor >= FD_TABLE_CAPACITY ||
	    table->entries[descriptor] == NULL)
		return -FD_ERROR_EBADF;
	if ((flags & ~FD_DESCRIPTOR_CLOEXEC) != 0)
		return -FD_ERROR_EINVAL;
	table->descriptor_flags[descriptor] = flags;
	return 0;
}

void
fd_table_close_on_exec(struct fd_table *table)
{
	unsigned int i;
	if (table == NULL)
		return;
	for (i = 0; i < FD_TABLE_CAPACITY; i++)
		if ((table->descriptor_flags[i] & FD_DESCRIPTOR_CLOEXEC) != 0)
			(void)fd_close(table, i);
}

bool
fd_object_is_terminal(const struct fd_object *object)
{
	return object != NULL && object->operations != NULL &&
	    object->operations->is_terminal != NULL &&
	    object->operations->is_terminal(object->context);
}

bool
fd_object_is_console(const struct fd_object *object)
{
	return object != NULL && object->operations == &console_operations;
}

uint32_t
fd_object_poll(struct fd_object *object, uint32_t events)
{
	uint32_t ready = 0;
	if (object == NULL || object->operations == NULL)
		return 0;
	if (object->operations->poll != NULL)
		return object->operations->poll(object->context, events) & events;
	if ((events & FD_POLL_READ) != 0) {
		if (fd_object_is_terminal(object)) {
			if (terminal_input_available())
				ready |= FD_POLL_READ;
		} else if (object->operations->read != NULL ||
		    object->operations->readdir != NULL) {
			ready |= FD_POLL_READ;
		}
	}
	if ((events & FD_POLL_WRITE) != 0 &&
	    object->operations->write != NULL)
		ready |= FD_POLL_WRITE;
	return ready;
}

bool
fd_object_nonblocking(const struct fd_object *object)
{
	return object != NULL &&
	    (object->status_flags & FD_STATUS_NONBLOCK) != 0;
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
	table->descriptor_flags[descriptor] = 0;
	fd_object_put(object);
	return 0;
}

int
fd_dup(struct fd_table *table, uint64_t source)
{
	struct fd_object *object;
	unsigned int descriptor;

	if (table == NULL || source >= FD_TABLE_CAPACITY ||
	    table->entries[source] == NULL)
		return -FD_ERROR_EBADF;
	for (descriptor = 0; descriptor < FD_TABLE_CAPACITY; descriptor++)
		if (table->entries[descriptor] == NULL)
			break;
	if (descriptor == FD_TABLE_CAPACITY)
		return -FD_ERROR_EMFILE;
	object = table->entries[source];
	fd_object_get(object);
	table->entries[descriptor] = object;
	table->descriptor_flags[descriptor] = 0;
	return (int)descriptor;
}

int
fd_dup2(struct fd_table *table, uint64_t source, uint64_t destination)
{
	struct fd_object *source_object;
	struct fd_object *old_destination;

	if (table == NULL || source >= FD_TABLE_CAPACITY ||
	    destination >= FD_TABLE_CAPACITY ||
	    table->entries[source] == NULL)
		return -FD_ERROR_EBADF;
	if (source == destination)
		return (int)destination;
	source_object = table->entries[source];
	fd_object_get(source_object);
	old_destination = table->entries[destination];
	table->entries[destination] = source_object;
	table->descriptor_flags[destination] = 0;
	fd_object_put(old_destination);
	return (int)destination;
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
	return fd_object_write_as(object, buffer, length, write_length, 0);
}

int
fd_object_write_as(struct fd_object *object, const void *buffer, size_t length,
	size_t *write_length, uint32_t uid)
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
	if (length != 0 && object->operations->prepare_write != NULL) {
		error = object->operations->prepare_write(object->context, uid);
		if (error != 0)
			return error;
	}
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
fd_write_as(struct fd_table *table, uint64_t descriptor, const void *buffer,
	size_t length, size_t *write_length, uint32_t uid)
{
	return fd_object_write_as(get_object(table, descriptor), buffer, length,
	    write_length, uid);
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
		return -FD_ERROR_ENOTDIR;
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

int
fd_object_seek(struct fd_object *object, int64_t offset, int whence,
	uint64_t *result)
{
	uint64_t new_offset;
	int error;

	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (result == NULL)
		return -FD_ERROR_EINVAL;
	if (object->operations->seek == NULL)
		return -FD_ERROR_ESPIPE;
	error = object->operations->seek(object->context, object->offset,
	    offset, whence, &new_offset);
	if (error != 0)
		return error;
	object->offset = new_offset;
	*result = new_offset;
	return 0;
}

int
fd_seek(struct fd_table *table, uint64_t descriptor, int64_t offset,
	int whence, uint64_t *result)
{
	return fd_object_seek(get_object(table, descriptor), offset, whence,
	    result);
}

int
fd_object_stat(struct fd_object *object, struct fd_metadata *metadata)
{
	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (metadata == NULL)
		return -FD_ERROR_EINVAL;
	if (object->operations->stat == NULL)
		return -FD_ERROR_EOPNOTSUPP;
	return object->operations->stat(object->context, metadata);
}

int
fd_object_chmod(struct fd_object *object, uint32_t mode)
{
	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (object->operations->chmod == NULL)
		return -FD_ERROR_EOPNOTSUPP;
	return object->operations->chmod(object->context, mode);
}

int
fd_object_chown(struct fd_object *object, uint32_t uid, uint32_t gid)
{
	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (object->operations->chown == NULL)
		return -FD_ERROR_EOPNOTSUPP;
	return object->operations->chown(object->context, uid, gid);
}

int
fd_object_lookup_at_as(struct fd_object *object, const char *path,
	uint32_t uid, uint32_t gid, struct vnode **result)
{
	if (result == NULL)
		return -FD_ERROR_EINVAL;
	*result = NULL;
	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (object->operations->lookup_at == NULL)
		return -FD_ERROR_ENOTDIR;
	return object->operations->lookup_at(object->context, path, uid, gid, result);
}

int
fd_object_get_path(struct fd_object *object, char *path, size_t capacity)
{
	if (object == NULL)
		return -FD_ERROR_EBADF;
	if (object->operations->get_path == NULL)
		return -FD_ERROR_ENOTDIR;
	if (path == NULL || capacity == 0)
		return -FD_ERROR_EINVAL;
	return object->operations->get_path(object->context, path, capacity);
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
