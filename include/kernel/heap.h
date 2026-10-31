#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <stdbool.h>
#include <stddef.h>

bool kheap_init(void);
void *kheap_alloc(size_t size);
bool kheap_free(void *allocation);

#endif
