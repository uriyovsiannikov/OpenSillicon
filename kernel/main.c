#include <stdint.h>
#include "limine.h"
#include "kernel/boot.h"
#include "kernel/boot_entropy.h"
#include "kernel/entropy.h"
#include "kernel/rdseed.h"
#include "kernel/virtio_rng.h"
#include "kernel/smp.h"
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
#include "kernel/devfs.h"
#include "kernel/procfs.h"
#include "kernel/block.h"
#include "kernel/block_silliconfs.h"
#include "kernel/silliconfs.h"
#include "kernel/tty.h"
#include "kernel/framebuffer_device.h"
#include "arch/x86_64/ahci.h"
#include "arch/x86_64/fpu.h"
#include "arch/x86_64/lapic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/ioapic.h"
#include "arch/x86_64/pci.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/ps2_keyboard.h"


#define MAIN_EEXIST 17
#define MAIN_ENOTDIR 20

static struct pci_device_info ahci_device;
static struct block_device ahci_block_device;
static struct block_silliconfs_adapter root_adapter;

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
	{
		const char *paths[] = { "/dev", "/proc" };
		const char *labels[] = { "devfs", "procfs" };
		int (*mount_functions[])(const char *) = { devfs_mount, procfs_mount };

		for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
			struct vnode *mountpoint = NULL;

			error = vfs_create_path(paths[i], VNODE_DIRECTORY, 0755,
			    &mountpoint);
			if (error == -MAIN_EEXIST) {
				error = vfs_lookup(paths[i], &mountpoint);
				if (error == 0 && mountpoint->type != VNODE_DIRECTORY) {
					vnode_put(mountpoint);
					mountpoint = NULL;
					error = -MAIN_ENOTDIR;
				}
			}
			vnode_put(mountpoint);
			if (error == 0)
				error = mount_functions[i](paths[i]);
			if (error == 0) {
				klog_status(labels[i], true, NULL);
			} else {
				klog_status(labels[i], false,
				    "mount failed; boot continues with mountpoint unchanged");
				klog("OpenSillicon: ");
				klog(labels[i]);
				klog(" error ");
				klog_u64((uint64_t)(-error));
				klog("\n");
			}
		}
	}
}

static uint64_t pci_device_count;
static bool ahci_controller_found;
static bool e1000_device_found;
#ifdef OPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG
static bool virtio_rng_device_found;
static bool virtio_rng_device_ambiguous;
static struct pci_device_info virtio_rng_pci_device;
#endif

