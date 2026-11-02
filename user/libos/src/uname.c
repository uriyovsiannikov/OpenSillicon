#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <string.h>

int
uname(struct utsname *name)
{
	static const struct utsname system_name = {
		.sysname = "OpenSillicon",
		.nodename = "localhost",
		.release = "unknown",
		.version = "unknown",
		.machine = "x86_64"
	};
	if (name == 0) {
		errno = EFAULT;
		return -1;
	}
	memcpy(name, &system_name, sizeof(system_name));
	return 0;
}

int
gethostname(char *name, size_t length)
{
	struct utsname information;
	size_t count;
	if (name == 0 || length == 0 || uname(&information) < 0)
		return -1;
	count = strlen(information.nodename);
	if (count >= length) {
		errno = EINVAL;
		return -1;
	}
	memcpy(name, information.nodename, count + 1);
	return 0;
}
