#ifndef LIBOS_SYS_UTSNAME_H
#define LIBOS_SYS_UTSNAME_H

#define LIBOS_UTSNAME_LENGTH 65

struct utsname {
	char sysname[LIBOS_UTSNAME_LENGTH];
	char nodename[LIBOS_UTSNAME_LENGTH];
	char release[LIBOS_UTSNAME_LENGTH];
	char version[LIBOS_UTSNAME_LENGTH];
	char machine[LIBOS_UTSNAME_LENGTH];
};

int uname(struct utsname *name);

#endif
