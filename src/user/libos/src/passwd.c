#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>

static struct passwd root_user = {
	.pw_name = "root", .pw_passwd = "", .pw_uid = 0, .pw_gid = 0,
	.pw_gecos = "root", .pw_dir = "/", .pw_shell = "/bin/sh"
};

struct passwd *getpwuid(uid_t uid)
{
	if (uid == 0) return &root_user;
	errno = ENOENT;
	return 0;
}

struct passwd *getpwnam(const char *name)
{
	if (name != 0 && strcmp(name, "root") == 0) return &root_user;
	errno = ENOENT;
	return 0;
}

struct group *getgrnam(const char *name)
{
	(void)name;
	errno = ENOENT;
	return 0;
}
