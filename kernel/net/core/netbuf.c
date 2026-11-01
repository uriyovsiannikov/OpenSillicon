#include "kernel/netbuf.h"

struct netbuf_slot {
	uint8_t bytes[NETBUF_CAPACITY];
	size_t length;
	uint64_t generation;
	enum netbuf_owner owner;
};

static struct netbuf_slot slots[NETBUF_POOL_CAPACITY];
static bool initialized;

static bool
owner_valid(enum netbuf_owner owner)
{
	return owner == NETBUF_OWNER_DEVICE || owner == NETBUF_OWNER_CORE;
}

static struct netbuf_slot *
lookup(struct netbuf_ref reference, enum netbuf_owner owner)
{
	struct netbuf_slot *slot;

	if (!initialized || reference.slot >= NETBUF_POOL_CAPACITY ||
	    reference.generation == 0 || !owner_valid(owner))
		return NULL;
	slot = &slots[reference.slot];
	if (slot->generation != reference.generation || slot->owner != owner)
		return NULL;
	return slot;
}

bool
netbuf_pool_init(void)
{
	if (initialized)
		return false;
	for (size_t i = 0; i < NETBUF_POOL_CAPACITY; i++) {
		slots[i].length = 0;
		slots[i].generation = 0;
		slots[i].owner = NETBUF_OWNER_NONE;
	}
	initialized = true;
	return true;
}

bool
netbuf_alloc(enum netbuf_owner owner, struct netbuf_ref *reference)
{
	if (!initialized || reference == NULL || !owner_valid(owner))
		return false;
	for (size_t i = 0; i < NETBUF_POOL_CAPACITY; i++) {
		struct netbuf_slot *slot = &slots[i];

		if (slot->owner != NETBUF_OWNER_NONE)
			continue;
		slot->generation++;
		if (slot->generation == 0)
			slot->generation++;
		slot->length = 0;
		slot->owner = owner;
		*reference = (struct netbuf_ref){
			.slot = (uint16_t)i,
			.generation = slot->generation
		};
		return true;
	}
	return false;
}

bool
netbuf_transfer(struct netbuf_ref reference, enum netbuf_owner from,
	enum netbuf_owner to)
{
	struct netbuf_slot *slot = lookup(reference, from);

	if (slot == NULL || !owner_valid(to) || to == from)
		return false;
	slot->owner = to;
	return true;
}

bool
netbuf_release(struct netbuf_ref reference, enum netbuf_owner owner)
{
	struct netbuf_slot *slot = lookup(reference, owner);

	if (slot == NULL)
		return false;
	slot->length = 0;
	slot->owner = NETBUF_OWNER_NONE;
	return true;
}

uint8_t *
netbuf_data(struct netbuf_ref reference, enum netbuf_owner owner)
{
	struct netbuf_slot *slot = lookup(reference, owner);

	return slot == NULL ? NULL : slot->bytes + NETBUF_HEADROOM;
}

size_t
netbuf_capacity(struct netbuf_ref reference, enum netbuf_owner owner)
{
	return lookup(reference, owner) == NULL ? 0 :
	    NETBUF_CAPACITY - NETBUF_HEADROOM;
}

size_t
netbuf_length(struct netbuf_ref reference, enum netbuf_owner owner)
{
	struct netbuf_slot *slot = lookup(reference, owner);

	return slot == NULL ? 0 : slot->length;
}

bool
netbuf_set_length(struct netbuf_ref reference, enum netbuf_owner owner,
	size_t length)
{
	struct netbuf_slot *slot = lookup(reference, owner);

	if (slot == NULL || length > NETBUF_CAPACITY - NETBUF_HEADROOM)
		return false;
	slot->length = length;
	return true;
}

size_t
netbuf_pool_available(void)
{
	size_t available = 0;

	if (!initialized)
		return 0;
	for (size_t i = 0; i < NETBUF_POOL_CAPACITY; i++)
		if (slots[i].owner == NETBUF_OWNER_NONE)
			available++;
	return available;
}
