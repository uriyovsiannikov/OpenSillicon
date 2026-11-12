#ifndef KERNEL_CREDENTIALS_H
#define KERNEL_CREDENTIALS_H

#include <stdbool.h>
#include "abi/credentials.h"

/* The process layer serializes updates before applying this policy. */
static inline bool
credentials_set_uid(struct os_credentials *credentials, uint32_t id,
    bool effective_only)
{
	bool privileged = credentials->effective_uid == 0;
	if (!privileged && id != credentials->real_uid &&
	    id != credentials->saved_uid &&
	    !(effective_only && id == credentials->effective_uid))
		return false;
	credentials->effective_uid = id;
	if (privileged && !effective_only) {
		credentials->real_uid = id;
		credentials->saved_uid = id;
	}
	return true;
}

static inline bool
credentials_set_gid(struct os_credentials *credentials, uint32_t id,
    bool effective_only)
{
	bool privileged = credentials->effective_uid == 0;
	if (!privileged && id != credentials->real_gid &&
	    id != credentials->saved_gid &&
	    !(effective_only && id == credentials->effective_gid))
		return false;
	credentials->effective_gid = id;
	if (privileged && !effective_only) {
		credentials->real_gid = id;
		credentials->saved_gid = id;
	}
	return true;
}

#endif
