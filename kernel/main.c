#include <stdint.h>
#include "limine.h"
#include "kernel/boot.h"
#include "kernel/log.h"
#include "kernel/pmm.h"
#include "arch/x86_64/interrupts.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_firmware_type_request firmware_request = {
	.id = LIMINE_FIRMWARE_TYPE_REQUEST_ID
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST_ID
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST_ID
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

void
_start(void)
{
	struct boot_info info;
	struct bootstrap_allocator allocator;
	uint64_t test_page;
	enum boot_info_error error;

	serial_init();
	error = boot_info_parse(firmware_request.response, hhdm_request.response,
	    memmap_request.response, framebuffer_request.response, &info);
	if (error != BOOT_INFO_OK) {
		klog("OpenSillicon: invalid boot information, error ");
		klog_u64((uint64_t)error);
		klog("\n");
		kernel_halt();
	}
	idt_init();
#ifdef KERNEL_TEST_INTERRUPT
	__asm__ volatile ("int $128");
#endif
#ifdef KERNEL_TEST_PAGE_FAULT
	{
		volatile uintptr_t test_address = 0;
		__asm__ volatile ("" : "+r" (test_address));
		*(volatile uint64_t *)test_address = 1;
	}
#endif
	if (!bootstrap_allocator_init(&allocator, &info)) {
		klog("OpenSillicon: bootstrap allocator initialization failed\n");
		kernel_halt();
	}
	if (!pmm_init(&info, &allocator)) {
		klog("OpenSillicon: physical memory manager initialization failed\n");
		kernel_halt();
	}
	if (!pmm_alloc_page(&test_page) || !pmm_free_page(test_page)) {
		klog("OpenSillicon: physical page allocation smoke test failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: UEFI boot information parsed\n");
	klog("OpenSillicon: memory map entries ");
	klog_u64(info.memmap->entry_count);
	klog("\nOpenSillicon: HHDM offset ");
	klog_u64(info.hhdm_offset);
	klog("\n");
	if (info.framebuffer != NULL) {
		klog("OpenSillicon: framebuffer ");
		klog_u64(info.framebuffer->width);
		klog("x");
		klog_u64(info.framebuffer->height);
		klog("\n");
	} else {
		klog("OpenSillicon: no framebuffer available\n");
	}
	klog("OpenSillicon: physical frame slots ");
	klog_u64(pmm_total_pages());
	klog(" free ");
	klog_u64(pmm_free_pages());
	klog("\nOpenSillicon: physical memory manager ready\n");
	kernel_halt();
}
