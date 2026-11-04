#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/pmm.h"
#include "kernel/procfs.h"
#include "kernel/timer.h"

#define PROCFS_EINVAL 22
#define PROCFS_ENOENT 2
#define PROCFS_ENOMEM 12
#define PROCFS_ENOTDIR 20
#define PROCFS_CONTENT_MAX 512U
#define PROCFS_TICK_HZ 100U

enum procfs_entry {
	PROCFS_ROOT,
	PROCFS_SELF,
	PROCFS_UPTIME,
	PROCFS_MEMINFO,
	PROCFS_CPUINFO
};

struct procfs_node {
	struct vnode vnode;
	enum procfs_entry entry;
	size_t content_length;
	uint64_t self_inode;
	uint64_t uptime_inode;
	uint64_t meminfo_inode;
	uint64_t cpuinfo_inode;
	char name[16];
	char content[PROCFS_CONTENT_MAX];
};

static int procfs_lookup(struct vnode *, const char *, size_t, struct vnode **);
static int procfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int procfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void procfs_destroy(struct vnode *);

static const struct vnode_operations procfs_operations = {
	.lookup = procfs_lookup,
	.read = procfs_read,
	.readdir = procfs_readdir,
	.destroy = procfs_destroy
};

static struct procfs_node *
procfs_from_vnode(struct vnode *vnode)
{
	return (struct procfs_node *)vnode->private_data;
}

static bool
append_text(struct procfs_node *node, const char *text)
{
	size_t length = strlen(text);

	if (length > sizeof(node->content) - node->content_length)
		return false;
	memcpy(node->content + node->content_length, text, length);
	node->content_length += length;
	return true;
}

static bool
append_u64(struct procfs_node *node, uint64_t value)
{
	char digits[20];
	size_t length = 0;

	do {
		digits[length++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0);
	if (length > sizeof(node->content) - node->content_length)
		return false;
	while (length != 0)
		node->content[node->content_length++] = digits[--length];
	return true;
}

static struct procfs_node *
procfs_allocate(const char *name, enum procfs_entry entry,
	enum vnode_type type, uint64_t inode)
{
	struct procfs_node *node;

	if (inode == 0)
		inode = vfs_allocate_inode();
	if (inode == 0)
		return NULL;
	node = kheap_alloc(sizeof(*node));
	if (node == NULL)
		return NULL;
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, strlen(name) + 1U);
	node->entry = entry;
	node->vnode = (struct vnode){
		.operations = &procfs_operations,
		.type = type,
		.mode = type == VNODE_DIRECTORY ? 0555U : 0444U,
		.inode = inode,
		.references = 1,
		.private_data = node
	};
	return node;
}