static bool
record_pci_device(const struct pci_device_info *device, void *context)
{
	(void)context;
	pci_device_count++;
#ifdef OPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG
	if (virtio_rng_matches_device(device)) {
		if (virtio_rng_device_found)
			virtio_rng_device_ambiguous = true;
		else {
			virtio_rng_device_found = true;
			virtio_rng_pci_device = *device;
		}
	}
#endif
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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_entropy_request entropy_request = {
	.id = LIMINE_ENTROPY_REQUEST_ID,
	.revision = 0,
	.value_count = BOOT_ENTROPY_MAX_VALUES
};

/* Also applies to the initial stacks Limine gives its MP processors. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_stack_size_request stack_size_request = {
	.id = LIMINE_STACK_SIZE_REQUEST_ID,
	.revision = 0,
	.stack_size = 64U * 1024U
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_tsc_frequency_request tsc_frequency_request = {
	.id = LIMINE_TSC_FREQUENCY_REQUEST_ID,
	.revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
	.id = LIMINE_MP_REQUEST_ID,
	.revision = 0,
	.flags = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

static void
clear_boot_entropy_values(uint64_t values[BOOT_ENTROPY_MAX_VALUES])
{
	volatile uint64_t *current = values;
	for (size_t i = 0; i < BOOT_ENTROPY_MAX_VALUES; i++)
		current[i] = 0;
}

static void
report_smp_bootstrap(void)
{
	if (stack_size_request.response == NULL) {
		klog("OpenSillicon: SMP unavailable; Limine stack size request was not honored\n");
	} else if (tsc_frequency_request.response == NULL ||
	    tsc_frequency_request.response->frequency == 0) {
		klog("OpenSillicon: SMP unavailable; no Limine TSC frequency for startup timeout\n");
	} else if (mp_request.response != NULL &&
	    mp_request.response->cpu_count > SMP_MAX_CPUS) {
		klog("OpenSillicon: SMP topology rejected; discovered ");
		klog_u64(mp_request.response->cpu_count);
		klog(" exceeds maximum ");
		klog_u64(SMP_MAX_CPUS);
		klog("; continuing on BSP\n");
	} else if (mp_request.response != NULL && smp_init(mp_request.response,
	    tsc_frequency_request.response->frequency, timer_ap_initial_count())) {
		klog("OpenSillicon: SMP BSP initialized; CPUs discovered ");
		klog_u64(smp_discovered_count());
		klog(" online ");
		klog_u64(smp_online_count());
		klog("; APs waiting for kernel work and TLB shootdown NMIs\n");
		if (!smp_tlb_shootdown_all()) {
			klog("OpenSillicon panic: SMP TLB shootdown acknowledgement timed out\n");
			kernel_halt();
		}
		klog("OpenSillicon: SMP TLB shootdown acknowledged by all online APs\n");
#ifdef KERNEL_TEST_SMP_SCHEDULER
		if (!smp_verify_ap_idle_timers()) {
			klog("OpenSillicon panic: AP idle timer verification failed\n");
			kernel_halt();
		}
		klog("OpenSillicon: AP idle timer ticks verified\n");
#endif
#ifdef KERNEL_TEST_SMP_TLB_ROUNDS
		for (unsigned int round = 0; round < KERNEL_TEST_SMP_TLB_ROUNDS; round++) {
			if (!smp_tlb_shootdown_all()) {
				klog("OpenSillicon panic: repeated SMP TLB shootdown failed\n");
				kernel_halt();
			}
		}
		klog("OpenSillicon: repeated SMP TLB shootdowns acknowledged rounds ");
		klog_u64(KERNEL_TEST_SMP_TLB_ROUNDS);
		klog("\n");
#endif
		if (smp_online_count() == smp_discovered_count())
			klog("OpenSillicon: SMP bootstrap verified discovered ");
		else
			klog("OpenSillicon: SMP bootstrap partial discovered ");
		klog_u64(smp_discovered_count());
		klog(" online ");
		klog_u64(smp_online_count());
		klog("\n");
		for (size_t cpu_index = 0; cpu_index < smp_discovered_count();
		    cpu_index++) {
			const struct smp_cpu *cpu = smp_cpu_at(cpu_index);
			klog("OpenSillicon: SMP cpu logical ");
			klog_u64(cpu->logical_id);
			klog(" processor ");
			klog_u64(cpu->processor_id);
			klog(" bsp ");
			klog_u64(cpu->bsp ? 1 : 0);
			klog(" apic ");
			klog_u64(cpu->apic_id);
			klog(" state ");
			klog_u64(__atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE));
			klog(" stage ");
			klog_u64(__atomic_load_n(&cpu->stage, __ATOMIC_ACQUIRE));
			klog(" per_cpu ");
			klog_u64((uintptr_t)cpu);
			klog(" gdt ");
			klog_u64((uintptr_t)cpu->gdt);
			klog(" tss ");
			klog_u64((uintptr_t)cpu->tss);
			klog(" active_space ");
			klog_u64((uintptr_t)cpu->active_address_space);
			klog(" stack_top ");
			klog_u64(cpu->stack_top);
			klog(" idle_timer_ticks ");
			klog_u64(__atomic_load_n(&cpu->idle_timer_ticks,
			    __ATOMIC_ACQUIRE));
			klog("\n");
		}
	} else {
		klog("OpenSillicon: SMP unavailable or invalid Limine CPU topology; continuing on BSP\n");
	}
}

void
_start(void)
{
	struct boot_info info;
	struct bootstrap_allocator allocator;
	struct acpi_madt_info madt;
	struct limine_rsdp_response rsdp_response_copy;
	const struct limine_rsdp_response *rsdp_response;
	uint64_t entropy_values[BOOT_ENTROPY_MAX_VALUES];
	size_t entropy_value_count;
	enum boot_entropy_result entropy_result;
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
	entropy_result = boot_entropy_inspect(
	    (const struct limine_entropy_response *)(uintptr_t)
	    entropy_request.response, info.hhdm_offset, info.memmap,
	    entropy_values, &entropy_value_count);
	/* Limine explicitly classifies these values as best-effort. The bounded
	 * stack copy is handed to the provenance-preserving manager after the raw
	 * RDRAND provider has completed its own initialization. */
	if (entropy_result == BOOT_ENTROPY_AVAILABLE_UNTRUSTED ||
	    entropy_result == BOOT_ENTROPY_PARTIAL_UNTRUSTED)
		klog_status("Limine entropy response", true,
		    "best-effort values copied to bounded manager staging; untrusted");
	else if (entropy_result == BOOT_ENTROPY_MISSING ||
	    entropy_result == BOOT_ENTROPY_EMPTY)
		klog_status("Limine entropy response", false,
		    "not provided; no trusted entropy assumed");
	else
		klog_status("Limine entropy response", false,
		    "invalid response; ignored");
	(void)entropy_value_count;
	idt_init();
	if (!arch_fpu_init()) {
		klog("OpenSillicon: CPU lacks required FXSAVE/SSE/SSE2 support\n");
		kernel_halt();
	}
	if (kernel_random_init())
		klog_status("RDRAND instruction", true,
		    "available; not approved entropy for keys or TCP ISNs");
	else
		klog_status("RDRAND instruction", false,
		    "unsupported or temporarily unavailable; boot continues");
	if (kernel_rdseed_init())
		klog_status("RDSEED instruction", true,
		    "supported; bounded reads may be temporarily unavailable; no trust implied");
	else
		klog_status("RDSEED instruction", false,
		    "unsupported; boot continues without this candidate provider");
	if (kernel_entropy_init(entropy_result, entropy_values,
	    entropy_value_count)) {
		klog_status("kernel entropy manager", true,
		    "candidate sources tracked separately; cryptographic readiness disabled");
	} else {
		klog_status("kernel entropy manager", false,
		    "source inventory unavailable; boot continues fail closed");
	}
	clear_boot_entropy_values(entropy_values);
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
	if (!vmm_init(&info)) {
		klog_status("VMM", false, "virtual memory initialization failed");
		kernel_halt();
	}
	klog_status("VMM", true, NULL);
	if (!dma_init(&validated_dma_contract)) {
		klog_status("DMA subsystem", false,
		    "initialization failed");
		kernel_halt();
	}
	if (dma_identity_addressing_available()) {
		klog_status("DMA address policy", true,
		    "Limine base revision 5+, identity physical addressing; no IOMMU-preservation request");
	} else {
		klog_status("DMA address policy", false,
		    "unavailable: Limine base revision 5+ IOMMU-disabled contract not established; DMA-dependent drivers disabled");
	}
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
	report_smp_bootstrap();
	if (!kheap_init()) {
		klog_status("heap", false, "kernel heap initialization failed");
		kernel_halt();
	}
	klog_status("heap", true, NULL);
	if (!tty_init(info.framebuffer)) {
		klog_status("console", false, "terminal initialization failed");
		kernel_halt();
	}
	if (!framebuffer_device_init(info.framebuffer))
		klog("OpenSillicon: userspace framebuffer API unavailable\n");
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
#ifdef OPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG
	if (virtio_rng_device_ambiguous) {
		klog_status("VirtIO RNG", false,
		    "multiple supported transitional devices; provider disabled");
	} else if (virtio_rng_device_found) {
		uint32_t assigned_bar;
		enum virtio_rng_result result;
		if (!pci_virtio_rng_legacy_io_assign(&virtio_rng_pci_device,
		    0xc000U, &assigned_bar)) {
			klog_status("VirtIO RNG", false,
			    "QEMU legacy I/O BAR assignment failed; provider disabled");
			goto virtio_rng_probe_done;
		}
		virtio_rng_pci_device.bars[0] = assigned_bar;
		result = virtio_rng_init(&virtio_rng_pci_device);
		if (result != VIRTIO_RNG_OK)
			klog_status("VirtIO RNG", false,
			    "provider unavailable or bounded request failed; no entropy fallback");
	} else {
		klog_status("VirtIO RNG", false,
		    "supported transitional device absent; boot continues");
	}
virtio_rng_probe_done:
	if (!kernel_entropy_virtio_probe_complete(virtio_rng_device_found))
		klog_status("VirtIO RNG entropy status", false,
		    "manager unavailable or source already finalized");
	else if (kernel_entropy_source_state(
	    KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_SUPPORTED) {
		uint8_t sample[VIRTIO_RNG_MAX_READ];
		struct kernel_entropy_candidate candidate;
		enum kernel_entropy_result result =
		    kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
		    sample, sizeof(sample), &candidate);
		for (size_t i = 0; i < sizeof(sample); i++)
			((volatile uint8_t *)sample)[i] = 0;
		if (result == KERNEL_ENTROPY_OK) {
			klog_status("VirtIO RNG", true,
			    "QEMU transitional legacy PCI I/O; manager returned host-provided candidate only");
			klog("OpenSillicon: VirtIO RNG completion bytes ");
			klog_u64(candidate.bytes_written);
			klog("; bytes not credited as trusted entropy and not used for CSPRNG/ISN\n");
		} else {
			klog_status("VirtIO RNG", false,
			    "bounded manager read failed; provider not trusted");
		}
	}
#else
	klog_status("VirtIO RNG", false,
	    "runtime probing disabled outside the explicit QEMU profile");
#endif
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
					.netmask = {255, 255, 255, 0},
					.gateway = {10, 15, 3, 2}
				};
				if (udp_socket_system_init(network_device,
				    &network_config) != 0)
					klog("OpenSillicon: static UDP service unavailable; socket creation returns ENETDOWN\n");
				else if (!network_service_init())
					klog("OpenSillicon: deferred network service unavailable; syscall polling remains enabled\n");
				else {
					klog("OpenSillicon: static IPv4 profile 10.15.3.1/24 via 10.15.3.2 initialized; bounded deferred polling enabled\n");
					if (!tcp_runtime_init())
						klog("OpenSillicon: TCP core runtime owner unavailable; TCP remains disabled\n");
					else
						klog("OpenSillicon: inert TCP core table owner initialized; dispatch disabled\n");
				}
			}
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
	if (block_flush(&ahci_block_device) != 0) {
		klog("OpenSillicon: root block flush failed\n");
		kernel_halt();
	}
	if (!scheduler_init()) {
		klog_status("scheduler", false, "initialization failed");
		kernel_halt();
	}
	klog_status("scheduler", true, NULL);
	__asm__ volatile ("sti" : : : "memory");
	if (!scheduler_start_secondary_cpus()) {
		klog("OpenSillicon panic: secondary CPU scheduler initialization failed\n");
		kernel_halt();
	}
	if (smp_current_cpu() != NULL && smp_online_count() > 1)
		klog("OpenSillicon: AP cooperative kernel schedulers ready\n");
#ifdef KERNEL_TEST_SMP_SCHEDULER
	if (!scheduler_verify_cpu_queues()) {
		klog("OpenSillicon panic: CPU-local scheduler verification failed\n");
		kernel_halt();
	}
	klog("OpenSillicon: CPU-local scheduler threads, yields and reaping verified\n");
#endif
	if (smp_current_cpu() != NULL) {
		if (!smp_verify_work_dispatch()) {
			klog("OpenSillicon panic: online CPU work dispatch verification failed\n");
			kernel_halt();
		}
		klog("OpenSillicon: SMP kernel work dispatch verified on all online CPUs\n");
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
	klog("OpenSillicon: virtual memory and address spaces ready\n");
	klog("OpenSillicon: local APIC id ");
	klog_u64(lapic_id());
	klog("\nOpenSillicon: kernel heap ready\n");
	klog("OpenSillicon terminal ready\n");
	klog("OpenSillicon: launching /sbin/init from SilliconFS\n");
	klog_flush_boot_log();
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
