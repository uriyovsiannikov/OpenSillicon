#ifndef LIBOS_GRP_H
#define LIBOS_GRP_H
struct group { char *gr_name; char *gr_passwd; unsigned int gr_gid; char **gr_mem; };
struct group *getgrnam(const char *);
#endif
