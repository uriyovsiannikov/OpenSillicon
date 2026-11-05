# Direct subsystem invocation re-enters the root graph so all source paths and
# cross-subsystem artifact dependencies have one canonical interpretation.
ifndef OPEN_SILLICON_ROOT
ROOT_DIR := $(abspath $(CURDIR)/..)
.DEFAULT_GOAL := __dispatch
.PHONY: __dispatch $(MAKECMDGOALS)
ifneq ($(strip $(MAKECMDGOALS)),)
$(MAKECMDGOALS): __dispatch
endif
__dispatch:
	+$(MAKE) -C $(ROOT_DIR) -f Makefile $(if $(strip $(MAKECMDGOALS)),$(MAKECMDGOALS),kernel)
else
all: kernel

kernel: $(BUILD_DIR)/kernel.elf

.PHONY: cyclone-config-check
cyclone-config-check: $(BUILD_DIR)/cyclone-config-check.o \
	$(BUILD_DIR)/cyclone-net.o

CYCLONE_STACK_SOURCES := core/net.c core/net_mem.c core/net_misc.c core/nic.c \
	core/ethernet.c core/ethernet_misc.c core/ip.c core/socket.c \
	core/socket_misc.c core/raw_socket.c core/ping.c core/tcp.c core/tcp_fsm.c core/tcp_misc.c \
	core/tcp_timer.c core/udp.c ipv4/ipv4.c ipv4/ipv4_misc.c \
	ipv4/arp.c ipv4/arp_cache.c ipv4/icmp.c ipv4/ipv4_multicast.c \
	dhcp/dhcp_common.c dhcp/dhcp_client.c dhcp/dhcp_client_fsm.c \
	dhcp/dhcp_client_misc.c
CYCLONE_STACK_OBJECTS := $(addprefix $(BUILD_DIR)/cyclone/,$(CYCLONE_STACK_SOURCES:.c=.o)) \
	$(BUILD_DIR)/cyclone/common/cpu_endian.o

.PHONY: cyclone-stack-objects
cyclone-stack-objects: $(CYCLONE_STACK_OBJECTS) \
	$(BUILD_DIR)/cyclone/cyclone_net.o


