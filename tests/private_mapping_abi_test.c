#include <stdint.h>
#include <stdio.h>

#include "kernel/vmm.h"

int
main(void)
{
	if (!vmm_private_unmap_length_matches(4096, 1) ||
	    !vmm_private_unmap_length_matches(8192, 4097) ||
	    !vmm_private_unmap_length_matches(8192, 8192) ||
	    vmm_private_unmap_length_matches(8192, 4096) ||
	    vmm_private_unmap_length_matches(8192, 8193) ||
	    vmm_private_unmap_length_matches(4096, 0) ||
	    vmm_private_unmap_length_matches(4096, UINT64_MAX) ||
	    vmm_private_unmap_length_matches(4097, 4096)) {
		fputs("private mapping ABI length validation failed\n", stderr);
		return 1;
	}
	puts("private mapping complete-unmap length tests passed");
	return 0;
}
