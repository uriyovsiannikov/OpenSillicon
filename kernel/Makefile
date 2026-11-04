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

$(BUILD_DIR)/main.o: kernel/main.c include/kernel/boot.h include/kernel/log.h \
		include/kernel/pmm.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/errno.h \
		include/kernel/timer.h include/kernel/scheduler.h include/kernel/smp.h \
		include/arch/x86_64/lapic.h include/arch/x86_64/interrupts.h \
		include/arch/x86_64/user.h include/kernel/fd.h include/kernel/syscall.h \
		include/kernel/elf.h include/kernel/process.h include/kernel/vfs.h \
		include/arch/x86_64/acpi.h include/arch/x86_64/ioapic.h \
		include/arch/x86_64/pci.h \
		include/kernel/dma.h \
		include/kernel/e1000.h include/kernel/e1000_profile.h \
		include/kernel/netstack.h include/kernel/udp_socket.h \
		include/kernel/network_service.h \
		include/kernel/framebuffer_device.h include/abi/framebuffer.h \
		include/kernel/tcp_runtime.h \
	include/kernel/random.h include/kernel/boot_entropy.h \
	include/kernel/entropy.h \
	include/kernel/rdseed.h \
	include/kernel/virtio_rng.h \
	include/kernel/smp.h \
		include/arch/x86_64/ahci.h include/kernel/block.h \
		include/kernel/block_silliconfs.h include/kernel/silliconfs.h \
		include/kernel/devfs.h include/kernel/procfs.h \
		include/arch/x86_64/fpu.h \
		include/kernel/tty.h \
		include/kernel/framebuffer_device.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/boot.o: kernel/core/boot/boot.c include/kernel/boot.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/boot_entropy.o: kernel/core/boot/boot_entropy.c \
		include/kernel/boot_entropy.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/entropy.o: kernel/random/entropy.c include/kernel/entropy.h \
		include/kernel/random.h include/kernel/rdseed.h \
		include/kernel/virtio_rng.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/rdseed.o: kernel/drivers/entropy/rdseed.c include/kernel/rdseed.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/virtio_rng.o: kernel/drivers/entropy/virtio_rng.c \
		include/kernel/virtio_rng.h include/kernel/dma.h \
		include/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/pmm.o: kernel/mm/pmm.c include/kernel/pmm.h \
		include/kernel/boot.h include/kernel/spinlock.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vmm.o: kernel/mm/vmm.c include/kernel/vmm.h \
		include/arch/x86_64/copy.h \
		include/kernel/pmm.h include/kernel/boot.h include/kernel/log.h \
		include/kernel/shm.h include/kernel/smp.h include/kernel/spinlock.h \
		limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/dma.o: kernel/mm/dma.c include/kernel/dma.h \
		include/kernel/pmm.h include/kernel/vmm.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/dma_ring.o: kernel/mm/dma_ring.c include/kernel/dma_ring.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/shm.o: kernel/mm/shm.c include/kernel/shm.h \
		include/kernel/vmm.h include/kernel/pmm.h include/kernel/heap.h \
		include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/heap.o: kernel/mm/heap.c include/kernel/heap.h \
		include/kernel/vmm.h include/kernel/pmm.h \
		include/kernel/spinlock.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/log.o: kernel/core/log/log.c include/kernel/log.h include/kernel/tty.h \
		include/kernel/framebuffer.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/fd.o: kernel/proc/fd.c include/kernel/fd.h include/kernel/log.h \
		include/kernel/heap.h include/kernel/vfs.h include/kernel/tty.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pipe.o: kernel/ipc/pipe.c include/kernel/pipe.h \
		include/kernel/process.h include/kernel/fd.h include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c kernel/ipc/pipe.c -o $@

