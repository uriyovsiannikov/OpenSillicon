# Kernel modules

`make modules` builds the e1000 module used by the QEMU network profile. The
root image installs it under `/boot/modules`; the kernel loads it after
mounting SilliconFS. If the module is missing or invalid, boot continues
without networking.

The module probes one QEMU 8086:100e Ethernet device, checks the driver API
version, starts the device, and attaches it to the DHCP-configured network
(MAC 52:54:00:15:03:01). The driver remains
outside the core kernel ELF.

e1000 refuses unloading with `EBUSY` because network services retain its
callbacks for the whole boot. It also remains resident if startup or network
attachment fails after initialization begins, since releasing code after a
failed DMA stop would be unsafe.
