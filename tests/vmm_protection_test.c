#include <stdint.h>
#include <stdio.h>

#include "kernel/vmm.h"

int
main(void)
{
	uint64_t rounded;
	uint64_t end;

	if (!vmm_private_protection_valid(VMM_PROT_NONE) ||
	    !vmm_private_protection_valid(VMM_PROT_READ) ||
	    !vmm_private_protection_valid(VMM_PROT_WRITE) ||
	    !vmm_private_protection_valid(VMM_PROT_READ | VMM_PROT_WRITE) ||
	    vmm_private_protection_valid(VMM_PROT_EXEC) ||
	    vmm_private_protection_valid(0x80U))
		return 1;
	if (!vmm_private_protection_range(0x600000001000ULL, 1,
	    &rounded, &end) || rounded != 4096 ||
	    end != 0x600000002000ULL)
		return 1;
	if (!vmm_private_protection_range(0x600000001000ULL, 4097,
	    &rounded, &end) || rounded != 8192 ||
	    end != 0x600000003000ULL)
		return 1;
	if (vmm_private_protection_range(0x600000001001ULL, 1,
	    &rounded, &end) ||
	    vmm_private_protection_range(0x600000001000ULL, 0,
	    &rounded, &end) ||
	    vmm_private_protection_range(0x600000001000ULL, UINT64_MAX,
	    &rounded, &end) ||
	    vmm_private_protection_range(UINT64_MAX & ~4095ULL, 4097,
	    &rounded, &end))
		return 1;
	puts("private protection flag and range helpers passed");
	return 0;
}