$(BUILD_DIR)/pipe_buffer.o: kernel/ipc/pipe_buffer.c include/kernel/pipe.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/unix_socket.o: kernel/ipc/unix_socket.c \
		include/kernel/unix_socket.h include/kernel/fd.h \
		include/kernel/process.h include/kernel/heap.h \
		include/abi/unix_socket.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/terminal.o: kernel/tty/terminal.c include/kernel/terminal.h \
		include/kernel/fd.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/keyboard.o: kernel/tty/keyboard.c \
		include/arch/x86_64/ps2_keyboard.h include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/framebuffer.o: kernel/tty/framebuffer.c \
		include/kernel/framebuffer.h include/kernel/terminal.h \
		kernel/tty/font.h kernel/tty/font/gallant.go \
		kernel/tty/font/GALLANT-LICENSE.txt | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/framebuffer_device.o: kernel/tty/framebuffer_device.c \
		include/kernel/framebuffer_device.h include/abi/framebuffer.h \
		include/kernel/vmm.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tty.o: kernel/tty/tty.c include/kernel/tty.h \
		include/kernel/terminal.h include/kernel/framebuffer.h \
		include/arch/x86_64/ps2_keyboard.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ps2_keyboard.o: kernel/drivers/input/ps2_keyboard.c \
		include/arch/x86_64/ps2_keyboard.h include/kernel/terminal.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/string.o: kernel/core/lib/string.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/vfs.o: kernel/fs/vfs/vfs.c include/kernel/vfs.h \
		include/kernel/fd.h include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/devfs.o: kernel/fs/devfs/devfs.c include/kernel/devfs.h \
		include/kernel/vfs.h include/kernel/fd.h include/kernel/heap.h \
		include/kernel/tty.h include/kernel/log.h include/kernel/random.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/procfs.o: kernel/fs/procfs/procfs.c include/kernel/procfs.h \
		include/kernel/vfs.h include/kernel/process.h include/kernel/timer.h \
		include/kernel/heap.h include/kernel/pmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ramfs.o: kernel/fs/ramfs/ramfs.c include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/initramfs.o: kernel/fs/initramfs/initramfs.c include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/silliconfs.o: kernel/fs/silliconfs/silliconfs.c \
		include/kernel/silliconfs.h include/kernel/vfs.h \
		include/kernel/heap.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/core/syscall/syscall.c include/kernel/syscall.h \
		include/abi/syscall.h include/abi/select.h \
		include/kernel/fd.h include/kernel/vmm.h include/kernel/log.h \
		include/kernel/process.h include/kernel/vfs.h include/kernel/udp_socket.h \
		include/kernel/icmp_echo.h include/kernel/unix_socket.h \
		include/kernel/network_service.h \
		include/kernel/framebuffer_device.h include/abi/framebuffer.h \
		include/abi/input.h include/arch/x86_64/ps2_keyboard.h \
		include/arch/x86_64/user.h include/arch/x86_64/interrupts.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/elf.o: kernel/proc/elf.c include/kernel/elf.h include/kernel/vmm.h \
		include/kernel/heap.h include/kernel/pmm.h include/kernel/shm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/process.o: kernel/proc/process.c include/kernel/process.h \
		include/kernel/syscall.h include/abi/syscall.h \
		include/kernel/signal_state.h include/kernel/elf.h include/kernel/vmm.h include/kernel/heap.h \
		include/kernel/pmm.h include/kernel/fd.h include/kernel/timer.h \
		include/kernel/smp.h \
		include/kernel/network_service.h \
		include/arch/x86_64/gdt.h include/arch/x86_64/fpu.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/signal_state.o: kernel/proc/signal_state.c \
		include/kernel/signal_state.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/arch/x86_64/interrupts/interrupts.c \
		include/arch/x86_64/gdt.h include/arch/x86_64/interrupts.h \
		include/kernel/log.h include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt.o: kernel/arch/x86_64/cpu/gdt.c include/arch/x86_64/gdt.h \
		include/kernel/smp.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/smp.o: kernel/arch/x86_64/cpu/smp.c include/kernel/smp.h \
		include/arch/x86_64/interrupts.h include/arch/x86_64/lapic.h \
		include/kernel/spinlock.h include/kernel/log.h include/kernel/vmm.h \
		include/kernel/scheduler.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -Ilimine -c $< -o $@

