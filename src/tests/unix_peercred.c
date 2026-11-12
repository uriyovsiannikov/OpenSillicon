#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "abi/unix_socket.h"
#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/unix_socket.h"
#include "kernel/vfs.h"

static uint64_t current_pid;
static struct os_credentials current_credentials;
static bool credentials_available = true;

void *kheap_alloc(size_t size) { return malloc(size); }
bool kheap_free(void *allocation) { free(allocation); return true; }
void process_wake_channel(void *channel) { (void)channel; }
uint64_t process_current_pid(void) { return current_pid; }
bool process_current_credentials(struct os_credentials *credentials)
{
	if (!credentials_available) return false;
	*credentials = current_credentials;
	return true;
}
void klog(const char *format) { (void)format; abort(); }
_Noreturn void kernel_halt(void) { abort(); }

uint32_t process_get_umask(void) { return 0022; }
int process_resolve_path(const char *path, char *output, size_t capacity)
{
	return vfs_resolve_path_as("/", path, output, capacity,
	    current_credentials.effective_uid, current_credentials.effective_gid);
}

static void
identity(uint64_t pid, uint32_t uid, uint32_t gid)
{
	current_pid = pid;
	current_credentials.effective_uid = uid;
	current_credentials.effective_gid = gid;
}

static void check_peer(struct fd_object *, int32_t, uint32_t, uint32_t);

static void
ramfs_paths(void)
{
	struct fd_table table = {0};
	struct vnode *root, *tmp;
	struct unix_socket_address path = {.family = 1, .path = "/tmp/socket"};
	struct unix_socket_address alias = {.family = 1, .path = "/tmp/./socket"};
	assert(vfs_init());
	root = ramfs_create_root();
	assert(root != NULL && vfs_mount("/", root) == 0);
	vnode_put(root);
	assert(vfs_create_path("/tmp", VNODE_DIRECTORY, 01777, &tmp) == 0);
	vnode_put(tmp);
	identity(50, 100, 100);
	int server_fd = unix_socket_create(&table, 1, 1, 0);
	int client_fd = unix_socket_create(&table, 1, 1, 0);
	assert(server_fd >= 0 && client_fd >= 0);
	assert(unix_socket_bind(table.entries[server_fd], &path, sizeof(path)) == 0);
	assert(unix_socket_bind(table.entries[client_fd], &alias, sizeof(alias)) == -98);
	assert(unix_socket_listen(table.entries[server_fd], 1) == 0);
	identity(60, 200, 200);
	assert(unix_socket_connect(table.entries[client_fd], &path, sizeof(path)) == -13);
	assert(vfs_chmod_as("/tmp/socket", 0777, 100, 100) == 0);
	assert(unix_socket_connect(table.entries[client_fd], &alias, sizeof(alias)) == 0);
	int accepted = unix_socket_accept(&table, table.entries[server_fd]);
	assert(accepted >= 0);
	check_peer(table.entries[client_fd], 50, 100, 100);
	check_peer(table.entries[accepted], 60, 200, 200);
	fd_table_destroy(&table);
	assert(vfs_remove_path_as("/tmp/socket", false, 100, 100) == 0);
}

static void
check_peer(struct fd_object *object, int32_t pid, uint32_t uid, uint32_t gid)
{
	struct os_unix_ucred credentials;
	size_t length = sizeof(credentials);
	assert(unix_socket_get_option(object, UNIX_SOCKET_SO_PEERCRED,
	    &credentials, &length) == 0);
	assert(length == sizeof(credentials));
	assert(credentials.pid == pid);
	assert(credentials.uid == uid);
	assert(credentials.gid == gid);
}

int main(void)
{
	struct fd_table table = {0};
	struct unix_socket_address address = {
		.family = UNIX_SOCKET_AF_UNIX, .path = {0, 't', 'e', 's', 't'}
	};
	identity(10, 0, 0);
	int server_fd = unix_socket_create(&table, 1, 1, 0);
	int client_fd = unix_socket_create(&table, 1, 1, 0);
	assert(server_fd >= 0 && client_fd >= 0);
	struct fd_object *server = table.entries[server_fd];
	struct fd_object *client = table.entries[client_fd];
	assert(unix_socket_bind(server, &address, 7) == 0);
	credentials_available = false;
	assert(unix_socket_create(&table, 1, 1, 0) == -3);
	assert(unix_socket_listen(server, 1) == -3);
	assert(unix_socket_connect(client, &address, 7) == -3);
	credentials_available = true;
	/* A child inherits the socket, then drops privileges before listen. */
	identity(20, 100, 200);
	assert(unix_socket_listen(server, 1) == 0);
	identity(30, 300, 400);
	assert(unix_socket_connect(client, &address, 7) == 0);
	int accepted_fd = unix_socket_accept(&table, server);
	assert(accepted_fd >= 0);
	check_peer(client, 20, 100, 200);
	check_peer(table.entries[accepted_fd], 30, 300, 400);
	/* A later credential change does not alter an established connection. */
	identity(40, 500, 600);
	check_peer(client, 20, 100, 200);
	check_peer(table.entries[accepted_fd], 30, 300, 400);
	fd_table_destroy(&table);
	ramfs_paths();
	puts("Unix socket peer credential tests passed");
	return 0;
}
