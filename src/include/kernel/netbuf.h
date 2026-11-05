#ifndef KERNEL_NETBUF_H
#define KERNEL_NETBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETBUF_POOL_CAPACITY 64U
#define NETBUF_CAPACITY 1600U
#define NETBUF_HEADROOM 32U

enum netbuf_owner {
	NETBUF_OWNER_NONE = 0,
	NETBUF_OWNER_DEVICE,
	NETBUF_OWNER_CORE
};

/* Generation-tagged references reject stale handles after a slot is reused. */
struct netbuf_ref {
	uint16_t slot;
	uint64_t generation;
};

bool netbuf_pool_init(void);
bool netbuf_alloc(enum netbuf_owner owner, struct netbuf_ref *reference);
bool netbuf_transfer(struct netbuf_ref reference, enum netbuf_owner from,
	enum netbuf_owner to);
bool netbuf_release(struct netbuf_ref reference, enum netbuf_owner owner);
uint8_t *netbuf_data(struct netbuf_ref reference, enum netbuf_owner owner);
size_t netbuf_capacity(struct netbuf_ref reference, enum netbuf_owner owner);
size_t netbuf_length(struct netbuf_ref reference, enum netbuf_owner owner);
bool netbuf_set_length(struct netbuf_ref reference, enum netbuf_owner owner,
	size_t length);
size_t netbuf_pool_available(void);

#endif
