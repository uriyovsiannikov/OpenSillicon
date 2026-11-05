PCI support enumerates devices and reads their configuration. `pci_bar_decode`
decodes the currently assigned BAR but intentionally leaves its aperture
unknown. A driver must first use `pci_device_claim` on its enumeration
snapshot. Claims are exclusive among drivers using this API and are guarded by
a generation token; the kernel rechecks device identity, class, header,
command and BAR values when a claim is created and when a BAR is sized. Claims
do not yet bind legacy in-kernel users that do not participate in this API,
so those drivers must still be quiesced before probing.

`pci_bar_probe_size` accepts an active claim and an assigned memory BAR. It
serializes config-space accesses, disables I/O, memory and bus-master decode,
reads the sizing mask, and restores the original BAR and command word on every
exit path. Only after the returned resource passes
`pci_bar_resource_valid` can `pci_mmio_map` create an uncached, writable MMIO
lease, or `pci_mmio_map_readonly` create an uncached lease without write
permission in the kernel page tables. Mapping requires the claim and exact
resource returned by sizing; it pins the claim until `pci_mmio_release`
unmaps every page. Claim release is rejected while mappings remain. BAR
reallocation is not supported.

Public `pci_config_write` still cannot write arbitrary BAR values. BAR writes
are confined to the private sizing transaction and the existing dedicated
VirtIO RNG assignment helper.
