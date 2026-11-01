#include <stdint.h>
#include "limine.h"
#include "kernel/boot.h"
#include "kernel/log.h"
#include "kernel/errno.h"
#include "kernel/pmm.h"
#include "kernel/dma.h"
#include "kernel/e1000.h"
#include "kernel/e1000_profile.h"
#include "kernel/netbuf.h"
#include "kernel/netdev.h"
#include "kernel/netstack.h"
#include "kernel/network_service.h"
#include "kernel/random.h"
#include "kernel/tcp_runtime.h"
#include "kernel/udp_socket.h"
#include "kernel/vmm.h"
#include "kernel/heap.h"
#include "kernel/timer.h"
#include "kernel/scheduler.h"
#include "kernel/fd.h"
#include "kernel/syscall.h"
#include "kernel/elf.h"
#include "kernel/process.h"
#include "kernel/vfs.h"
#include "kernel/block.h"
#include "kernel/block_silliconfs.h"
#include "kernel/silliconfs.h"
#include "kernel/tty.h"
#include "arch/x86_64/ahci.h"
#include "arch/x86_64/user.h"
#include "arch/x86_64/fpu.h"
#include "arch/x86_64/lapic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/ioapic.h"
#include "arch/x86_64/pci.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/ps2_keyboard.h"

