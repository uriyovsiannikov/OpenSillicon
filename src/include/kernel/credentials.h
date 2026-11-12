#ifndef KERNEL_CREDENTIALS_H
#define KERNEL_CREDENTIALS_H

#include <stdbool.h>
#include "abi/credentials.h"

/* The process layer serializes updates before applying this policy. */
static inline bool
credentials_set_uid(struct os_credentials *credentials, uint32_t id,
    bool effective_only)
{
	if (id == UINT32_MAX)
		return false;
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
	if (id == UINT32_MAX)
		return false;
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

static inline bool
credentials_set_res_ids(struct os_credentials *credentials, bool group,
    uint32_t real, uint32_t effective, uint32_t saved)
{
	const uint32_t requested[3] = {real, effective, saved};
	const uint32_t available[3] = {
		group ? credentials->real_gid : credentials->real_uid,
		group ? credentials->effective_gid : credentials->effective_uid,
		group ? credentials->saved_gid : credentials->saved_uid
	};
	/* Authorize the entire request against the old credentials before any
	 * ID changes, including a change that drops or restores effective root. */
	if (credentials->effective_uid != 0) {
		for (unsigned int i = 0; i < 3; i++) {
			if (requested[i] != UINT32_MAX && requested[i] != available[0] &&
			    requested[i] != available[1] && requested[i] != available[2])
				return false;
		}
	}
	if (group) {
		if (real != UINT32_MAX) credentials->real_gid = real;
		if (effective != UINT32_MAX) credentials->effective_gid = effective;
		if (saved != UINT32_MAX) credentials->saved_gid = saved;
	} else {
		if (real != UINT32_MAX) credentials->real_uid = real;
		if (effective != UINT32_MAX) credentials->effective_uid = effective;
		if (saved != UINT32_MAX) credentials->saved_uid = saved;
	}
	return true;
}

#endif