static int
procfs_make_self(uint64_t inode, struct procfs_node **result)
{
	struct process_snapshot_entry current;
	struct procfs_node *node;
	static const char *const states[] = {
		[PROCESS_READY] = "ready",
		[PROCESS_RUNNING] = "running",
		[PROCESS_BLOCKED] = "blocked",
		[PROCESS_ZOMBIE] = "zombie"
	};
	int error;

	error = process_current_snapshot(&current);
	if (error != 0)
		return error;
	node = procfs_allocate("self", PROCFS_SELF, VNODE_REGULAR, inode);
	if (node == NULL)
		return -PROCFS_ENOMEM;
	if (!append_text(node, "Pid:\t") || !append_u64(node, current.pid) ||
	    !append_text(node, "\nPPid:\t") ||
	    !append_u64(node, current.parent_pid) ||
	    !append_text(node, "\nState:\t") ||
	    current.state >= sizeof(states) / sizeof(states[0]) ||
	    !append_text(node, states[current.state]) ||
	    !append_text(node, "\nExitStatus:\t") ||
	    !append_u64(node, current.exit_status) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -PROCFS_EINVAL;
	}
	node->content[node->content_length] = '\0';
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_make_uptime(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("uptime", PROCFS_UPTIME,
	    VNODE_REGULAR, inode);
	uint64_t ticks;
	uint64_t fractional;

	if (node == NULL)
		return -PROCFS_ENOMEM;
	ticks = timer_ticks();
	fractional = ticks % PROCFS_TICK_HZ;
	if (!append_u64(node, ticks / PROCFS_TICK_HZ) ||
	    !append_text(node, ".") ||
	    !append_text(node, (const char[]){
		(char)('0' + fractional / 10U),
		(char)('0' + fractional % 10U), '\0'
	    }) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -PROCFS_EINVAL;
	}
	node->content[node->content_length] = '\0';
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_make_meminfo(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("meminfo", PROCFS_MEMINFO,
	    VNODE_REGULAR, inode);
	uint64_t total = pmm_usable_pages();
	uint64_t free = pmm_free_pages();

	if (node == NULL)
		return -PROCFS_ENOMEM;
	/* PMM frames are 4096 bytes; report KiB in one immutable snapshot. */
	if (!append_text(node, "MemTotal: ") || !append_u64(node, total * 4U) ||
	    !append_text(node, " kB\nMemFree: ") ||
	    !append_u64(node, free * 4U) || !append_text(node, " kB\n")) {
		vnode_put(&node->vnode);
		return -PROCFS_EINVAL;
	}
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static void
procfs_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
	uint32_t *edx)
{
	__asm__ volatile ("cpuid" : "=a" (*eax), "=b" (*ebx), "=c" (*ecx),
	    "=d" (*edx) : "a" (leaf), "c" (0) : "memory");
}

static void
procfs_store_word(char *buffer, size_t offset, uint32_t word)
{
	for (size_t i = 0; i < 4; i++)
		buffer[offset + i] = (char)(word >> (i * 8U));
}

static int
procfs_make_cpuinfo(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("cpuinfo", PROCFS_CPUINFO,
	    VNODE_REGULAR, inode);
	uint32_t max_leaf, eax, ebx, ecx, edx;
	uint32_t signature = 0;
	uint32_t base_family, family, base_model, model, stepping;
	char vendor[13] = "UnknownCPU";
	char brand[49] = "Unknown processor";
	size_t brand_start = 0;

	if (node == NULL)
		return -PROCFS_ENOMEM;
	procfs_cpuid(0, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf >= 1U) {
		procfs_cpuid(1, &signature, &ebx, &ecx, &edx);
		procfs_store_word(vendor, 0, ebx);
		procfs_store_word(vendor, 4, edx);
		procfs_store_word(vendor, 8, ecx);
		vendor[12] = '\0';
	}
	procfs_cpuid(0x80000000U, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf >= 0x80000004U) {
		for (uint32_t i = 0; i < 3U; i++) {
			uint32_t leaf = 0x80000002U + i;
			procfs_cpuid(leaf, &eax, &ebx, &ecx, &edx);
			procfs_store_word(brand, i * 16U, eax);
			procfs_store_word(brand, i * 16U + 4U, ebx);
			procfs_store_word(brand, i * 16U + 8U, ecx);
			procfs_store_word(brand, i * 16U + 12U, edx);
		}
		brand[48] = '\0';
	}
	for (size_t i = strlen(brand); i != 0 && brand[i - 1U] == ' '; i--)
		brand[i - 1U] = '\0';
	while (brand[brand_start] == ' ')
		brand_start++;
	base_family = (signature >> 8) & 0x0fU;
	base_model = (signature >> 4) & 0x0fU;
	family = base_family == 0x0fU ? base_family +
	    ((signature >> 20) & 0xffU) : base_family;
	model = (base_family == 0x06U || base_family == 0x0fU) ?
	    base_model | (((signature >> 16) & 0x0fU) << 4) : base_model;
	stepping = signature & 0x0fU;
	if (!append_text(node, "processor: 0\nvendor_id: ") ||
	    !append_text(node, vendor) || !append_text(node, "\ncpu family: ") ||
	    !append_u64(node, family) || !append_text(node, "\nmodel: ") ||
	    !append_u64(node, model) || !append_text(node, "\nstepping: ") ||
	    !append_u64(node, stepping) || !append_text(node, "\nmodel name: ") ||
	    !append_text(node, brand + brand_start) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -PROCFS_EINVAL;
	}
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_lookup(struct vnode *directory, const char *name, size_t name_length,
	struct vnode **result)
{
	struct procfs_node *node;
	int error;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -PROCFS_ENOTDIR;
	if (name == NULL || result == NULL)
		return -PROCFS_EINVAL;
	*result = NULL;
	node = procfs_from_vnode(directory);
	if (node->entry != PROCFS_ROOT)
		return -PROCFS_ENOTDIR;
	if (name_length == 4 && memcmp(name, "self", 4) == 0) {
		struct procfs_node *self;

		error = procfs_make_self(node->self_inode, &self);
		if (error == 0)
			*result = &self->vnode;
		return error;
	}
	if (name_length == 6 && memcmp(name, "uptime", 6) == 0) {
		struct procfs_node *uptime;

		error = procfs_make_uptime(node->uptime_inode, &uptime);
		if (error == 0)
			*result = &uptime->vnode;
		return error;
	}
	if (name_length == 7 && memcmp(name, "meminfo", 7) == 0) {
		struct procfs_node *meminfo;

		error = procfs_make_meminfo(node->meminfo_inode, &meminfo);
		if (error == 0)
			*result = &meminfo->vnode;
		return error;
	}
	if (name_length == 7 && memcmp(name, "cpuinfo", 7) == 0) {
		struct procfs_node *cpuinfo;

		error = procfs_make_cpuinfo(node->cpuinfo_inode, &cpuinfo);
		if (error == 0)
			*result = &cpuinfo->vnode;
		return error;
	}
	return -PROCFS_ENOENT;
}

static int
procfs_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct procfs_node *node;
	size_t available;

	if (vnode == NULL || read_length == NULL ||
	    (buffer == NULL && length != 0))
		return -PROCFS_EINVAL;
	*read_length = 0;
	if (vnode->type == VNODE_DIRECTORY)
		return -PROCFS_EINVAL;
	node = procfs_from_vnode(vnode);
	if (offset >= node->content_length)
		return 0;
	available = node->content_length - (size_t)offset;
	if (length > available)
		length = available;
	if (length != 0)
		memcpy(buffer, node->content + (size_t)offset, length);
	*read_length = length;
	return 0;
}

static int
procfs_readdir(struct vnode *directory, uint64_t index,
	struct vfs_dirent *entry)
{
	static const char *const names[] = {
		"self", "uptime", "meminfo", "cpuinfo"
	};
	struct procfs_node *node;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -PROCFS_ENOTDIR;
	if (entry == NULL)
		return -PROCFS_EINVAL;
	if (index >= sizeof(names) / sizeof(names[0]))
		return 0;
	node = procfs_from_vnode(directory);
	memset(entry, 0, sizeof(*entry));
	entry->inode = index == 0 ? node->self_inode :
	    index == 1 ? node->uptime_inode : index == 2 ? node->meminfo_inode :
	    node->cpuinfo_inode;
	entry->mode = 0444U;
	entry->type = VNODE_REGULAR;
	memcpy(entry->name, names[index], strlen(names[index]) + 1U);
	return 1;
}

static void
procfs_destroy(struct vnode *vnode)
{
	(void)kheap_free(procfs_from_vnode(vnode));
}

int
procfs_mount(const char *path)
{
	struct procfs_node *root = procfs_allocate("", PROCFS_ROOT,
	    VNODE_DIRECTORY, 0);
	int error;

	if (root == NULL)
		return -PROCFS_ENOMEM;
	root->self_inode = vfs_allocate_inode();
	root->uptime_inode = vfs_allocate_inode();
	root->meminfo_inode = vfs_allocate_inode();
	root->cpuinfo_inode = vfs_allocate_inode();
	if (root->self_inode == 0 || root->uptime_inode == 0 ||
	    root->meminfo_inode == 0 || root->cpuinfo_inode == 0) {
		vnode_put(&root->vnode);
		return -PROCFS_ENOMEM;
	}
	error = vfs_mount(path, &root->vnode);
	vnode_put(&root->vnode);
	return error;
}