$(BUILD_DIR)/smp_topology.o: kernel/arch/x86_64/cpu/smp_topology.c \
		include/kernel/smp.h limine/limine.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -Ilimine -c $< -o $@

$(BUILD_DIR)/smp_asm.o: kernel/arch/x86_64/cpu/smp.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/fpu.o: kernel/arch/x86_64/cpu/fpu.c \
		include/arch/x86_64/fpu.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/random.o: kernel/random/random.c include/kernel/random.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/lapic.o: kernel/arch/x86_64/controllers/lapic.c \
		include/arch/x86_64/lapic.h include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ioapic.o: kernel/arch/x86_64/controllers/ioapic.c \
		include/arch/x86_64/ioapic.h include/arch/x86_64/acpi.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pci.o: kernel/drivers/bus/pci/pci.c \
		include/arch/x86_64/pci.h include/arch/x86_64/acpi.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/pci_resource.o: kernel/drivers/bus/pci/pci_resource.c \
		include/arch/x86_64/pci_resource.h include/arch/x86_64/pci.h \
		include/kernel/vmm.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netbuf.o: kernel/net/core/netbuf.c include/kernel/netbuf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netdev.o: kernel/net/core/netdev.c include/kernel/netdev.h \
		include/kernel/netbuf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/netstack.o: kernel/net/core/netstack.c include/kernel/netstack.h \
		include/kernel/tcp_wire.h include/kernel/tcp_runtime.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/udp_socket.o: kernel/net/udp/udp_socket.c \
		include/kernel/udp_socket.h include/kernel/netstack.h \
		include/kernel/netdev.h include/kernel/netbuf.h \
		include/kernel/fd.h include/kernel/timer.h include/kernel/errno.h \
		include/kernel/network_service.h include/kernel/icmp_echo.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/network_service.o: kernel/net/service/network_service.c \
		include/kernel/network_service.h include/kernel/udp_socket.h \
		include/kernel/errno.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL -Iinclude -c $< -o $@

