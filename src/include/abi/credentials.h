#ifndef ABI_CREDENTIALS_H
#define ABI_CREDENTIALS_H

#include <stdint.h>

struct os_credentials {
	uint32_t real_uid;
	uint32_t effective_uid;
	uint32_t saved_uid;
	uint32_t real_gid;
	uint32_t effective_gid;
	uint32_t saved_gid;
};

#endif
