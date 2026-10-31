#include <stdint.h>
#include "limine.h"
#include "kernel/boot.h"
#include "kernel/log.h"
#include "kernel/pmm.h"
#include "kernel/vmm.h"
#include "kernel/heap.h"
#include "kernel/timer.h"
#include "kernel/scheduler.h"
#include "kernel/fd.h"
#include "kernel/syscall.h"
#include "arch/x86_64/user.h"
#include "arch/x86_64/lapic.h"
#include "arch/x86_64/interrupts.h"

#define SCHEDULER_TEST_STEPS 4U
#define USER_TEST_CODE 0x400000ULL
#define USER_TEST_FAULT (USER_TEST_CODE + 0x400ULL)
#define USER_TEST_STACK (0x00007ffffffff000ULL - 0x1000ULL)
#define PAGE_SIZE 4096U

static void
run_user_test(void)
{
	struct address_space space;
	struct fd_table descriptors;
	uint64_t code_page;
	uint64_t stack_page;
	uint64_t image_size = (uintptr_t)_binary_user_test_bin_end -
	    (uintptr_t)_binary_user_test_bin_start;
	uint64_t status;
	uint64_t unmapped;
	uint8_t *code;
	uint64_t i;

	if (image_size == 0 || image_size > PAGE_SIZE ||
	    !pmm_alloc_page(&code_page) || !pmm_alloc_page(&stack_page) ||
	    !address_space_create(&space)) {
		klog("OpenSillicon: user test allocation failed\n");
		kernel_halt();
	}
	code = (uint8_t *)(uintptr_t)(vmm_hhdm_offset() + code_page);
	for (i = 0; i < image_size; i++)
		code[i] = _binary_user_test_bin_start[i];
	if (!vmm_map_page(&space, USER_TEST_CODE, code_page,
	    VMM_USER | VMM_EXEC) || !vmm_map_page(&space, USER_TEST_STACK,
	    stack_page, VMM_USER | VMM_WRITE) || !address_space_activate(&space)) {
		klog("OpenSillicon: user test mapping failed\n");
		kernel_halt();
	}
	fd_table_init_standard(&descriptors);
	syscall_init(&descriptors);
	status = user_enter(USER_TEST_CODE, USER_TEST_STACK + PAGE_SIZE);
	if (status != 0) {
		klog("OpenSillicon: user syscall test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	status = user_enter(USER_TEST_FAULT, USER_TEST_STACK + PAGE_SIZE);
	if (status != 0x10eU ||
	    !address_space_activate(vmm_kernel_address_space()) ||
	    !vmm_unmap_page(&space, USER_TEST_CODE, &unmapped) ||
	    unmapped != code_page || !vmm_unmap_page(&space, USER_TEST_STACK,
	    &unmapped) || unmapped != stack_page || !address_space_destroy(&space) ||
	    !pmm_free_page(code_page) || !pmm_free_page(stack_page)) {
		klog("OpenSillicon: user test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: Ring 3 isolation and syscall tests passed\n");
}

static volatile uint64_t scheduler_test_order[SCHEDULER_TEST_STEPS * 2];
static volatile uint64_t scheduler_test_count;
static volatile uint64_t scheduler_test_finished;

static void
scheduler_test_worker(void *argument)
{
	uint64_t worker = (uintptr_t)argument;
	unsigned int i;

	for (i = 0; i < SCHEDULER_TEST_STEPS; i++) {
		uint64_t index = scheduler_test_count;

		if (index < SCHEDULER_TEST_STEPS * 2)
			scheduler_test_order[index] = worker;
		scheduler_test_count = index + 1;
		thread_yield();
	}
	scheduler_test_finished++;
}

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
	uint64_t user_page;
	struct address_space user_space;
	void *heap_allocation;
	void *large_heap_allocation;
	void *coalesced_heap_allocation;
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
	if (!vmm_init(&info)) {
		klog("OpenSillicon: virtual memory initialization failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: VMM initialized\n");
#ifdef KERNEL_TEST_VMM_PROTECTION
	{
		struct address_space protected_space;
		uint64_t protected_page;
		volatile uint64_t *protected_address = (volatile uint64_t *)0x400000;

		if (!pmm_alloc_page(&protected_page) ||
		    !address_space_create(&protected_space) ||
		    !vmm_map_page(&protected_space, 0x400000, protected_page,
		    VMM_USER) || !address_space_activate(&protected_space)) {
			klog("OpenSillicon: VMM protection test setup failed\n");
			kernel_halt();
		}
		*protected_address = 1;
		klog("OpenSillicon: VMM protection test failed\n");
		kernel_halt();
	}
#endif
	if (!lapic_init()) {
		klog("OpenSillicon: local APIC initialization failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: LAPIC initialized\n");
	if (!timer_init()) {
		klog("OpenSillicon: LAPIC timer initialization failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: 100 Hz LAPIC timer initialized\n");
	if (!kheap_init()) {
		klog("OpenSillicon: kernel heap initialization failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: heap initialized\n");
	if (!pmm_alloc_page(&user_page) || !address_space_create(&user_space) ||
	    vmm_map_page(&user_space, 0x400000, user_page,
	    VMM_USER | VMM_WRITE | VMM_EXEC) ||
	    !vmm_map_page(&user_space, 0x400000, user_page, VMM_USER | VMM_WRITE) ||
	    !address_space_activate(&user_space)) {
		klog("OpenSillicon: address space setup failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: user address space activated\n");
	heap_allocation = kheap_alloc(128);
	if (heap_allocation == NULL) {
		klog("OpenSillicon: kernel heap allocation failed\n");
		kernel_halt();
	}
	large_heap_allocation = kheap_alloc(9000);
	if (large_heap_allocation == NULL) {
		klog("OpenSillicon: multi-page heap allocation failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: heap allocations succeeded\n");
	((volatile uint64_t *)heap_allocation)[0] = 0x4f70656e53696c6cULL;
	if (((volatile uint64_t *)heap_allocation)[0] != 0x4f70656e53696c6cULL ||
	    !kheap_free(heap_allocation) || kheap_free(heap_allocation) ||
	    !kheap_free(large_heap_allocation)) {
		klog("OpenSillicon: kernel heap smoke test failed\n");
		kernel_halt();
	}
	coalesced_heap_allocation = kheap_alloc(8000);
	if (coalesced_heap_allocation == NULL ||
	    !kheap_free(coalesced_heap_allocation)) {
		klog("OpenSillicon: kernel heap coalescing test failed\n");
		kernel_halt();
	}
	*(volatile uint64_t *)(uintptr_t)0x400000 = 0x564d4d7465737421ULL;
	if (*(volatile uint64_t *)(uintptr_t)0x400000 != 0x564d4d7465737421ULL ||
	    !vmm_unmap_page(&user_space, 0x400000, &test_page) ||
	    test_page != user_page || !address_space_activate(
	    vmm_kernel_address_space()) || !address_space_destroy(&user_space) ||
	    !pmm_free_page(user_page)) {
		klog("OpenSillicon: address space smoke test failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: address space destroyed\n");
	run_user_test();
	if (!scheduler_init() ||
	    !thread_create(scheduler_test_worker, (void *)(uintptr_t)0) ||
	    !thread_create(scheduler_test_worker, (void *)(uintptr_t)1)) {
		klog("OpenSillicon: scheduler initialization failed\n");
		kernel_halt();
	}
	__asm__ volatile ("sti" : : : "memory");
	while (scheduler_test_finished < 2)
		thread_yield();
	thread_yield();
	if (scheduler_test_count != SCHEDULER_TEST_STEPS * 2) {
		klog("OpenSillicon: scheduler lost a thread step\n");
		kernel_halt();
	}
	for (uint64_t i = 0; i < SCHEDULER_TEST_STEPS * 2; i++) {
		if (scheduler_test_order[i] != i % 2) {
			klog("OpenSillicon: Round Robin order test failed\n");
			kernel_halt();
		}
	}
	while (timer_ticks() < 5)
		__asm__ volatile ("hlt" : : : "memory");
	klog("OpenSillicon: Round Robin scheduler passed\n");
	klog("OpenSillicon: timer tick count ");
	klog_u64(timer_ticks());
	klog("\n");
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
	klog("OpenSillicon: virtual memory and address spaces ready\n");
	klog("OpenSillicon: local APIC id ");
	klog_u64(lapic_id());
	klog("\nOpenSillicon: kernel heap ready\n");
	kernel_halt();
}
