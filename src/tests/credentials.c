#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/credentials.h"

static void
check_transition(bool group, bool effective_only, uint32_t id,
    bool privileged, bool allowed, uint32_t real, uint32_t effective,
    uint32_t saved)
{
	struct os_credentials before = {
		.real_uid = 100, .effective_uid = privileged ? 0 : 200,
		.saved_uid = 300, .real_gid = 100, .effective_gid = 200,
		.saved_gid = 300
	};
	struct os_credentials after = before;
	bool result = group ? credentials_set_gid(&after, id, effective_only) :
	    credentials_set_uid(&after, id, effective_only);
	assert(result == allowed);
	if (!allowed) {
		assert(memcmp(&after, &before, sizeof(after)) == 0);
		return;
	}
	if (group) {
		assert(after.real_gid == real && after.effective_gid == effective &&
		    after.saved_gid == saved);
		after.real_gid = before.real_gid;
		after.effective_gid = before.effective_gid;
		after.saved_gid = before.saved_gid;
	} else {
		assert(after.real_uid == real && after.effective_uid == effective &&
		    after.saved_uid == saved);
		after.real_uid = before.real_uid;
		after.effective_uid = before.effective_uid;
		after.saved_uid = before.saved_uid;
	}
	assert(memcmp(&after, &before, sizeof(after)) == 0);
}

int main(void)
{
	for (unsigned int group = 0; group < 2; group++) {
		/* Unprivileged setuid/setgid affect only the effective identity. */
		check_transition(group, false, 100, false, true, 100, 100, 300);
		check_transition(group, false, 300, false, true, 100, 300, 300);
		check_transition(group, false, 400, false, false, 0, 0, 0);
		check_transition(group, true, 100, false, true, 100, 100, 300);
		check_transition(group, true, 200, false, true, 100, 200, 300);
		check_transition(group, true, 300, false, true, 100, 300, 300);
		check_transition(group, true, 400, false, false, 0, 0, 0);
		check_transition(group, false, 400, true, true, 400, 400, 400);
		check_transition(group, true, 400, true, true, 100, 400, 300);
	}
	/* Becoming effective UID 0 must not retroactively authorize an all-ID
	 * change in the same operation. */
	struct os_credentials c = {.real_uid = 100, .effective_uid = 200};
	assert(credentials_set_uid(&c, 0, false));
	assert(c.real_uid == 100 && c.effective_uid == 0 && c.saved_uid == 0);
	puts("Credential transition tests passed");
	return 0;
}
