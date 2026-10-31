#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "include/os.h"

int
main(void)
{
	char path[256];
	assert(u_normalize_path("/", "etc/../bin", path, sizeof(path)));
	assert(strcmp(path, "/bin") == 0);
	assert(u_normalize_path("/etc/rc.d", "../../bin/sh", path,
	    sizeof(path)));
	assert(strcmp(path, "/bin/sh") == 0);
	assert(u_normalize_path("/etc", "/tmp//file", path, sizeof(path)));
	assert(strcmp(path, "/tmp/file") == 0);
	assert(u_normalize_path("/", "../../../", path, sizeof(path)));
	assert(strcmp(path, "/") == 0);
	assert(!u_normalize_path("/", "a-long-component", path, 4));
	assert(!u_normalize_path("relative", "file", path, sizeof(path)));
	assert(u_streq("shell", "shell"));
	assert(!u_streq("shell", "sh"));
	puts("userspace path helper tests passed");
	return 0;
}