$(BUILD_DIR)/tcp_core.o: kernel/net/tcp/tcp_core.c include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tcp_wire.o: kernel/net/tcp/tcp_wire.c include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/tcp_runtime.o: kernel/net/tcp/tcp_runtime.c \
		include/kernel/tcp_runtime.h include/kernel/tcp_core.h \
		include/kernel/tcp_wire.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_profile.o: kernel/drivers/net/e1000/e1000_profile.c \
		include/kernel/e1000_profile.h include/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_ring.o: kernel/drivers/net/e1000/e1000_ring.c \
		include/kernel/e1000_ring.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000.o: kernel/drivers/net/e1000/e1000.c include/kernel/e1000.h \
		include/kernel/e1000_profile.h include/arch/x86_64/pci_resource.h \
		include/kernel/e1000_ring.h include/kernel/dma.h \
		include/kernel/dma_ring.h include/kernel/netdev.h \
		include/kernel/netbuf.h include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/e1000_reset.o: kernel/drivers/net/e1000/e1000_reset.c include/kernel/e1000.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/ahci.o: kernel/drivers/storage/ahci.c \
		include/arch/x86_64/ahci.h include/arch/x86_64/pci.h \
		include/kernel/block.h include/kernel/errno.h include/kernel/dma.h include/kernel/pmm.h \
		include/kernel/vmm.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block.o: kernel/fs/block/block.c include/kernel/block.h \
		include/kernel/errno.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/block_silliconfs.o: kernel/fs/block/block_silliconfs.c \
		include/kernel/block_silliconfs.h include/kernel/block.h \
		include/kernel/errno.h include/kernel/silliconfs.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/acpi.o: kernel/arch/x86_64/firmware/acpi.c \
		include/arch/x86_64/acpi.h include/kernel/heap.h \
		include/kernel/log.h include/kernel/timer.h include/kernel/vmm.h \
		kernel/arch/x86_64/firmware/acpica/source/include/acpi.h \
		kernel/arch/x86_64/firmware/acpica/source/include/acpiosxf.h \
		kernel/arch/x86_64/firmware/acpica/source/include/acpixf.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -DOPEN_SILLICON_KERNEL \
		-isystem kernel/arch/x86_64/firmware/acpica/source/include -Iinclude -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/arch/x86_64/timer/timer.c \
		include/arch/x86_64/lapic.h include/arch/x86_64/interrupts.h \
		include/kernel/timer.h include/kernel/process.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/sched/scheduler.c \
		include/kernel/scheduler.h include/kernel/heap.h include/kernel/vmm.h \
		include/kernel/smp.h include/kernel/spinlock.h \
		include/kernel/log.h | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/gdt_asm.o: kernel/arch/x86_64/cpu/gdt.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts_asm.o: kernel/arch/x86_64/interrupts/interrupts.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/context.o: kernel/arch/x86_64/context/context.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry.o: kernel/arch/x86_64/context/user_entry.S | $(BUILD_DIR)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_entry_c.o: kernel/arch/x86_64/context/user_entry.c \
		include/arch/x86_64/user.h include/kernel/smp.h include/kernel/log.h | $(BUILD_DIR)
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
	$(BUILD_DIR)/netstack.o \
	$(BUILD_DIR)/tcp_core.o $(BUILD_DIR)/tcp_wire.o $(BUILD_DIR)/tcp_runtime.o \
	$(BUILD_DIR)/udp_socket.o $(BUILD_DIR)/network_service.o \
	$(BUILD_DIR)/e1000_profile.o $(BUILD_DIR)/e1000.o \
	$(BUILD_DIR)/e1000_ring.o \
	$(BUILD_DIR)/e1000_reset.o \
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
	$(BUILD_DIR)/process.o \
	$(BUILD_DIR)/signal_state.o \
	$(BUILD_DIR)/string.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ramfs.o \
	$(BUILD_DIR)/initramfs.o $(BUILD_DIR)/silliconfs.o \
	$(BUILD_DIR)/devfs.o $(BUILD_DIR)/procfs.o \
	$(ACPICA_OBJECTS)

-include $(KERNEL_OBJECTS:.o=.d)

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) kernel/arch/x86_64/boot/linker.ld
	$(LD) $(KERNEL_LDFLAGS) $(KERNEL_OBJECTS) -o $@

iso: $(BUILD_DIR)/opensillicon.iso

$(BUILD_DIR)/opensillicon.iso: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/initramfs.cpio \
		limine/BOOTX64.EFI limine/limine-uefi-cd.bin kernel/Makefile
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/boot $(ISO_ROOT)/limine
	cp limine/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	cp limine/limine-uefi-cd.bin $(ISO_ROOT)/limine/limine-uefi-cd.bin
	cp $(BUILD_DIR)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
	cp $(BUILD_DIR)/initramfs.cpio $(ISO_ROOT)/boot/initramfs.cpio
	printf 'timeout: 0\n/ OpenSillicon\n    protocol: limine\n    path: boot():/boot/kernel.elf\n    module_path: boot():/boot/initramfs.cpio\n    module_string: initramfs\n    resolution: 1920x1080x32\n' > $(ISO_ROOT)/limine.conf
	$(XORRISO) -as mkisofs -R -r -J \
		--efi-boot limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $@

endif

$(BUILD_DIR)/pty.o: kernel/tty/pty.c include/kernel/pty.h include/kernel/pipe.h \
		include/kernel/fd.h
	$(CC) $(KERNEL_CFLAGS) -Iinclude -c $< -o $@