$(BUILD_DIR)/cyclone/cyclone_net.o: src/kernel/net/cyclonetcp/opensillicon/cyclone_net.c \
		src/include/kernel/cyclone_net.h \
		src/kernel/net/cyclonetcp/opensillicon/net_config.h \
		src/kernel/net/cyclonetcp/opensillicon/os_port_config.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CYCLONE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/cyclone/%.o: $(CYCLONE_TCP_ROOT)/%.c \
		$(CYCLONE_PORT_ROOT)/net_config.h \
		$(CYCLONE_PORT_ROOT)/os_port_config.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CYCLONE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/cyclone/common/cpu_endian.o: $(CYCLONE_COMMON_ROOT)/cpu_endian.c \
		$(CYCLONE_PORT_ROOT)/os_port_config.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CYCLONE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/cyclone-config-check.o: src/kernel/net/cyclonetcp/opensillicon/config_check.c \
		src/kernel/net/cyclonetcp/opensillicon/os_port_config.h \
		src/kernel/net/cyclonetcp/opensillicon/net_config.h | $(BUILD_DIR)
	$(CC) $(CYCLONE_CFLAGS) -fsyntax-only $<
	$(CC) $(CYCLONE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/cyclone-net.o: $(CYCLONE_TCP_ROOT)/core/net.c \
		$(CYCLONE_PORT_ROOT)/net_config.h \
		$(CYCLONE_PORT_ROOT)/os_port_config.h | $(BUILD_DIR)
	$(CC) $(CYCLONE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: src/kernel/main.c src/include/kernel/boot.h src/include/kernel/log.h \
		src/include/kernel/pmm.h src/include/kernel/vmm.h src/include/kernel/heap.h \
		src/include/kernel/errno.h \
		src/include/kernel/timer.h src/include/kernel/scheduler.h src/include/kernel/smp.h \
		src/include/arch/x86_64/lapic.h src/include/arch/x86_64/interrupts.h \
		src/include/arch/x86_64/user.h src/include/kernel/fd.h src/include/kernel/syscall.h \
		src/include/kernel/elf.h src/include/kernel/module.h src/include/kernel/process.h src/include/kernel/vfs.h \
		src/include/arch/x86_64/acpi.h src/include/arch/x86_64/ioapic.h \
		src/include/arch/x86_64/pci.h \
		src/include/kernel/dma.h \
		src/include/kernel/e1000.h src/include/kernel/e1000_profile.h \
		src/include/kernel/udp_socket.h \
		src/include/kernel/network_service.h \
		src/include/kernel/framebuffer_device.h src/include/abi/framebuffer.h \
	src/include/kernel/random.h src/include/kernel/boot_entropy.h \
	src/include/kernel/entropy.h \
	src/include/kernel/rdseed.h \
	src/include/kernel/virtio_rng.h \
	src/include/kernel/smp.h \
		src/include/arch/x86_64/ahci.h src/include/kernel/block.h \
		src/include/kernel/block_silliconfs.h src/include/kernel/silliconfs.h \
		src/include/kernel/devfs.h src/include/kernel/procfs.h \
		src/include/arch/x86_64/fpu.h \
		src/include/kernel/tty.h \
		src/include/kernel/framebuffer_device.h \
		src/limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/boot.o: src/kernel/core/boot/boot.c src/include/kernel/boot.h src/limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/boot_entropy.o: src/kernel/core/boot/boot_entropy.c \
		src/include/kernel/boot_entropy.h src/limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/entropy.o: src/kernel/random/entropy.c src/include/kernel/entropy.h \
		src/include/kernel/random.h src/include/kernel/rdseed.h \
		src/include/kernel/virtio_rng.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/rdseed.o: src/kernel/drivers/entropy/rdseed.c src/include/kernel/rdseed.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/virtio_rng.o: src/kernel/drivers/entropy/virtio_rng.c \
		src/include/kernel/virtio_rng.h src/include/kernel/dma.h \
		src/include/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/pmm.o: src/kernel/mm/pmm.c src/include/kernel/pmm.h \
		src/include/kernel/boot.h src/include/kernel/spinlock.h src/limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vmm.o: src/kernel/mm/vmm.c src/include/kernel/vmm.h \
		src/include/arch/x86_64/copy.h \
		src/include/kernel/pmm.h src/include/kernel/boot.h src/include/kernel/log.h \
		src/include/kernel/shm.h src/include/kernel/smp.h src/include/kernel/spinlock.h \
		src/limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/dma.o: src/kernel/mm/dma.c src/include/kernel/dma.h \
		src/include/kernel/pmm.h src/include/kernel/vmm.h src/include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/dma_ring.o: src/kernel/mm/dma_ring.c src/include/kernel/dma_ring.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/shm.o: src/kernel/mm/shm.c src/include/kernel/shm.h \
		src/include/kernel/vmm.h src/include/kernel/pmm.h src/include/kernel/heap.h \
		src/include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/heap.o: src/kernel/mm/heap.c src/include/kernel/heap.h \
		src/include/kernel/vmm.h src/include/kernel/pmm.h \
		src/include/kernel/spinlock.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/log.o: src/kernel/core/log/log.c src/include/kernel/log.h src/include/kernel/tty.h \
		src/include/kernel/framebuffer.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/fd.o: src/kernel/proc/fd.c src/include/kernel/fd.h src/include/kernel/log.h \
		src/include/kernel/heap.h src/include/kernel/vfs.h src/include/kernel/tty.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pipe.o: src/kernel/ipc/pipe.c src/include/kernel/pipe.h \
		src/include/kernel/process.h src/include/kernel/fd.h src/include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c src/kernel/ipc/pipe.c -o $@

$(BUILD_DIR)/pipe_buffer.o: src/kernel/ipc/pipe_buffer.c src/include/kernel/pipe.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/unix_socket.o: src/kernel/ipc/unix_socket.c \
		src/include/kernel/unix_socket.h src/include/kernel/fd.h \
		src/include/kernel/process.h src/include/kernel/heap.h \
		src/include/abi/unix_socket.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/terminal.o: src/kernel/tty/terminal.c src/include/kernel/terminal.h \
		src/include/kernel/fd.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/keyboard.o: src/kernel/tty/keyboard.c \
		src/include/arch/x86_64/ps2_keyboard.h src/include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/framebuffer.o: src/kernel/tty/framebuffer.c \
		src/include/kernel/framebuffer.h src/include/kernel/terminal.h \
		src/kernel/tty/font.h src/kernel/tty/font/gallant.go | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/framebuffer_device.o: src/kernel/tty/framebuffer_device.c \
		src/include/kernel/framebuffer_device.h src/include/abi/framebuffer.h \
		src/include/kernel/vmm.h src/limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tty.o: src/kernel/tty/tty.c src/include/kernel/tty.h \
		src/include/kernel/terminal.h src/include/kernel/framebuffer.h \
		src/include/arch/x86_64/ps2_keyboard.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ps2_keyboard.o: src/kernel/drivers/input/ps2_keyboard.c \
		src/include/arch/x86_64/ps2_keyboard.h src/include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/string.o: src/kernel/core/lib/string.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vfs.o: src/kernel/fs/vfs/vfs.c src/include/kernel/vfs.h \
		src/include/kernel/fd.h src/include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/devfs.o: src/kernel/fs/devfs/devfs.c src/include/kernel/devfs.h \
		src/include/kernel/vfs.h src/include/kernel/fd.h src/include/kernel/heap.h \
		src/include/kernel/tty.h src/include/kernel/log.h src/include/kernel/random.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/procfs.o: src/kernel/fs/procfs/procfs.c src/include/kernel/procfs.h \
		src/include/kernel/vfs.h src/include/kernel/process.h src/include/kernel/timer.h \
		src/include/kernel/heap.h src/include/kernel/pmm.h src/include/kernel/module.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ramfs.o: src/kernel/fs/ramfs/ramfs.c src/include/kernel/vfs.h \
		src/include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/initramfs.o: src/kernel/fs/initramfs/initramfs.c src/include/kernel/vfs.h \
		src/include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/silliconfs.o: src/kernel/fs/silliconfs/silliconfs.c \
		src/include/kernel/silliconfs.h src/include/kernel/vfs.h \
		src/include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/syscall.o: src/kernel/core/syscall/syscall.c src/include/kernel/syscall.h \
		src/include/abi/syscall.h src/include/abi/select.h \
		src/include/kernel/fd.h src/include/kernel/vmm.h src/include/kernel/log.h \
		src/include/kernel/process.h src/include/kernel/vfs.h src/include/kernel/udp_socket.h \
		src/include/kernel/tcp_socket.h \
		src/include/kernel/module.h src/include/kernel/heap.h \
		src/include/kernel/icmp_echo.h src/include/kernel/unix_socket.h \
		src/include/kernel/network_service.h \
		src/include/kernel/cyclone_net.h \
		src/include/kernel/framebuffer_device.h src/include/abi/framebuffer.h \
		src/include/abi/input.h src/include/arch/x86_64/ps2_keyboard.h \
		src/include/arch/x86_64/user.h src/include/arch/x86_64/interrupts.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/elf.o: src/kernel/proc/elf.c src/include/kernel/elf.h src/include/kernel/vmm.h \
		src/include/kernel/heap.h src/include/kernel/pmm.h src/include/kernel/shm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/module.o: src/kernel/module/module.c src/include/kernel/module.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/module_loader.o: src/kernel/module/loader.c src/include/kernel/module.h \
		src/include/kernel/vmm.h src/include/kernel/pmm.h src/include/kernel/heap.h \
		src/include/kernel/spinlock.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/process.o: src/kernel/proc/process.c src/include/kernel/process.h \
		src/include/kernel/syscall.h src/include/abi/syscall.h \
		src/include/kernel/signal_state.h src/include/kernel/elf.h src/include/kernel/vmm.h src/include/kernel/heap.h \
		src/include/kernel/pmm.h src/include/kernel/fd.h src/include/kernel/timer.h \
		src/include/kernel/smp.h \
		src/include/kernel/network_service.h \
		src/include/arch/x86_64/gdt.h src/include/arch/x86_64/fpu.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/signal_state.o: src/kernel/proc/signal_state.c \
		src/include/kernel/signal_state.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/interrupts.o: src/kernel/arch/x86_64/interrupts/interrupts.c \
		src/include/arch/x86_64/gdt.h src/include/arch/x86_64/interrupts.h \
		src/include/kernel/log.h src/include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt.o: src/kernel/arch/x86_64/cpu/gdt.c src/include/arch/x86_64/gdt.h \
		src/include/kernel/smp.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/smp.o: src/kernel/arch/x86_64/cpu/smp.c src/include/kernel/smp.h \
		src/include/arch/x86_64/interrupts.h src/include/arch/x86_64/lapic.h \
		src/include/kernel/spinlock.h src/include/kernel/log.h src/include/kernel/vmm.h \
		src/include/kernel/scheduler.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -Ilimine -c $< -o $@

$(BUILD_DIR)/smp_topology.o: src/kernel/arch/x86_64/cpu/smp_topology.c \
		src/include/kernel/smp.h src/limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -Ilimine -c $< -o $@

$(BUILD_DIR)/smp_asm.o: src/kernel/arch/x86_64/cpu/smp.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/fpu.o: src/kernel/arch/x86_64/cpu/fpu.c \
		src/include/arch/x86_64/fpu.h src/include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/random.o: src/kernel/random/random.c src/include/kernel/random.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/lapic.o: src/kernel/arch/x86_64/controllers/lapic.c \
		src/include/arch/x86_64/lapic.h src/include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ioapic.o: src/kernel/arch/x86_64/controllers/ioapic.c \
		src/include/arch/x86_64/ioapic.h src/include/arch/x86_64/acpi.h \
		src/include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pci.o: src/kernel/drivers/bus/pci/pci.c \
		src/include/arch/x86_64/pci.h src/include/arch/x86_64/acpi.h \
		src/include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pci_resource.o: src/kernel/drivers/bus/pci/pci_resource.c \
		src/include/arch/x86_64/pci_resource.h src/include/arch/x86_64/pci.h \
		src/include/kernel/vmm.h src/include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netbuf.o: src/kernel/net/core/netbuf.c src/include/kernel/netbuf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netdev.o: src/kernel/net/core/netdev.c src/include/kernel/netdev.h \
		src/include/kernel/netbuf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/udp_socket.o: src/kernel/net/udp/udp_socket.c \
		src/include/kernel/udp_socket.h \
		src/include/kernel/cyclone_net.h \
		src/include/kernel/netdev.h src/include/kernel/netbuf.h \
		src/include/kernel/fd.h src/include/kernel/timer.h src/include/kernel/errno.h \
		src/include/kernel/network_service.h src/include/kernel/icmp_echo.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tcp_socket.o: src/kernel/net/tcp/tcp_socket.c \
		src/include/kernel/tcp_socket.h src/include/kernel/cyclone_net.h \
		src/include/kernel/errno.h src/include/kernel/fd.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/network_service.o: src/kernel/net/service/network_service.c \
		src/include/kernel/network_service.h src/include/kernel/udp_socket.h \
		src/include/kernel/errno.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_profile.o: src/kernel/drivers/net/e1000/e1000_profile.c \
		src/include/kernel/e1000_profile.h src/include/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_ring.o: src/kernel/drivers/net/e1000/e1000_ring.c \
		src/include/kernel/e1000_ring.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000.o: src/kernel/drivers/net/e1000/e1000.c src/include/kernel/e1000.h \
		src/include/kernel/e1000_profile.h src/include/arch/x86_64/pci_resource.h \
		src/include/kernel/e1000_ring.h src/include/kernel/dma.h \
		src/include/kernel/dma_ring.h src/include/kernel/netdev.h \
		src/include/kernel/netbuf.h src/include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_reset.o: src/kernel/drivers/net/e1000/e1000_reset.c src/include/kernel/e1000.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ahci.o: src/kernel/drivers/storage/ahci.c \
		src/include/arch/x86_64/ahci.h src/include/arch/x86_64/pci.h \
		src/include/kernel/block.h src/include/kernel/errno.h src/include/kernel/dma.h src/include/kernel/pmm.h \
		src/include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block.o: src/kernel/fs/block/block.c src/include/kernel/block.h \
		src/include/kernel/errno.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block_silliconfs.o: src/kernel/fs/block/block_silliconfs.c \
		src/include/kernel/block_silliconfs.h src/include/kernel/block.h \
		src/include/kernel/errno.h src/include/kernel/silliconfs.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/acpi.o: src/kernel/arch/x86_64/firmware/acpi.c \
		src/include/arch/x86_64/acpi.h src/include/kernel/heap.h \
		src/include/kernel/log.h src/include/kernel/timer.h src/include/kernel/vmm.h \
		src/kernel/arch/x86_64/firmware/acpica/source/include/acpi.h \
		src/kernel/arch/x86_64/firmware/acpica/source/include/acpiosxf.h \
		src/kernel/arch/x86_64/firmware/acpica/source/include/acpixf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL \
		-isystem src/kernel/arch/x86_64/firmware/acpica/source/include -Iinclude -c $< -o $@

$(BUILD_DIR)/timer.o: src/kernel/arch/x86_64/timer/timer.c \
		src/include/arch/x86_64/lapic.h src/include/arch/x86_64/interrupts.h \
		src/include/kernel/timer.h src/include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/scheduler.o: src/kernel/sched/scheduler.c \
		src/include/kernel/scheduler.h src/include/kernel/heap.h src/include/kernel/vmm.h \
		src/include/kernel/smp.h src/include/kernel/spinlock.h \
		src/include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt_asm.o: src/kernel/arch/x86_64/cpu/gdt.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts_asm.o: src/kernel/arch/x86_64/interrupts/interrupts.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/context.o: src/kernel/arch/x86_64/context/context.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry.o: src/kernel/arch/x86_64/context/user_entry.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry_c.o: src/kernel/arch/x86_64/context/user_entry.c \
		src/include/arch/x86_64/user.h src/include/kernel/smp.h src/include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/acpica/%.o: $(ACPICA_ROOT)/%.c \
		$(ACPICA_ROOT)/include/platform/acenv.h \
		$(ACPICA_ROOT)/include/platform/acopensillicon.h \
		$(ACPICA_ROOT)/include/ctype.h
	mkdir -p $(dir $@)
	$(CC) $(ACPICA_CFLAGS) -c $< -o $@

acpica-check: $(ACPICA_OBJECTS)

KERNEL_OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/boot.o $(BUILD_DIR)/boot_entropy.o $(BUILD_DIR)/entropy.o $(BUILD_DIR)/random.o $(BUILD_DIR)/rdseed.o $(BUILD_DIR)/pmm.o \
	$(BUILD_DIR)/virtio_rng.o \
	$(BUILD_DIR)/vmm.o $(BUILD_DIR)/shm.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/log.o \
	$(BUILD_DIR)/gdt.o $(BUILD_DIR)/smp.o $(BUILD_DIR)/smp_topology.o \
	$(BUILD_DIR)/smp_asm.o \
	$(BUILD_DIR)/fpu.o $(BUILD_DIR)/lapic.o $(BUILD_DIR)/acpi.o \
	$(BUILD_DIR)/ioapic.o $(BUILD_DIR)/ahci.o $(BUILD_DIR)/block.o \
	$(BUILD_DIR)/block_silliconfs.o \
	$(BUILD_DIR)/pci.o $(BUILD_DIR)/pci_resource.o $(BUILD_DIR)/dma.o \
	$(BUILD_DIR)/dma_ring.o $(BUILD_DIR)/netbuf.o $(BUILD_DIR)/netdev.o \
	$(BUILD_DIR)/udp_socket.o $(BUILD_DIR)/tcp_socket.o \
	$(BUILD_DIR)/network_service.o \
	$(BUILD_DIR)/cyclone/cyclone_net.o $(CYCLONE_STACK_OBJECTS) \
	$(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/scheduler.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/gdt_asm.o \
	$(BUILD_DIR)/interrupts_asm.o $(BUILD_DIR)/context.o $(BUILD_DIR)/fd.o \
	$(BUILD_DIR)/terminal.o $(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/framebuffer_device.o $(BUILD_DIR)/tty.o \
	$(BUILD_DIR)/ps2_keyboard.o \
	$(BUILD_DIR)/syscall.o $(BUILD_DIR)/pipe.o $(BUILD_DIR)/pipe_buffer.o \
	$(BUILD_DIR)/unix_socket.o \
	$(BUILD_DIR)/pty.o \
	$(BUILD_DIR)/user_entry.o \
	$(BUILD_DIR)/user_entry_c.o \
	$(BUILD_DIR)/elf.o \
	$(BUILD_DIR)/module.o \
	$(BUILD_DIR)/module_loader.o $(BUILD_DIR)/driver_api.o \
	$(BUILD_DIR)/process.o \
	$(BUILD_DIR)/signal_state.o \
	$(BUILD_DIR)/string.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ramfs.o \
	$(BUILD_DIR)/initramfs.o $(BUILD_DIR)/silliconfs.o \
	$(BUILD_DIR)/devfs.o $(BUILD_DIR)/procfs.o \
	$(ACPICA_OBJECTS)

-include $(KERNEL_OBJECTS:.o=.d)

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) src/kernel/arch/x86_64/boot/linker.ld
	$(LD) $(KERNEL_LDFLAGS) $(KERNEL_OBJECTS) -o $@

iso: $(BUILD_DIR)/opensillicon.iso

INITRAMFS_INPUTS := $(shell find src/initramfs -print)

$(BUILD_DIR)/initramfs.cpio: src/tools/mkinitramfs.py $(INITRAMFS_INPUTS) | $(BUILD_DIR)
	python3 src/tools/mkinitramfs.py src/initramfs $@

$(BUILD_DIR)/opensillicon.iso: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/initramfs.cpio \
		src/limine/BOOTX64.EFI src/limine/limine-uefi-cd.bin src/kernel/Makefile
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/boot $(ISO_ROOT)/limine
	cp src/limine/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	cp src/limine/limine-uefi-cd.bin $(ISO_ROOT)/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
	cp $(BUILD_DIR)/initramfs.cpio $(ISO_ROOT)/boot/initramfs.cpio
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n    module_path: boot():/boot/initramfs.cpio\n    module_string: initramfs\n    resolution: 1920x1080x32\n' > $(ISO_ROOT)/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $@

endif

$(BUILD_DIR)/pty.o: src/kernel/tty/pty.c src/include/kernel/pty.h src/include/kernel/pipe.h \
		src/include/kernel/fd.h
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/driver_api.o: src/kernel/module/driver_api.c \
		src/include/kernel/cyclone_net.h \
		src/include/kernel/udp_socket.h src/include/kernel/tcp_socket.h \
		src/include/kernel/network_service.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@