#ifdef KERNEL_TEST_FPU_CONTEXT
static void
run_fpu_context_test(void)
{
	uint64_t preemptions_before = process_user_preemptions();
	uint64_t status = process_run_path("/init/bin/fpu_context_test", false);

	if (status != 0 || process_user_preemptions() <= preemptions_before) {
		klog("OpenSillicon: Ring3 FPU/SSE context test failed, status ");
		klog_u64(status);
		klog(" preemptions ");
		klog_u64(process_user_preemptions() - preemptions_before);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: eager per-process FXSAVE/FXRSTOR context test passed\n");
}
#endif

#define SCHEDULER_TEST_STEPS 4U
#define USER_TEST_CODE 0x400000ULL
#define USER_TEST_FAULT (USER_TEST_CODE + 0x400ULL)
#define USER_TEST_STACK (0x00007ffffffff000ULL - 0x2000ULL)
#define USER_TEST_END 0x00007ffffffff000ULL
#define PAGE_SIZE 4096U
#define MAIN_EEXIST 17
#define MAIN_ENOTDIR 20

static struct fd_table user_descriptors;
static struct pci_device_info ahci_device;
static struct block_device ahci_block_device;
static struct block_silliconfs_adapter root_adapter;

#ifdef KERNEL_TEST_UDP_SOCKET
extern const uint8_t _binary_udp_socket_test_bin_start[];
extern const uint8_t _binary_udp_socket_test_bin_end[];

static void
run_udp_socket_test(void)
{
	uint64_t image_size = (uintptr_t)_binary_udp_socket_test_bin_end -
	    (uintptr_t)_binary_udp_socket_test_bin_start;
	uint64_t status;
	struct network_service_result service_result;

	status = process_run_elf(_binary_udp_socket_test_bin_start,
	    (size_t)image_size);
	if (!tcp_runtime_shutdown() ||
	    !network_service_request(NETWORK_SERVICE_POLL_RX) ||
	    udp_socket_system_shutdown() ||
	    !network_service_run(1, &service_result) ||
	    service_result.events_processed != 1 ||
	    !udp_socket_system_shutdown() || e1000_state() != E1000_STATE_STOPPED) {
		klog("OpenSillicon: UDP test could not confirm safe e1000 shutdown\n");
		kernel_halt();
	}
	if (status != 0) {
		klog("OpenSillicon: user UDP socket test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
#ifdef KERNEL_TEST_ICMP_PING
	klog("OpenSillicon: userspace ICMP ping passed; e1000 stopped\n");
#else
	klog("OpenSillicon: user UDP socket send/receive/close passed; e1000 stopped\n");
#endif
}
#endif

#ifdef KERNEL_TEST_E1000_ICMP_SMOKE
#define NETSTACK_TEST_POLLS 10000000U
#define NETSTACK_TEST_TX_POLLS 1000000U
#define NETSTACK_TEST_TIMEOUT_TICKS 500U

static bool
netstack_test_transmit(const uint8_t *frame, size_t length)
{
	struct net_device *device = e1000_network_device();
	struct netbuf_ref packet;
	uint8_t *buffer;
	bool sent;

	if (device == NULL || length > NETSTACK_MAX_FRAME ||
	    !netbuf_alloc(NETBUF_OWNER_CORE, &packet))
		return false;
	buffer = netbuf_data(packet, NETBUF_OWNER_CORE);
	if (buffer == NULL || length > netbuf_capacity(packet, NETBUF_OWNER_CORE)) {
		(void)netbuf_release(packet, NETBUF_OWNER_CORE);
		return false;
	}
	for (size_t i = 0; i < length; i++)
		buffer[i] = frame[i];
	if (!netbuf_set_length(packet, NETBUF_OWNER_CORE, length) ||
	    net_device_transmit(device, packet) != NET_DEVICE_OK) {
		if (netbuf_data(packet, NETBUF_OWNER_CORE) != NULL)
			(void)netbuf_release(packet, NETBUF_OWNER_CORE);
		return false;
	}
	sent = e1000_wait_transmit(packet, NETSTACK_TEST_TX_POLLS);
	if (sent)
		(void)netbuf_release(packet, NETBUF_OWNER_CORE);
	return sent;
}

static bool
netstack_test_receive(struct netstack *stack, enum netstack_event wanted,
	const uint8_t *expected_payload, size_t expected_payload_length)
{
	struct net_device *device = e1000_network_device();
	struct netbuf_ref packet[1];
	uint64_t start_tick = timer_ticks();

	for (uint32_t poll = 0; poll < NETSTACK_TEST_POLLS; poll++) {
		size_t count = 0;
		if (net_device_receive(device, 1, packet, 1, &count) != NET_DEVICE_OK)
			return false;
		if (count != 0) {
			uint8_t response[NETSTACK_MAX_FRAME];
			uint8_t *data = netbuf_data(packet[0], NETBUF_OWNER_CORE);
			size_t length = netbuf_length(packet[0], NETBUF_OWNER_CORE);
			size_t response_length = 0;
			enum netstack_event event = data == NULL ? NETSTACK_IGNORED :
			    netstack_process_frame(stack, data, length, timer_ticks(),
			    response, sizeof(response), &response_length, NULL);
			bool payload_matches = true;
			if (event == NETSTACK_ECHO_REPLY) {
				if (length < 42 + expected_payload_length || data[34] != 0 ||
				    data[38] != 0x15 || data[39] != 0x3a ||
			    data[40] != 0 || data[41] != 1 ||
			    data[6] != 0x52 || data[7] != 0x54 ||
			    data[8] != 0 || data[9] != 0x15 ||
			    data[10] != 3 || data[11] != 2 ||
			    data[26] != 10 || data[27] != 15 ||
			    data[28] != 3 || data[29] != 2 ||
			    data[30] != 10 || data[31] != 15 ||
			    data[32] != 3 || data[33] != 1)
					payload_matches = false;
				for (size_t i = 0; payload_matches &&
				    i < expected_payload_length; i++)
					if (data[42 + i] != expected_payload[i])
						payload_matches = false;
			}
			(void)netbuf_release(packet[0], NETBUF_OWNER_CORE);
			if (response_length != 0 &&
			    !netstack_test_transmit(response, response_length))
				return false;
			if (event == wanted && payload_matches) {
				return true;
			}
		}
		if (timer_ticks() - start_tick >= NETSTACK_TEST_TIMEOUT_TICKS)
			return false;
		__asm__ volatile ("pause" : : : "memory");
	}
	return false;
}

static bool
run_e1000_icmp_smoke_test(void)
{
	static const struct netstack_config config = {
		.mac = {0x52, 0x54, 0x00, 0x15, 0x03, 0x01},
		.address = {10, 15, 3, 1},
		.netmask = {255, 255, 255, 0}
	};
	static const uint8_t peer_ip[4] = {10, 15, 3, 2};
	static const uint8_t payload[] = "OS-PHASE15.3A-ECHO";
	struct netstack stack;
	uint8_t frame[NETSTACK_MAX_FRAME];
	uint8_t peer_mac[6];
	size_t length;
	bool resolved = false;

	if (!netstack_init(&stack, &config) ||
	    !netstack_build_arp_request(&stack, peer_ip, frame, sizeof(frame),
	    &length) || !netstack_test_transmit(frame, length))
		return false;
	klog("OpenSillicon: ARP request transmitted for 10.15.3.2\n");
	(void)netstack_test_receive(&stack, NETSTACK_ARP_RESOLVED, NULL, 0);
	resolved = netstack_arp_lookup(&stack, peer_ip, timer_ticks(), peer_mac);
	if (!resolved) {
		klog("OpenSillicon: ARP resolution timed out\n");
		return false;
	}
	klog("OpenSillicon: ARP peer resolved 10.15.3.2\n");
	if (!netstack_build_echo_request(&stack, peer_mac, peer_ip, 0x153a, 1,
	    payload, sizeof(payload) - 1, frame, sizeof(frame), &length) ||
	    !netstack_test_transmit(frame, length))
		return false;
	klog("OpenSillicon: ICMP Echo Request transmitted\n");
	if (!netstack_test_receive(&stack, NETSTACK_ECHO_REPLY, payload,
	    sizeof(payload) - 1)) {
		klog("OpenSillicon: ICMP Echo Reply timed out or mismatched\n");
		return false;
	}
	if (!e1000_shutdown())
		return false;
	klog("OpenSillicon: guest ARP/IPv4/ICMP smoke passed\n");
	return true;
}
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
extern uint8_t _binary_private_mapping_lifecycle_test_elf_start[];
extern uint8_t _binary_private_mapping_lifecycle_test_elf_end[];
#endif

static void
run_dma_allocation_smoke_test(void)
{
	struct dma_buffer buffer;
	struct dma_buffer_info info;
	uint64_t device_address;
	volatile uint8_t *bytes;

	if (!dma_alloc(5000, 8192, 32, &buffer) ||
	    !dma_buffer_query(buffer, &info) || info.page_count != 2 ||
	    info.allocated_size != 2 * PAGE_SIZE ||
	    (info.physical_address & (2 * PAGE_SIZE - 1)) != 0 ||
	    info.device_address != info.physical_address) {
		klog_status("DMA allocation smoke test", false,
		    "allocation, alignment, or identity address failed");
		kernel_halt();
	}
	bytes = info.cpu_virtual_address;
	for (size_t i = 0; i < info.allocated_size; i++) {
		if (bytes[i] != 0) {
			klog_status("DMA allocation smoke test", false,
			    "allocation was not zero-filled");
			kernel_halt();
		}
	}
	if (!dma_device_acquire(buffer, &device_address) ||
	    device_address != info.physical_address || dma_free(buffer) ||
	    !dma_device_complete(buffer) || !dma_free(buffer)) {
		klog_status("DMA allocation smoke test", false,
		    "device ownership lifecycle failed");
		kernel_halt();
	}
	klog_status("DMA allocation smoke test", true,
	    "contiguous, aligned, zero-filled, identity-addressed, ownership checked");
}

static void
run_mmio_memmap_smoke_test(const struct boot_info *info)
{
	bool reserved_range_found = false;
	bool reserved_range_accepted = false;
	bool usable_range_found = false;
	bool hhdm_range_rejected = false;

	for (uint64_t i = 0; i < info->memmap->entry_count; i++) {
		const struct limine_memmap_entry *entry = info->memmap->entries[i];
		uint64_t base;

		if (entry->base > UINT64_MAX - (PAGE_SIZE - 1) ||
		    entry->base > UINT64_MAX - entry->length)
			continue;
		base = (entry->base + PAGE_SIZE - 1) &
		    ~(uint64_t)(PAGE_SIZE - 1);
		if (entry->type == LIMINE_MEMMAP_RESERVED &&
		    base <= entry->base + entry->length &&
		    PAGE_SIZE <= entry->base + entry->length - base) {
			reserved_range_found = true;
			reserved_range_accepted =
			    vmm_physical_range_mmio_safe(base, PAGE_SIZE);
			if (!reserved_range_accepted)
				break;
		}
		if (!usable_range_found && entry->type == LIMINE_MEMMAP_USABLE &&
		    PAGE_SIZE <= entry->length) {
			usable_range_found = true;
			hhdm_range_rejected =
			    !vmm_physical_range_mmio_safe(entry->base, PAGE_SIZE);
		}
	}
	if (!reserved_range_found || !reserved_range_accepted ||
	    !usable_range_found || !hhdm_range_rejected) {
		klog_status("MMIO HHDM-alias check", false,
		    "memory-map range classification failed");
		kernel_halt();
	}
	klog_status("MMIO HHDM-alias check", true,
	    "reserved ranges accepted; usable HHDM RAM rejected");
}

static void
run_user_test(void)
{
	struct address_space space;
	uint64_t code_page;
	uint64_t stack_page;
	uint64_t data_page;
	uint64_t collision_page;
	uint64_t image_size = (uintptr_t)_binary_user_test_bin_end -
	    (uintptr_t)_binary_user_test_bin_start;
	uint64_t status;
	uint64_t unmapped;
	uint64_t free_pages = pmm_free_pages();
	uint8_t *code;
	uint64_t i;

	if (image_size == 0 || image_size > PAGE_SIZE ||
	    !pmm_alloc_page(&code_page) || !pmm_alloc_page(&stack_page) ||
	    !pmm_alloc_page(&data_page) || !pmm_alloc_page(&collision_page) ||
	    !address_space_create(&space)) {
		klog("OpenSillicon: user test allocation failed\n");
		kernel_halt();
	}
	code = (uint8_t *)(uintptr_t)(vmm_hhdm_offset() + code_page);
	for (i = 0; i < image_size; i++)
		code[i] = _binary_user_test_bin_start[i];
	*(volatile uint64_t *)(uintptr_t)(vmm_hhdm_offset() + data_page) = 0;
	if (!vmm_map_page(&space, USER_TEST_CODE, code_page,
	    VMM_USER | VMM_EXEC) || !vmm_map_page(&space, USER_TEST_STACK,
	    stack_page, VMM_USER | VMM_WRITE) ||
	    !vmm_map_page(&space, USER_TEST_CODE + PAGE_SIZE, data_page,
	    VMM_USER | VMM_WRITE) ||
	    !vmm_map_page(&space, SHM_ARENA_START + 2 * SHM_PAGE_SIZE,
	    collision_page, VMM_USER | VMM_WRITE) ||
	    !vmm_validate_user_exec(&space, USER_TEST_CODE, 1) ||
	    vmm_validate_user_exec(&space, USER_TEST_STACK, 1) ||
	    vmm_validate_user_exec(&space, USER_TEST_CODE + PAGE_SIZE - 1, 2) ||
	    vmm_validate_user_exec(&space, 0xffffffff80000000ULL, 1) ||
	    vmm_validate_user_exec(&space, USER_TEST_END - 1, 2) ||
	    !address_space_activate(&space)) {
		klog("OpenSillicon: user test mapping failed\n");
		kernel_halt();
	}
	fd_table_init_standard(&user_descriptors);
	syscall_init(&user_descriptors);
	status = user_enter(USER_TEST_CODE, USER_TEST_STACK + PAGE_SIZE);
	if (status != 0 || !vmm_unmap_page(&space,
	    SHM_ARENA_START + 2 * SHM_PAGE_SIZE, &unmapped) ||
	    unmapped != collision_page || !vmm_unmap_shared_object(&space,
	    SHM_ARENA_START + SHM_PAGE_SIZE) || !pmm_free_page(collision_page)) {
		klog("OpenSillicon: shared syscall failure rollback test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	{
		uint64_t phase = 1;

		if (!vmm_copy_to_user(&space, USER_TEST_CODE + PAGE_SIZE,
		    &phase, sizeof(phase))) {
			klog("OpenSillicon: shared syscall test phase setup failed\n");
			kernel_halt();
		}
	}
	status = user_enter(USER_TEST_CODE, USER_TEST_STACK + PAGE_SIZE);
	if (status != 0) {
		klog("OpenSillicon: shared syscall success test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	status = user_enter(USER_TEST_FAULT, USER_TEST_STACK + PAGE_SIZE);
	if (status != 0x10eU ||
	    !address_space_activate(vmm_kernel_address_space()) ||
	    !vmm_unmap_page(&space, USER_TEST_CODE, &unmapped) ||
	    unmapped != code_page ||
	    !vmm_unmap_page(&space, USER_TEST_CODE + PAGE_SIZE, &unmapped) ||
	    unmapped != data_page || !vmm_unmap_page(&space, USER_TEST_STACK,
	    &unmapped) || unmapped != stack_page || !address_space_destroy(&space) ||
	    !pmm_free_page(code_page) || !pmm_free_page(data_page) ||
	    !pmm_free_page(stack_page) || pmm_free_pages() != free_pages) {
		klog("OpenSillicon: user test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: Ring 3 and shared-memory syscall tests passed\n");
}

static void
run_vmm_shared_test(void)
{
	const uint64_t private_address = 0x500000ULL;
	const uint64_t shared_address = 0x600000ULL;
	const uint64_t initial_private = 0x5052495641544531ULL;
	const uint64_t changed_private = 0x5052495641544532ULL;
	const uint64_t shared_value = 0x5348415245443031ULL;
	const uint64_t changed_shared = 0x5348415245443032ULL;
	struct address_space parent;
	struct address_space child;
	struct shm_object *object;
	uint64_t private_page;
	uint64_t value;
	unsigned int step = 1;

	if (!pmm_alloc_page(&private_page) || !address_space_create(&parent) ||
	    !vmm_map_page(&parent, private_address, private_page,
	    VMM_USER | VMM_WRITE))
		goto fail;
	step++;
	if (!shm_object_create(1, &object) ||
	    !vmm_map_shared_object(&parent, shared_address, object))
		goto fail;
	step++;
	if (!vmm_copy_to_user(&parent, private_address, &initial_private,
	    sizeof(initial_private)) ||
	    !vmm_copy_to_user(&parent, shared_address, &shared_value,
	    sizeof(shared_value)) || address_space_destroy(&parent))
		goto fail;
	if (!vmm_validate_user_write(&parent, shared_address, sizeof(value)) ||
	    vmm_validate_user_exec(&parent, shared_address, 1))
		goto fail;
	step++;
	if (!address_space_clone(&parent, &child))
		goto fail;
	step++;
	if (!vmm_copy_to_user(&parent, private_address, &changed_private,
	    sizeof(changed_private)) ||
	    !vmm_copy_from_user(&child, &value, private_address, sizeof(value)) ||
	    value != initial_private)
		goto fail;
	step++;
	if (vmm_unmap_page(&child, shared_address, &private_page) ||
	    !vmm_validate_user_write(&child, shared_address, sizeof(value)) ||
	    vmm_validate_user_exec(&child, shared_address, 1) ||
	    !vmm_copy_to_user(&child, shared_address, &changed_shared,
	    sizeof(changed_shared)) ||
	    !vmm_copy_from_user(&parent, &value, shared_address, sizeof(value)) ||
	    value != changed_shared)
		goto fail;
	step++;
	if (!address_space_destroy_with_pages(&child))
		goto fail;
	step++;
	if (!vmm_copy_from_user(&parent, &value, shared_address, sizeof(value)) ||
	    value != changed_shared)
		goto fail;
	step++;
	if (!vmm_unmap_shared_object(&parent, shared_address))
		goto fail;
	step++;
	if (vmm_copy_from_user(&parent, &value, shared_address, sizeof(value)))
		goto fail;
	step++;
	if (!address_space_destroy_with_pages(&parent))
		goto fail;
	klog("OpenSillicon: shared VMM marking, clone, detach, and teardown passed\n");
	return;

fail:
	klog("OpenSillicon: shared VMM mapping test failed\n");
	klog_u64(step);
	klog("\n");
	kernel_halt();
}

static void
run_vmm_private_mapping_test(void)
{
	const uint64_t initial = 0x706172656e743031ULL;
	const uint64_t parent_update = 0x706172656e743032ULL;
	const uint64_t child_update = 0x6368696c643031ULL;
	struct address_space parent;
	struct address_space child;
	struct elf_loaded_image replacement;
	uint64_t base;
	uint64_t next;
	uint64_t fork_base;
	uint64_t value;
	uint8_t last_byte;
	uint64_t free_pages = pmm_free_pages();
	uint64_t image_size = (uintptr_t)_binary_elf_test_bin_end -
	    (uintptr_t)_binary_elf_test_bin_start;
	unsigned int step = 1;

	if (!address_space_create(&parent) ||
	    vmm_private_map(&parent, 0, &base) != -EINVAL ||
	    vmm_private_map(&parent, UINT64_MAX, &base) != -EINVAL ||
	    vmm_private_map(&parent, PRIVATE_MAP_USABLE_END -
	    PRIVATE_MAP_USABLE_START + PAGE_SIZE, &base) == 0 ||
	    vmm_private_unmap(&parent, PRIVATE_MAP_ARENA_START, PAGE_SIZE) !=
	    -EINVAL ||
	    vmm_map_page(&parent, PRIVATE_MAP_ARENA_START, 0,
	    VMM_USER | VMM_WRITE))
		goto fail;
	step++;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
	{
		uint64_t cursor = parent.private_map_cursor;
		uint64_t collision_base = 0;

		if (!vmm_test_install_private_collision(&parent) ||
		    vmm_private_map(&parent, PAGE_SIZE, &collision_base) != -ENOMEM ||
		    collision_base != 0 || parent.private_map_cursor != cursor ||
		    !vmm_test_remove_private_collision(&parent))
			goto fail;
	}
#endif
	if (vmm_private_map(&parent, PAGE_SIZE + 1, &base) != 0 ||
	    base != PRIVATE_MAP_USABLE_START || (base & (PAGE_SIZE - 1)) != 0 ||
	    !vmm_copy_from_user(&parent, &value, base, sizeof(value)) || value != 0 ||
	    !vmm_copy_from_user(&parent, &last_byte,
	    base + 2 * PAGE_SIZE - 1, sizeof(last_byte)) || last_byte != 0 ||
	    !vmm_validate_user_write(&parent, base, PAGE_SIZE + 1) ||
	    vmm_validate_user_exec(&parent, base, 1) ||
	    !vmm_copy_to_user(&parent, base, &initial, sizeof(initial)))
		goto fail;
	step++;
	if (vmm_private_map(&parent, PAGE_SIZE, &next) != 0 ||
	    next != base + 2 * PAGE_SIZE ||
	    vmm_private_unmap(&parent, base, PAGE_SIZE) != -EINVAL ||
	    vmm_private_unmap(&parent, base + 1, PAGE_SIZE + 1) != -EINVAL ||
	    vmm_private_unmap(&parent, base, PAGE_SIZE + 1) != 0 ||
	    vmm_private_unmap(&parent, base, PAGE_SIZE + 1) != -EINVAL ||
	    vmm_private_map(&parent, PAGE_SIZE, &fork_base) != 0 ||
	    fork_base <= next)
		goto fail;
	step++;
	if (!vmm_copy_to_user(&parent, fork_base, &initial, sizeof(initial)))
		goto fail;
	step = 41;
	if (!address_space_clone(&parent, &child))
		goto fail;
	step = 42;
	if (child.private_mappings == NULL || parent.private_mappings == NULL ||
	    child.private_mappings->physical_pages[0] ==
	    parent.private_mappings->physical_pages[0])
		goto fail;
	step++;
	if (!vmm_copy_to_user(&parent, fork_base, &parent_update,
	    sizeof(parent_update)) ||
	    !vmm_copy_from_user(&child, &value, fork_base, sizeof(value)) ||
	    value != initial ||
	    !vmm_copy_to_user(&child, fork_base, &child_update,
	    sizeof(child_update)) ||
	    !vmm_copy_from_user(&parent, &value, fork_base, sizeof(value)) ||
	    value != parent_update || !address_space_destroy_with_pages(&child))
		goto fail;
	step++;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
	{
		uint64_t failed_base = 0;
		uint64_t cursor = parent.private_map_cursor;

		vmm_test_arm_private_metadata_failure(1);
		if (vmm_private_map(&parent, PAGE_SIZE, &failed_base) != -ENOMEM ||
		    failed_base != 0 || parent.private_map_cursor != cursor)
			goto fail;
		vmm_test_arm_private_metadata_failure(2);
		if (vmm_private_map(&parent, PAGE_SIZE, &failed_base) != -ENOMEM ||
		    failed_base != 0 || parent.private_map_cursor != cursor)
			goto fail;
		vmm_test_arm_private_metadata_failure(3);
		if (vmm_private_map(&parent, PAGE_SIZE, &failed_base) != -ENOMEM ||
		    failed_base != 0 || parent.private_map_cursor != cursor)
			goto fail;
		vmm_test_arm_private_map_failure(1);
		if (vmm_private_map(&parent, 2 * PAGE_SIZE, &failed_base) != -ENOMEM ||
		    failed_base != 0 || parent.private_map_cursor != cursor)
			goto fail;
		if (vmm_private_map(&parent, PAGE_SIZE, &failed_base) != 0 ||
		    failed_base != cursor ||
		    vmm_private_unmap(&parent, failed_base, PAGE_SIZE) != 0)
			goto fail;
		vmm_test_arm_private_fork_failure(1);
		if (address_space_clone(&parent, &child) ||
		    !vmm_copy_from_user(&parent, &value, fork_base, sizeof(value)) ||
		    value != parent_update)
			goto fail;
	}
#endif
	if (elf_image_load(NULL, 0, &replacement) ||
	    !vmm_copy_from_user(&parent, &value, fork_base, sizeof(value)) ||
	    value != parent_update || image_size == 0 ||
	    !elf_image_load(_binary_elf_test_bin_start, (size_t)image_size,
	    &replacement) || !address_space_activate(&replacement.address_space) ||
	    !address_space_destroy_with_pages(&parent) ||
	    !address_space_activate(vmm_kernel_address_space()) ||
	    !elf_image_unload(&replacement) || pmm_free_pages() != free_pages)
		goto fail;
	klog("OpenSillicon: private mapping zero-fill, RW/NX, exact unmap, fork isolation, exec replacement, and teardown passed\n");
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
	klog("OpenSillicon: private mapping metadata, page, and fork rollback injection passed\n");
#endif
	return;

fail:
	klog("OpenSillicon: private mapping test failed at step ");
	klog_u64(step);
	klog("\n");
	kernel_halt();
}

#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
static void
install_private_fault_instruction(uint64_t physical, uint64_t target,
	bool write_access)
{
	volatile uint8_t *code = (volatile uint8_t *)(uintptr_t)(
	    vmm_hhdm_offset() + physical);
	uint64_t i;
	size_t offset = 0;

	code[offset++] = 0x48;
	code[offset++] = 0xb8;
	for (i = 0; i < sizeof(target); i++)
		code[offset++] = (uint8_t)(target >> (i * 8));
	if (write_access) {
		static const uint8_t write_code[] = {
			0x48, 0xc7, 0x00, 0x78, 0x56, 0x34, 0x12
		};

		for (i = 0; i < sizeof(write_code); i++)
			code[offset++] = write_code[i];
	} else {
		static const uint8_t read_code[] = {0x48, 0x8b, 0x00};

		for (i = 0; i < sizeof(read_code); i++)
			code[offset++] = read_code[i];
	}
	code[offset++] = 0x0f;
	code[offset] = 0x0b;
}

static bool
run_private_access_fault(struct address_space *space, uint64_t code_physical,
	uint64_t target, bool write_access, uint64_t expected_error)
{
	uint64_t vector;
	uint64_t error_code;
	uint64_t status;

	install_private_fault_instruction(code_physical, target, write_access);
	if (!address_space_activate(space))
		return false;
	process_test_arm_user_fault_capture();
	status = user_enter(USER_TEST_CODE, USER_TEST_STACK + PAGE_SIZE);
	if (!address_space_activate(vmm_kernel_address_space()) || status != 0x10eU ||
	    !process_test_user_fault_capture(&vector, &error_code))
		return false;
	return vector == 14 && error_code == expected_error;
}

static void
run_vmm_private_protection_test(void)
{
	const uint64_t first_value = 0x50524f545f303031ULL;
	const uint64_t second_value = 0x50524f545f303032ULL;
	const uint64_t neighbor_value = 0x50524f545f4e4549ULL;
	const uint64_t child_value = 0x4348494c445f3031ULL;
	struct address_space parent;
	struct address_space child;
	uint64_t base;
	uint64_t neighbor;
	uint64_t parent_frames[3];
	uint64_t child_frames[3];
	uint64_t code_physical;
	uint64_t stack_physical;
	uint64_t value;
	unsigned int step = 1;
	size_t i;

	if (!address_space_create(&parent) ||
	    vmm_private_map(&parent, 2 * PAGE_SIZE, &base) != 0 ||
	    vmm_private_map(&parent, PAGE_SIZE, &neighbor) != 0 ||
	    neighbor != base + 2 * PAGE_SIZE ||
	    !vmm_copy_to_user(&parent, base, &first_value, sizeof(first_value)) ||
	    !vmm_copy_to_user(&parent, base + PAGE_SIZE, &second_value,
	    sizeof(second_value)) ||
	    !vmm_copy_to_user(&parent, neighbor, &neighbor_value,
	    sizeof(neighbor_value)))
		goto fail;
	parent_frames[0] = parent.private_mappings->physical_pages[0];
	parent_frames[1] = parent.private_mappings->physical_pages[1];
	parent_frames[2] = parent.private_mappings->next->physical_pages[0];
	step++;
	if (vmm_private_protect(&parent, base, PAGE_SIZE + 1,
	    VMM_PROT_READ) != 0 ||
	    vmm_validate_user_write(&parent, base, 2 * PAGE_SIZE) ||
	    !vmm_copy_from_user(&parent, &value, base, sizeof(value)) ||
	    value != first_value || vmm_validate_user_exec(&parent, base,
	    2 * PAGE_SIZE) || parent.private_mappings->page_protections[0] !=
	    VMM_PROT_READ || parent.private_mappings->page_protections[1] !=
	    VMM_PROT_READ ||
	    vmm_private_protect(&parent, base, PAGE_SIZE, VMM_PROT_WRITE) != 0 ||
	    !vmm_validate_user_write(&parent, base, PAGE_SIZE) ||
	    !vmm_copy_to_user(&parent, base, &first_value, sizeof(first_value)))
		goto fail;
	step++;
	if (vmm_private_protect(&parent, base, PAGE_SIZE, VMM_PROT_NONE) != 0 ||
	    vmm_validate_user_write(&parent, base, 1) ||
	    vmm_copy_from_user(&parent, &value, base, sizeof(value)) ||
	    !pmm_test_page_is_allocated(parent_frames[0]) ||
	    parent.private_mappings->physical_pages[0] != parent_frames[0] ||
	    parent.private_mappings->page_protections[0] != VMM_PROT_NONE ||
	    vmm_validate_user_exec(&parent, base, 1))
		goto fail;
	step++;
	if (vmm_private_protect(&parent, base, 0, VMM_PROT_READ) != -EINVAL ||
	    vmm_private_protect(&parent, base + 1, 1, VMM_PROT_READ) != -EINVAL ||
	    vmm_private_protect(&parent, base, 3 * PAGE_SIZE,
	    VMM_PROT_NONE) != -EINVAL ||
	    vmm_private_protect(&parent, UINT64_MAX & ~4095ULL, 4097,
	    VMM_PROT_READ) != -EINVAL ||
	    vmm_private_protect(&parent, PRIVATE_MAP_USABLE_END - PAGE_SIZE,
	    PAGE_SIZE, VMM_PROT_READ) != -EINVAL ||
	    vmm_private_protect(&parent, base, PAGE_SIZE, VMM_PROT_EXEC) !=
	    -EOPNOTSUPP ||
	    vmm_private_protect(&parent, base, PAGE_SIZE,
	    VMM_PROT_READ | 0x80U) != -EINVAL ||
	    parent.private_mappings->page_protections[0] != VMM_PROT_NONE ||
	    parent.private_mappings->page_protections[1] != VMM_PROT_READ ||
	    !vmm_validate_user_write(&parent, neighbor, sizeof(value)))
		goto fail;
	step++;
	if (!pmm_alloc_page(&code_physical) || !pmm_alloc_page(&stack_physical) ||
	    !vmm_map_page(&parent, USER_TEST_CODE, code_physical,
	    VMM_USER | VMM_EXEC) ||
	    !vmm_map_page(&parent, USER_TEST_STACK, stack_physical,
	    VMM_USER | VMM_WRITE) ||
	    !run_private_access_fault(&parent, code_physical, base, false, 0x5U) ||
	    !run_private_access_fault(&parent, code_physical, base, true, 0x7U))
		goto fail;
	step++;
	if (vmm_private_protect(&parent, base, PAGE_SIZE, VMM_PROT_READ) != 0 ||
	    !vmm_copy_from_user(&parent, &value, base, sizeof(value)) ||
	    value != first_value || vmm_validate_user_write(&parent, base, 1) ||
	    vmm_validate_user_exec(&parent, base, 1) ||
	    !run_private_access_fault(&parent, code_physical, base, true, 0x7U) ||
	    vmm_private_protect(&parent, base, PAGE_SIZE, VMM_PROT_READ |
	    VMM_PROT_WRITE) != 0 ||
	    !vmm_copy_from_user(&parent, &value, base, sizeof(value)) ||
	    value != first_value)
		goto fail;
	step++;
	if (vmm_private_protect(&parent, base + PAGE_SIZE, 1,
	    VMM_PROT_READ) != 0 ||
	    !vmm_validate_user_write(&parent, base, PAGE_SIZE) ||
	    vmm_validate_user_write(&parent, base + PAGE_SIZE, 1) ||
	    !vmm_copy_from_user(&parent, &value, base + PAGE_SIZE, sizeof(value)) ||
	    value != second_value ||
	    vmm_private_protect(&parent, base, PAGE_SIZE, VMM_PROT_NONE) != 0 ||
	    !address_space_clone(&parent, &child))
		goto fail;
	step++;
	if (parent.private_mappings->page_protections[0] != VMM_PROT_NONE ||
	    child.private_mappings->page_protections[0] != VMM_PROT_NONE ||
	    parent.private_mappings->page_protections[1] != VMM_PROT_READ ||
	    child.private_mappings->page_protections[1] != VMM_PROT_READ)
		goto fail;
	for (i = 0; i < 3; i++) {
		parent_frames[i] = i < 2 ? parent.private_mappings->physical_pages[i] :
		    parent.private_mappings->next->physical_pages[0];
		child_frames[i] = i < 2 ? child.private_mappings->physical_pages[i] :
		    child.private_mappings->next->physical_pages[0];
		if (parent_frames[i] == child_frames[i])
			goto fail;
	}
	if (vmm_validate_user_write(&parent, base, 1) ||
	    vmm_validate_user_write(&child, base, 1) ||
	    !pmm_test_page_is_allocated(child_frames[0]))
		goto fail;
	if (vmm_private_protect(&child, base, PAGE_SIZE, VMM_PROT_WRITE) != 0 ||
	    !vmm_validate_user_write(&child, base, 1) ||
	    vmm_validate_user_write(&parent, base, 1) ||
	    !vmm_copy_from_user(&child, &value, base, sizeof(value)) ||
	    value != first_value ||
	    !vmm_copy_to_user(&child, base, &child_value, sizeof(child_value)))
		goto fail;
	if (vmm_private_protect(&parent, base + PAGE_SIZE, PAGE_SIZE,
	    VMM_PROT_WRITE) != 0 ||
	    !vmm_validate_user_write(&parent, base + PAGE_SIZE, 1) ||
	    vmm_validate_user_write(&child, base + PAGE_SIZE, 1))
		goto fail;
	if (vmm_private_protect(&child, base + PAGE_SIZE, PAGE_SIZE,
	    VMM_PROT_NONE) != 0 ||
	    !vmm_validate_user_write(&parent, base + PAGE_SIZE, 1) ||
	    vmm_validate_user_write(&child, base + PAGE_SIZE, 1) ||
	    !vmm_copy_from_user(&child, &value, base, sizeof(value)) ||
	    value != child_value ||
	    vmm_private_protect(&parent, base, PAGE_SIZE, VMM_PROT_READ) != 0 ||
	    !vmm_copy_from_user(&parent, &value, base, sizeof(value)) ||
	    value != first_value)
		goto fail;
	step++;
	if (!address_space_activate(vmm_kernel_address_space()) ||
	    !address_space_destroy_with_pages(&child) ||
	    !address_space_destroy_with_pages(&parent))
		goto fail;
	for (i = 0; i < 3; i++) {
		if (pmm_test_page_is_allocated(parent_frames[i]) ||
		    pmm_test_page_is_allocated(child_frames[i]))
			goto fail;
	}
	if (pmm_test_page_is_allocated(code_physical) ||
	    pmm_test_page_is_allocated(stack_physical))
		goto fail;
	klog("OpenSillicon: private protection modes, range checks, RW/NX faults, fork isolation, and teardown passed\n");
	return;

fail:
	klog("OpenSillicon: private protection test failed at step ");
	klog_u64(step);
	klog("\n");
	kernel_halt();
}
#endif

#ifdef KERNEL_TEST_PRIVATE_MAPPING_EXHAUSTION
static void
run_private_mapping_exhaustion_test(void)
{
	struct address_space space;
	uint64_t neighbor;
	uint64_t cursor;
	uint64_t free_before;
	uint64_t free_after_failure;
	uint64_t request_pages;
	uint64_t failed_base = 0;
	uint64_t retry_base = 0;
	uint64_t value = 0x504d4d5f4e454947ULL;
	uint64_t observed = 0;
	uint64_t scratch_size;
	void *scratch;

	if (!address_space_create(&space) ||
	    vmm_private_map(&space, PAGE_SIZE, &neighbor) != 0 ||
	    !vmm_copy_to_user(&space, neighbor, &value, sizeof(value)))
		goto fail;
	if (pmm_free_pages() > (UINT64_MAX - 65536U) / sizeof(uint64_t))
		goto fail;
	scratch_size = pmm_free_pages() * sizeof(uint64_t) + 65536U;
	scratch = kheap_alloc((size_t)scratch_size);
	if (scratch == NULL || !kheap_free(scratch))
		goto fail;
	free_before = pmm_free_pages();
	cursor = space.private_map_cursor;
	if (free_before == 0 || free_before >=
	    (PRIVATE_MAP_USABLE_END - cursor) / PAGE_SIZE ||
	    free_before == UINT64_MAX)
		goto fail;
	request_pages = free_before + 1;
	if (request_pages > UINT64_MAX / PAGE_SIZE)
		goto fail;
	pmm_test_reset_exhaustion_failures();
	if (vmm_private_map(&space, request_pages * PAGE_SIZE,
	    &failed_base) != -ENOMEM || failed_base != 0 ||
	    pmm_test_exhaustion_failures() == 0 ||
	    pmm_free_pages() != free_before || space.private_map_cursor != cursor ||
	    space.private_mappings == NULL ||
	    space.private_mappings->base != neighbor ||
	    space.private_mappings->next != NULL ||
	    vmm_validate_user_write(&space, cursor, 1) ||
	    !vmm_copy_from_user(&space, &observed, neighbor, sizeof(observed)) ||
	    observed != value)
		goto fail;
	free_after_failure = pmm_free_pages();
	if (vmm_private_map(&space, 2 * PAGE_SIZE, &retry_base) != 0 ||
	    retry_base != cursor ||
	    !vmm_validate_user_write(&space, retry_base, 2 * PAGE_SIZE) ||
	    vmm_private_unmap(&space, retry_base, 2 * PAGE_SIZE) != 0 ||
	    vmm_private_unmap(&space, neighbor, PAGE_SIZE) != 0 ||
	    !address_space_destroy_with_pages(&space))
		goto fail;
	klog("OpenSillicon: actual PMM exhaustion rollback and retry passed; free pages before/after failure ");
	klog_u64(free_before);
	klog(" -> ");
	klog_u64(free_after_failure);
	klog("; after teardown ");
	klog_u64(pmm_free_pages());
	klog("\n");
	return;

fail:
	klog("OpenSillicon: actual PMM exhaustion test failed\n");
	kernel_halt();
}
#endif

#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
static void
run_private_mapping_lifecycle_test(void)
{
	uint64_t size = (uintptr_t)_binary_private_mapping_lifecycle_test_elf_end -
	    (uintptr_t)_binary_private_mapping_lifecycle_test_elf_start;
	uint64_t status = process_test_private_mapping_lifecycle(
	    _binary_private_mapping_lifecycle_test_elf_start, (size_t)size);

	if (status != 0) {
		klog("OpenSillicon: private mapping NX exit/reap test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: private mapping NX instruction fault, zombie retention, and reap cleanup passed\n");
}
#endif

static void
run_elf_test(void)
{
	uint64_t image_size = (uintptr_t)_binary_elf_test_bin_end -
	    (uintptr_t)_binary_elf_test_bin_start;
	uint64_t status;

	status = process_run_elf(_binary_elf_test_bin_start, (size_t)image_size);
	if (status != 0 || process_user_preemptions() == 0) {
		klog("OpenSillicon: ELF process smoke test failed, status ");
		klog_u64(status);
		klog(" preemptions ");
		klog_u64(process_user_preemptions());
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: user timer preemptions ");
	klog_u64(process_user_preemptions());
	klog("\n");
	klog("OpenSillicon: ELF64 process, fork, wait, and cleanup passed\n");
}

static void
run_vfs_test(void)
{
	uint64_t image_size = (uintptr_t)_binary_vfs_test_bin_end -
	    (uintptr_t)_binary_vfs_test_bin_start;
	uint64_t status = process_run_elf(_binary_vfs_test_bin_start,
	    (size_t)image_size);

	if (status != 0) {
		klog("OpenSillicon: VFS userspace test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: VFS userspace operations passed\n");
}

static void
run_pipe_test(void)
{
	uint64_t status = process_run_path("/init/bin/pipe_test", false);

	if (status != 0) {
		klog("OpenSillicon: anonymous pipe userspace test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	if (process_pipe_blocked_ticks() == 0) {
		klog("OpenSillicon: pipe wait did not block through timer ticks\n");
		kernel_halt();
	}
	klog("OpenSillicon: pipe blocked timer ticks ");
	klog_u64(process_pipe_blocked_ticks());
	klog("\n");
	klog("OpenSillicon: anonymous pipe fork, exec, and blocking tests passed\n");
}

static void
run_signal_test(void)
{
	uint64_t status = process_run_path("/init/bin/signal_test", false);

	if (status != 0) {
		klog("OpenSillicon: user signal handler test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: signal delivery, sigreturn, and blocked syscall "
	    "interruption passed\n");
}

static void
run_shm_lifecycle_test(void)
{
	uint64_t status = process_run_path("/init/bin/shm_lifecycle_test", false);

	if (status != 0) {
		klog("OpenSillicon: shared-memory lifecycle test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
#ifdef KERNEL_TEST_SHM_LIFECYCLE
	if (shm_test_live_pages() != 0 ||
	    process_test_shm_reap_releases() == 0 ||
	    process_test_shm_exec_releases() == 0) {
		klog("OpenSillicon: shared-memory lifecycle accounting failed\n");
		kernel_halt();
	}
#endif
	klog("OpenSillicon: shared-memory process lifecycle passed\n");
}

#ifdef KERNEL_TEST_NATIVE_MUSL
static void
run_native_musl_test(void)
{
	uint64_t status = process_run_path("/bin/musl_hello", true);

	if (status != 0) {
		klog("OpenSillicon: native musl executable failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: native musl 1.2.6 CRT, TLS, errno, write, and exit passed\n");
}
#endif

#ifdef KERNEL_TEST_NATIVE_MUSL_ALLOC
static void
run_native_musl_alloc_test(void)
{
	uint64_t status = process_run_path("/bin/musl_alloc", true);

	if (status != 0) {
		klog("OpenSillicon: native musl allocation executable failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: native musl malloc, mmap, mprotect, munmap, TLS, and exit passed\n");
}
#endif

#ifdef KERNEL_TEST_NATIVE_MUSL_STDIO
static void
run_native_musl_stdio_test(void)
{
	uint64_t status = process_run_path("/bin/musl_stdio", true);

	if (status != 0) {
		klog("OpenSillicon: native musl stdio executable failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: native musl stdio QEMU fixture exited with status zero\n");
}
#endif

#ifdef KERNEL_TEST_SHM_FORK_FAILURE
static void
run_shm_fork_failure_test(void)
{
	uint64_t live_before = shm_test_live_pages();
	uint64_t status;

	vmm_test_arm_shm_fork_failure();
	status = process_run_path("/init/bin/shm_fork_failure_test", false);
	if (status != 0 || shm_test_live_pages() != live_before) {
		klog("OpenSillicon: shared fork rollback test failed, status ");
		klog_u64(status);
		klog("\n");
		kernel_halt();
	}
	klog("OpenSillicon: shared fork mapping rollback and reference cleanup passed\n");
}
#endif

static void
initialize_filesystems(const struct boot_info *info)
{
	struct vnode *ramfs_root;
	struct vnode *initramfs_root;
	struct vnode *created;
	int error;

	if (!vfs_init()) {
		klog_status("VFS", false, "VFS initialization failed");
		kernel_halt();
	}
	ramfs_root = ramfs_create_root();
	if (ramfs_root == NULL || vfs_mount("/", ramfs_root) != 0) {
		klog_status("RAMFS", false, "root mount failed");
		kernel_halt();
	}
	vnode_put(ramfs_root);
	klog_status("RAMFS", true, NULL);
	error = vfs_create_path("/init", VNODE_DIRECTORY, 0755, &created);
	if (error == 0)
		vnode_put(created);
	if (error != 0 || initramfs_mount(info->initramfs->address,
	    (size_t)info->initramfs->size, &initramfs_root) != 0 ||
	    vfs_mount("/init", initramfs_root) != 0) {
		klog_status("initramfs", false, "mount failed");
		kernel_halt();
	}
	vnode_put(initramfs_root);
	klog_status("initramfs", true, NULL);
	klog("OpenSillicon: initramfs and RAMFS mounted\n");
}

static void
switch_root_to_silliconfs(void)
{
	struct vnode *disk_root;
	struct vnode *temporary_directory;
	int error;

	if (!dma_identity_addressing_available()) {
		klog_status("AHCI", false,
		    "disabled: required Limine base revision 5+ DMA contract is unavailable; persistent disk root cannot be mounted");
		kernel_halt();
	}
	if (!ahci_initialize(&ahci_device, &ahci_block_device) ||
	    !block_silliconfs_init(&root_adapter, &ahci_block_device)) {
		klog_status("AHCI", false, "disk initialization failed");
		kernel_halt();
	}
	klog("OpenSillicon: AHCI logical sector size ");
	klog_u64(ahci_block_device.logical_sector_size);
	klog(" sectors ");
	klog_u64(ahci_block_device.sector_count);
	klog("\n");
	klog_status("AHCI", true, NULL);
	error = silliconfs_mount(&root_adapter.filesystem, &disk_root);
	if (error != 0) {
		klog_status("SilliconFS", false, "root mount failed");
		klog("OpenSillicon: SilliconFS error ");
		klog_u64((uint64_t)(-error));
		klog("\n");
		kernel_halt();
	}
	error = vfs_replace_root(disk_root);
	vnode_put(disk_root);
	if (error != 0) {
		klog_status("SilliconFS", false, "root switch failed");
		kernel_halt();
	}
	error = vfs_create_path("/tmp", VNODE_DIRECTORY, 01777,
	    &temporary_directory);
	if (error == -MAIN_EEXIST) {
		error = vfs_lookup("/tmp", &temporary_directory);
		if (error == 0 && temporary_directory->type != VNODE_DIRECTORY) {
			vnode_put(temporary_directory);
			error = -MAIN_ENOTDIR;
		}
	}
	if (error != 0) {
		klog_status("SilliconFS", false, "/tmp setup failed");
		klog("OpenSillicon: SilliconFS /tmp error ");
		klog_u64((uint64_t)(-error));
		klog("\n");
		kernel_halt();
	}
	vnode_put(temporary_directory);
	klog_status("SilliconFS", true, NULL);
	klog("OpenSillicon: SilliconFS mounted as /; initramfs retained at /init\n");
}

static volatile uint64_t scheduler_test_order[SCHEDULER_TEST_STEPS * 2];
static volatile uint64_t scheduler_test_count;
static volatile uint64_t scheduler_test_finished;
static uint64_t pci_device_count;
static bool ahci_controller_found;
static bool e1000_device_found;

static bool
record_pci_device(const struct pci_device_info *device, void *context)
{
	(void)context;
	pci_device_count++;
	if (device->class_code == 0x02 && device->subclass == 0x00) {
		if (device->vendor_id == E1000_QEMU_VENDOR_ID &&
		    device->device_id == E1000_QEMU_DEVICE_ID) {
			if (e1000_device_found) {
				(void)e1000_discover(device);
				klog("OpenSillicon: duplicate supported e1000 profile found\n");
			} else {
				e1000_device_found = true;
				(void)e1000_discover(device);
			}
		} else if (device->vendor_id == E1000_QEMU_VENDOR_ID) {
			klog("OpenSillicon: unsupported Intel network device ID ");
			klog_u64(device->device_id);
			klog("; e1000 initialization skipped\n");
		}
	}
	if (device->class_code == 0x01 && device->subclass == 0x06 &&
	    device->programming_interface == 0x01 && !ahci_controller_found) {
		ahci_device = *device;
		ahci_controller_found = true;
	}
	return true;
}

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
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(5);

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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
	.id = LIMINE_MODULE_REQUEST_ID,
	.revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST_ID,
	.revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

void
_start(void)
{
	struct boot_info info;
	struct bootstrap_allocator allocator;
	struct acpi_madt_info madt;
	struct limine_rsdp_response rsdp_response_copy;
	const struct limine_rsdp_response *rsdp_response;
	uint64_t test_page;
	uint64_t user_page;
	struct address_space user_space;
	void *heap_allocation;
	void *large_heap_allocation;
	void *coalesced_heap_allocation;
	enum boot_info_error error;
	uint64_t loaded_base_revision;
	bool revision_five_honored;
	struct dma_boot_contract validated_dma_contract;

	serial_init();
	loaded_base_revision = LIMINE_LOADED_BASE_REVISION(limine_base_revision);
	revision_five_honored =
	    LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
	klog("OpenSillicon: Limine loaded base revision ");
	klog_u64(loaded_base_revision);
	klog(revision_five_honored ? " (requested revision honored)\n" :
	    " (requested revision not honored)\n");
	if (!LIMINE_LOADED_BASE_REVISION_VALID(limine_base_revision) ||
	    loaded_base_revision < 4) {
		klog("OpenSillicon: Limine base revision 4 or newer is required for boot memory and ACPI contracts\n");
		kernel_halt();
	}
	validated_dma_contract = (struct dma_boot_contract){
		.requested_revision_honored = revision_five_honored,
		.loaded_base_revision = loaded_base_revision,
		/* No LIMINE_X86_64_KEEP_IOMMU request is declared by this kernel. */
		.keep_iommu_requested = false
	};
	rsdp_response = rsdp_request.response;
	if (hhdm_request.response != NULL && rsdp_response != NULL &&
	    rsdp_response->address != NULL) {
		uint64_t rsdp_hhdm = (uintptr_t)rsdp_response->address;
		uint64_t offset = hhdm_request.response->offset;

		rsdp_response_copy = *rsdp_response;
		if (rsdp_hhdm < offset)
			rsdp_response_copy.address = NULL;
		else
			rsdp_response_copy.address =
			    (void *)(uintptr_t)(rsdp_hhdm - offset);
		rsdp_response = &rsdp_response_copy;
	}
	error = boot_info_parse(firmware_request.response, hhdm_request.response,
	    memmap_request.response, framebuffer_request.response,
	    module_request.response, rsdp_response, &info);
	if (error != BOOT_INFO_OK) {
		klog("OpenSillicon: invalid boot information, error ");
		klog_u64((uint64_t)error);
		klog("\n");
		kernel_halt();
	}
	idt_init();
	if (!arch_fpu_init()) {
		klog("OpenSillicon: CPU lacks required FXSAVE/SSE/SSE2 support\n");
		kernel_halt();
	}
	if (kernel_random_init())
		klog_status("kernel random source", true,
		    "CPUID RDRAND hardware DRBG available");
	else
		klog_status("kernel random source", false,
		    "unavailable; secure random requests fail closed");
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
		klog_status("bootstrap allocator", false, "initialization failed");
		kernel_halt();
	}
	klog_status("bootstrap allocator", true, NULL);
	if (!pmm_init(&info, &allocator)) {
		klog_status("PMM", false, "physical memory initialization failed");
		kernel_halt();
	}
	klog_status("PMM", true, NULL);
	if (!pmm_alloc_page(&test_page) || !pmm_free_page(test_page)) {
		klog("OpenSillicon: physical page allocation smoke test failed\n");
		kernel_halt();
	}
	if (!vmm_init(&info)) {
		klog_status("VMM", false, "virtual memory initialization failed");
		kernel_halt();
	}
	klog_status("VMM", true, NULL);
	run_mmio_memmap_smoke_test(&info);
	if (!dma_init(&validated_dma_contract)) {
		klog_status("DMA subsystem", false,
		    "initialization failed");
		kernel_halt();
	}
	if (dma_identity_addressing_available()) {
		klog_status("DMA address policy", true,
		    "Limine base revision 5+, identity physical addressing; no IOMMU-preservation request");
		run_dma_allocation_smoke_test();
	} else {
		klog_status("DMA address policy", false,
		    "unavailable: Limine base revision 5+ IOMMU-disabled contract not established; DMA-dependent drivers disabled");
	}
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
		klog_status("LAPIC", false, "local APIC initialization failed");
		kernel_halt();
	}
	klog_status("LAPIC", true, NULL);
	if (!timer_init()) {
		klog_status("scheduler timer", false, "LAPIC timer initialization failed");
		kernel_halt();
	}
	klog_status("scheduler timer", true, NULL);
	if (!kheap_init()) {
		klog_status("heap", false, "kernel heap initialization failed");
		kernel_halt();
	}
	klog_status("heap", true, NULL);
	if (!tty_init(info.framebuffer)) {
		klog_status("console", false, "terminal initialization failed");
		kernel_halt();
	}
	klog_status("console", true, NULL);
	if (tty_framebuffer_available())
		klog_status("framebuffer renderer", true, NULL);
	else
		klog_status("framebuffer renderer", false,
		    "unavailable; serial console active");
	if (tty_keyboard_available())
		klog_status("PS/2 keyboard", true, NULL);
	else {
		klog_status("PS/2 keyboard", false, "initialization failed");
		klog("OpenSillicon: PS/2 keyboard diagnostic: ");
		switch (ps2_keyboard_last_error()) {
		case PS2_KEYBOARD_CONTROLLER_TIMEOUT:
			klog("i8042 controller timeout\n");
			break;
		case PS2_KEYBOARD_CONFIG_READ_TIMEOUT:
			klog("controller configuration read timeout\n");
			break;
		case PS2_KEYBOARD_CONFIG_WRITE_TIMEOUT:
			klog("controller configuration write timeout\n");
			break;
		case PS2_KEYBOARD_PORT_ENABLE_FAILED:
			klog("keyboard port enable timeout\n");
			break;
		case PS2_KEYBOARD_SCAN_ENABLE_FAILED:
			klog("keyboard did not acknowledge scan enable\n");
			break;
		case PS2_KEYBOARD_OK:
		default:
			klog("initialization failed\n");
			break;
		}
	}
	klog("OpenSillicon: Limine RSDP address ");
	klog_u64(info.rsdp_physical);
	klog("\n");
	if (!acpi_init(info.rsdp_physical, &madt)) {
		klog_status("ACPI", false, "initialization or MADT validation failed");
		kernel_halt();
	}
	if (!ioapic_init(&madt)) {
		klog_status("IOAPIC", false, "initialization failed");
		kernel_halt();
	}
	klog_status("ACPI and IOAPIC", true, NULL);
	if (!pci_enumerate(record_pci_device, NULL)) {
		klog_status("PCI", false, "device enumeration failed");
		kernel_halt();
	}
	klog_status("PCI", true, NULL);
	klog("OpenSillicon: PCI functions enumerated ");
	klog_u64(pci_device_count);
	klog(" AHCI ");
	klog(ahci_controller_found ? "found\n" : "not found\n");
	if (e1000_device_found) {
		struct net_device *network_device = e1000_network_device();
		bool network_started = netbuf_pool_init() && network_device != NULL &&
		    net_device_start(network_device) == NET_DEVICE_OK;

		if (network_started && e1000_initialize()) {
			klog_status("QEMU e1000 82540EM", true,
			    "8086:100e rings initialized; identity DMA; bus mastering enabled; interrupts disabled");
			{
				struct netstack_config network_config = {
					.mac = {0x52, 0x54, 0x00, 0x15, 0x03, 0x01},
					.address = {10, 15, 3, 1},
					.netmask = {255, 255, 255, 0}
				};
				if (udp_socket_system_init(network_device,
				    &network_config) != 0)
					klog("OpenSillicon: static UDP service unavailable; socket creation returns ENETDOWN\n");
				else if (!network_service_init())
					klog("OpenSillicon: deferred network service unavailable; syscall polling remains enabled\n");
				else {
					klog("OpenSillicon: static IPv4 UDP profile 10.15.3.1/24 initialized; bounded deferred polling enabled\n");
					if (!tcp_runtime_init())
						klog("OpenSillicon: TCP core runtime owner unavailable; TCP remains disabled\n");
					else
						klog("OpenSillicon: inert TCP core table owner initialized; dispatch disabled\n");
				}
			}
			#ifdef KERNEL_TEST_E1000_SMOKE
			if (!e1000_run_qemu_smoke_test()) {
				klog("OpenSillicon: guest e1000 TX/RX smoke failed\n");
				kernel_halt();
			}
			#endif
			#ifdef KERNEL_TEST_E1000_ICMP_SMOKE
			if (!run_e1000_icmp_smoke_test()) {
				klog("OpenSillicon: guest ARP/IPv4/ICMP smoke failed\n");
				kernel_halt();
			}
			#endif
		} else {
			klog("OpenSillicon: e1000 initialization failed, state ");
			klog_u64(e1000_state());
			klog(" reason ");
			klog_u64(e1000_last_failure());
			klog("; device left disabled where PCI config access permits\n");
		}
	} else {
		klog_status("QEMU e1000 82540EM", false,
		    "supported 8086:100e profile not found; no default NIC assumed");
	}
	if (!ahci_controller_found)
		klog_status("AHCI", false, "no supported controller found");
	if (!ahci_controller_found)
		kernel_halt();
	process_system_init();
	klog_status("process subsystem", true, NULL);
	initialize_filesystems(&info);
	klog_status("VFS", true, NULL);
	switch_root_to_silliconfs();
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
	run_vmm_shared_test();
	run_vmm_private_mapping_test();
#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
	run_vmm_private_protection_test();
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_EXHAUSTION
	run_private_mapping_exhaustion_test();
#endif
	run_user_test();
	run_elf_test();
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	run_private_mapping_lifecycle_test();
#endif
	run_vfs_test();
	run_pipe_test();
	run_signal_test();
#ifdef KERNEL_TEST_FPU_CONTEXT
	run_fpu_context_test();
#endif
	run_shm_lifecycle_test();
#ifdef KERNEL_TEST_SHM_FORK_FAILURE
	run_shm_fork_failure_test();
#endif
	if (block_flush(&ahci_block_device) != 0) {
		klog("OpenSillicon: root block flush failed\n");
		kernel_halt();
	}
#ifdef KERNEL_TEST_NATIVE_MUSL
	run_native_musl_test();
#endif
#ifdef KERNEL_TEST_NATIVE_MUSL_ALLOC
	run_native_musl_alloc_test();
#endif
#ifdef KERNEL_TEST_NATIVE_MUSL_STDIO
	run_native_musl_stdio_test();
#endif
#ifdef KERNEL_TEST_UDP_SOCKET
	run_udp_socket_test();
#endif
	if (!scheduler_init()) {
		klog_status("scheduler", false, "initialization failed");
		kernel_halt();
	}
	klog_status("scheduler", true, NULL);
	if (!thread_create(scheduler_test_worker, (void *)(uintptr_t)0) ||
	    !thread_create(scheduler_test_worker, (void *)(uintptr_t)1)) {
		klog("OpenSillicon: scheduler test worker creation failed\n");
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
	klog("OpenSillicon terminal ready\n");
	klog("OpenSillicon: launching /sbin/init from SilliconFS\n");
	fd_console_framebuffer_enable(true);
	{
		uint64_t status = process_run_path("/sbin/init", true);

		if (status == UINT64_MAX)
			klog("OpenSillicon: /sbin/init missing or failed to load; halting\n");
		else {
			klog("OpenSillicon: /sbin/init exited with status ");
			klog_u64(status);
			klog("; halting\n");
		}
	}
	kernel_halt();
}
