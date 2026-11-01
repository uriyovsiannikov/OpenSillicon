# TPM 2.0 CRB provider: design for a future implementation

**Phase:** 15.6H — read-only investigation and design
**Assessment date:** 2026-11-01
**Decision:** a narrowly scoped CRB provider is technically feasible, but the
current machine's CRB table details and TPM identity are unavailable to this
user. No TPM output is approved as entropy.

## Scope and evidence labels

This is a design for a later provider. It adds no driver, parser, command
encoder, entropy-manager source, test backend, or TPM state operation. Terms
are labeled as **Observed** (host/repository evidence), **Specification**
(published TCG text), **Linux behavior** (implementation or workaround), and
**Proposal** (OpenSillicon design choice).

Primary references are TCG PC Client Platform TPM Profile (PTP) v1.07,
published 2026-03-23, with current v1.1 errata; TCG ACPI General Specification
v1.50, published 2025-12-12; and the TPM 2.0 Library. The current Library
publication page lists Version 185 and errata. Some accessible Part 1/Part 3
PDFs at review time are marked RC4/RC2; those are release candidates, not
final normative text. Verify final parts and errata before implementation.
Linux references are `drivers/char/tpm/tpm_crb.c`,
`drivers/char/tpm/tpm2-cmd.c`, `drivers/char/tpm/tpm-chip.c`, and Linux ACPI
TPM binding code. Linux is evidence about a mature implementation, not a
replacement for TCG requirements.

## Current host and repository evidence

**Observed:** Linux reports TPM 2.0 at `/sys/class/tpm/tpm0`, associated with
ACPI device `INTC7001:00`, HID `INTC7001`, CID `MSFT0101`, and driver
`tpm_crb_acpi`. `/dev/tpm0` and `/dev/tpmrm0` exist but deny access to the
current unprivileged user. The `TPM2` ACPI table is root-readable only and was
not read. Read-only `tpm2_getcap` attempts failed with `Permission denied`; no
TPM command was issued. Thus the table's start method, control-area address,
buffer addresses/capacities, TPM manufacturer, firmware revision, capabilities,
and actual `TPM2_GetRandom` behavior are unknown. Linux's binding establishes
an ACPI CRB association, not Intel PTT, a discrete TPM, or a specific CRB
method. The platform remains unknown/ambiguous rather than confirmed physical.

**Observed in OpenSillicon:** ACPI code parses MADT/MCFG and initializes
ACPICA, but has no TPM2 table parser or TPM driver. PCI MMIO leases map
validated PCI BARs only; they are not a general ACPI physical-range mapping
API. `vmm_physical_range_mmio_safe()` can reject ranges overlapping described
RAM, and VMM can map uncached pages, but a CRB needs an explicit ownership and
lifetime mapping contract. The 100 Hz kernel timer starts before ACPI and PCI
initialization. There are no TPM command symbols or TPM tests. Both
`kernel_entropy_cryptographic_ready()` and `tcp_core_live_isn_available()`
return false.

Host discovery is recorded in
[`tpm2-feasibility-assessment.md`](tpm2-feasibility-assessment.md); source
trust is recorded in [`entropy-trust-assessment.md`](entropy-trust-assessment.md).
This design does not change either conclusion.

## ACPI discovery and supported start methods

### TPM2 ACPI table

TCG ACPI v1.50 §3.3 defines the `TPM2` SDT: ACPI header, platform class and
reserved fields, 64-bit `AddressOfControlArea`, 32-bit `StartMethod`, and
method-dependent parameters; optional log-area fields may extend the table.
For CRB, the control-area address and CRB registers identify the control and
data buffers; never assume one fixed chipset address or a common layout for
all start methods. See [TCG ACPI v1.50 §3.3](https://trustedcomputinggroup.org/wp-content/uploads/TCG-ACPI-General-Specification_v1.50_pub.pdf).

**Proposal:** copy the ACPI table into bounded kernel-owned memory, then
validate signature, length, checksum, revision/field presence, reserved
fields, method-parameter bounds, physical-address width, page rounding, and
overflow in every `base + size` calculation. Verify the full control-register
span and buffer extents against ACPI resources and the platform memory map.
Reject ambiguous overlap except for a profile-defined shared command/response
buffer. Do not probe guessed MMIO. A valid HHDM address does not prove device
ownership or safe mapping.

### Start method table

Values below summarize the TCG ACPI v1.50 TPM2 start-method table. Support
column is a proposal, not a claim about the host.

| Value | Method | Meaning | Initial CRB provider policy |
|---:|---|---|---|
| 0 | Invalid | No usable interface | Reject |
| 1 | Reserved/legacy | Legacy/vendor method | Reject |
| 2 | ACPI Start | Start/cancel via ACPI `_DSM` | Defer pending `_DSM` implementation/tests |
| 3–5 | Reserved/legacy | Legacy identifiers | Reject |
| 6 | MMIO TIS 1.2 with Cancel | FIFO/TIS, not CRB | Reject |
| 7 | CRB | Standard CRB Start register | Candidate minimal support after target validation |
| 8 | CRB with ACPI Start | CRB plus `_DSM` notification | Defer; implement exact `_DSM` contract first |
| 9–10 | Reserved/legacy | Legacy identifiers | Reject |
| 11 | CRB with Arm SMC/HVC | Secure monitor signaling | Reject in x86-64 scope |
| 12 | FIFO over I2C | I2C FIFO transport | Reject |
| 13 | CRB with AMD mailbox | Platform notification | Reject pending separate specification/testing |
| 14 | Reserved/future MMIO | Reserved | Reject |
| 15 | CRB over Arm FF-A | FF-A notification transport | Reject in x86-64 scope |
| 16+ | Reserved | Future/unknown | Reject |

Terminology correction for the implementation phase: TCG ACPI v1.50 Table 8
defines **7 as “Uses the Command Response Buffer Interface”** and **8 as
CRB with the ACPI Start Method**. A request description that calls method 7
“CRB with ACPI Start” conflates these two values. The parser therefore
accepts method 7 only, as plain CRB, and does not claim `_DSM`/ACPI Start
support.

Start method 7 is the only suitable first method. Method 8 must not be
approximated by writing the CRB Start bit alone. PTP 1.07 defines optional
chunking via `nextChunk` and `crbRspRetry`; a first small `GetRandom` provider
should leave chunking unimplemented and reject any command/response that does
not fit the actual buffers. Linux contains a compatibility workaround for
some PTT devices reporting ACPI Start but requiring an additional CRB Start.
That is Linux-specific behavior, not a general protocol rule.

## CRB control area and buffers

Offsets below are relative to a locality base in PTP v1.07 §6.5.3/Table 36.
The PC Client profile spaces locality windows by 0x1000. Register accesses
must use width-correct MMIO operations, not plain C structure loads/stores.

| Offset | Register | Width/access | Semantics |
|---:|---|---|---|
| `0x00` | `TPM_LOC_STATE_0` | 32-bit RO | Shared locality state, including register-valid and locality-assigned state. |
| `0x08` | `TPM_LOC_CTRL_0` | 32-bit control | Request/relinquish locality using the defined control bits; initial driver does not seize. |
| `0x0c` | `TPM_LOC_STS_0` | 32-bit RO | Per-locality granted/seized status. |
| `0x30` | `TPM_CRB_INTF_ID_0` | 64-bit RO | Interface capabilities and vendor/device/revision identifiers. |
| `0x40` | `TPM_CRB_CTRL_REQ_0` | 32-bit RW request bits | `cmdReady` bit 0 and `goIdle` bit 1; set one request at a time. |
| `0x44` | `TPM_CRB_CTRL_STS_0` | 32-bit RO | `cSUMAvailable` bit 2, `tpmIdle` bit 1, `tpmSts` fatal status bit 0. |
| `0x48` | `TPM_CRB_CTRL_CANCEL_0` | 32-bit RW | Asynchronous command cancel; writing it does not prove quiescence. |
| `0x4c` | `TPM_CRB_CTRL_START_0` | 32-bit RW | `Start` bit 0; PTP 1.07 adds `crbRspRetry` bit 1 and `nextChunk` bit 2. Never combine fields in one write. |
| `0x50` | `TPM_CRB_INT_ENABLE_0` | 32-bit RW | Conditional interrupt setup; disabled for initial polling design. |
| `0x54` | `TPM_CRB_INT_STS_0` | 32-bit status | Interrupt status; unused when interrupts are disabled. |
| `0x58` | `TPM_CRB_CTRL_CMD_SIZE_0` | 32-bit RO | Command buffer capacity. |
| `0x5c` | `TPM_CRB_CTRL_CMD_LADDR_0` | 32-bit RO | Command buffer physical address low half. |
| `0x60` | `TPM_CRB_CTRL_CMD_HADDR_0` | 32-bit RO | Command buffer physical address high half. |
| `0x64` | `TPM_CRB_CTRL_RSP_SIZE_0` | 32-bit RO | Response capacity; equals command size when one shared buffer is used. |
| `0x68` | `TPM_CRB_CTRL_RSP_ADDR_0` | 64-bit RO | Response buffer physical address. |
| `0x80` | CRB data buffer base | Buffer region | PTP PC Client locality data-buffer base; actual address/capacity must follow the advertised interface. |

Address and size registers are platform/TPM populated and read-only to
software. PTP includes conditional features; the driver must read capability
bits and reject unsupported operation. Interrupts and checksum support are
not needed by a bounded polled MVP. A single command/response buffer must be
treated as an alias, not two independent areas. PTP source: [v1.07
§§6.5.3.1–6.5.3.10, Tables 36, 41–44](https://trustedcomputinggroup.org/wp-content/uploads/PC-Client-Specific-Platform-TPM-Profile-for-TPM-2p0-v1p07_Pub.pdf).

### Mapping and ordering

PTP allows CRB implementations with device registers and shared RAM
(including RAM CRB cases); not every address is ordinary RAM or every region
the same kind of MMIO. The driver must validate resource ownership, alignment,
page rounding, physical-address width, non-overflowing ranges, required
register coverage, buffer capacities, and any buffer alias. It must map
registers with device-appropriate uncached attributes, and map shared buffers
with platform-defined cache/coherency attributes. It must use architecture
MMIO accessors and barriers: command-buffer writes become visible before the
start notification; response reads occur after completion and an appropriate
read barrier. The specification defines device-visible order/state, not the
CPU instruction used to implement barriers.

Current PCI leases are BAR-specific; a narrowly scoped validated physical
window lease is a prerequisite. Do not use HHDM translation alone or pass a
virtual address to a register expecting a physical one. Keep mappings alive
until quiescence is established. On an unrecoverable timeout, quarantine
potentially device-owned mappings until reboot rather than unmapping memory
that hardware might still access.

## Transaction lifecycle

**Proposal:** one in-flight command globally, locality 0, normal context,
bounded fixed buffers, no queue. A second caller receives `BUSY`; it does not
spin. Interrupt context, scheduler/allocator locks, and packet paths cannot
issue TPM commands.

1. Acquire a nonblocking transaction token; validate driver state, request,
   buffer capacities, and all integer ranges before hardware writes.
2. Request locality 0 using the specified register; wait finitely for valid,
   assigned, and granted state. Do not seize a higher-priority locality.
3. Check fatal/status state and request Ready (`cmdReady`) as required by the
   current interface state; wait for acknowledgement and validate `tpmIdle`.
4. Encode the command in bounded kernel-owned memory, check total size against
   discovered capacity, copy to CRB shared buffer, issue write ordering.
5. Notify using the exact advertised start method. For method 7, write
   `Start=1` and wait until cleared. For method 8, use the ACPI `_DSM` method;
   no guessed substitute.
6. On completion, check fatal status, order reads, validate and copy the
   response header/body into kernel-owned storage, and distinguish TPM return
   codes from transport errors.
7. For successful GetRandom, validate the `TPM2B_DIGEST` length against actual
   response length, response capacity, and requested bytes. Short data must
   remain explicitly short or fail the all-or-nothing candidate API without
   modifying caller output.
8. Transition to Idle (`goIdle`) when the interface state requires it; wait
   finitely for acknowledgement, relinquish locality when appropriate, then
   release ownership.

Every failure path must unwind only transitions known to have completed. A
well-formed TPM error response is not a transport corruption. Malformed
framing, impossible lengths, fatal status, or an interface that cannot return
to a known safe state must never return candidate bytes.

Linux `tpm_crb.c` demonstrates locality acquisition, command-ready handling,
ordered I/O, response bounds checks, and start-method dispatch. Its ACPI
`_DSM`, SMC, FF-A branches are platform-specific. Linux also has a
PTT/MSFT0101 compatibility path that may issue both `_DSM` and CRB Start; do
not copy that workaround as the standard method. Linux `/dev/tpmrm0` adds
resource-manager arbitration and transient-object virtualization; a bare
kernel still needs its own command serialization. See [Linux CRB
driver](https://github.com/torvalds/linux/blob/master/drivers/char/tpm/tpm_crb.c),
[TPM command core](https://github.com/torvalds/linux/blob/master/drivers/char/tpm/tpm2-cmd.c),
and [TPM core](https://github.com/torvalds/linux/blob/master/drivers/char/tpm/tpm-chip.c).

## Command and response framing

TPM fields are big-endian even on x86. A command header is 10 bytes:

| Offset | Field | Encoding |
|---:|---|---|
| 0 | Tag (`TPM_ST_NO_SESSIONS` for this command) | UINT16 BE |
| 2 | Total command size | UINT32 BE |
| 6 | Command code | UINT32 BE |
| 10 | Command-specific parameters | Defined per command |

`TPM2_GetRandom` (`TPM_CC_GetRandom = 0x0000017B`) appends a UINT16
`bytesRequested`; asking for 16 bytes makes a 12-byte command. A successful
response has the 10-byte header followed by `TPM2B_DIGEST`: a UINT16 byte
count and that many bytes. Error responses may contain only the common
header. Validate response tag, total size, response code, 2B length, and
buffer bounds before reading data. A TPM can return fewer bytes than
requested; response maximum depends on its `TPM2B_DIGEST` capacity and CRB
buffer. PTP 1.07 adds optional chunking, excluded from the first provider.
Never treat a requested length as a returned length. Before code is written,
reconcile command definitions with final TPM Library Parts 2/3 and errata;
see the [TCG Library publication page](https://trustedcomputinggroup.org/resources/documents/tpm-library-specification).

## Timeout and recovery policy

PTP v1.07 §6.5.1.4/Table 27 defines `TIMEOUT_A=750 ms`, `TIMEOUT_B=2000 ms`,
`TIMEOUT_C=200 ms`, `TIMEOUT_D=30 ms`. Table 26 gives GetRandom command
duration 750 ms and timeout 2000 ms. These are bounded interface/command
requirements, not permission for unbounded polling. Use the relevant
command-specific and interface limits. See [PTP timing tables](https://trustedcomputinggroup.org/wp-content/uploads/PC-Client-Specific-Platform-TPM-Profile-for-TPM-2p0-v1p07_Pub.pdf).

**Prototype proposal:** after `timer_init`, use absolute monotonic deadlines
in ordinary context. The existing 100 Hz timer yields rounded-up limits of
75, 200, 20, and 3 ticks respectively; the clock is coarse but bounded. Do
not invoke the provider before timer initialization or after timer failure. A
strict loop-count limit without a timer bounds CPU work but is not a
specification-time timeout and is unsuitable to claim CRB conformance.

On command timeout, request Cancel only through the supported mechanism,
then wait at most `TIMEOUT_B` for completion/cancel response and a known safe
state. PTP cancellation is asynchronous: completion or `TPM_RC_CANCELED` is
possible and writing Cancel alone does not prove stopped execution. Clear the
register as required. If Start never clears, the TPM remains fatal/busy,
locality cannot be relinquished, or safe buffer ownership cannot be proved,
disable the provider for the rest of boot and retain mappings. A normal TPM
error response is a request failure, not automatically a permanent transport
failure. Permanent disablement and mapping quarantine are project safety
policy; Linux timed polling is implementation behavior, not normative policy.

## Concurrency and API boundary

The first provider should expose a narrow explicit operation conceptually
equivalent to `tpm_crb_get_random_candidate(requested, out, capacity,
actual_length)`. It uses fixed buffers, one request in flight, explicit
statuses (`UNAVAILABLE`, `BUSY`, `TIMEOUT`, `TPM_ERROR`, `MALFORMED`,
`DISABLED`, `OK`), and all-or-nothing caller output. It neither queues,
mixes, nor falls back to another source. A future entropy-manager adapter
must preserve TPM provenance and candidate status.

On the current UP kernel, protect acquisition/release of an owner flag with a
short interrupt-masked critical section, then allow interrupts during the
bounded transaction. Never hold interrupts off for up to seconds. Since
preemption can switch tasks on one CPU, a contender must return `BUSY`, not
spin while the owner is suspended. Disallow ISR callers. SMP later requires
real cross-CPU serialization and cancellation-aware waiting; no SMP safety is
claimed here.

## Entropy interpretation

These are distinct results: (1) a command was accepted; (2) CRB transport and
framing were correct; (3) the TPM RNG implementation operated as specified;
(4) its internal entropy source and reseeding are adequate; (5) it is
independent of CPU, firmware, hypervisor, or other sources; (6) output is
approved input to a particular DRBG. A functional driver test can establish
only the first two. TPM2_GetRandom returns output of TPM RNG architecture, not
a promise of raw physical entropy. The host has not disclosed its TPM type,
firmware, RNG design, capabilities, or command result. No output receives
entropy credit. `kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` remain false.

## Staged future implementation and tests

| Step | Likely files/API | Preconditions and deterministic tests | Disabled condition |
|---|---|---|---|
| A. TPM2 ACPI parser | `include/kernel/tpm2_acpi.h`, `kernel/drivers/tpm/tpm2_acpi.c` | Bounded copy; missing table, signature/checksum/revision/length, truncated options, method matrix, address overflow | No MMIO before parser/range validation |
| B. Physical window lease | Small VMM extension, not PCI BAR API reuse | Resource ownership, RAM overlap, alignment, rounding, overflow, rollback/lifetime | No transport until map ownership is proven |
| C. CRB state machine | `include/kernel/tpm_crb.h`, `kernel/tpm_crb.c` | Fake register backend: locality/ready/start/idle, timeout, fatal status, cancel, unsupported method, aliasing | No physical provider until all lifecycle tests pass |
| D. Framing and GetRandom | Fixed bounded codec/API | Endianness, sizes, TPM errors, short/truncated/oversized responses, output unchanged, repeated command | No candidate result until response validation is complete |
| E. QEMU protocol test | Test harness only | Verify installed QEMU version, `tpm-crb` frontend, swtpm, emitted ACPI method and table; emulator path tests | Emulator is not physical TPM/entropy evidence |
| F. Physical validation | Isolated opt-in boot test | Authorized read-only identify/capability path; bounded command, no provisioning/clear | No ordinary-boot probe; failures remain optional |
| G. Entropy manager adapter | Add distinct source only after review | Provenance, retry, absence, short-response tests | No default source, mixing, credit, CSPRNG, or TCP enablement |

Future fake-backend tests must include missing/invalid TPM2 table, unsupported
start method, physical address overflow, invalid size/alignment, locality
and command timeouts, cancellation failure, fatal status, malformed header,
inconsistent response size, TPM error, partial GetRandom, oversized response,
repeated reads, and output unchanged for every failed all-or-nothing result.
They must ensure mappings are retained when quiescence is unknown. Tests must
never access this host's real TPM.

QEMU documents a `tpm-crb` frontend backed by `swtpm`; it exchanges commands
and responses with the emulator. Verify the chosen QEMU machine emits a TPM2
table with the expected method before using it. Such tests cover device-model
transport only, not physical TPM behavior or entropy quality. [QEMU TPM
documentation](https://www.qemu.org/docs/master/specs/tpm).

## Open questions and unsupported scope

- Host ACPI start method, control/data ranges and capacities: unknown.
- Exact TPM identity (Intel PTT, other firmware TPM, discrete, or virtual):
  unknown.
- Manufacturer, firmware, supported commands/algorithms, response limits,
  and `GetRandom` behavior: unknown; no command was attempted.
- Vendor documentation for entropy source, DRBG, reseeding, RNG evaluation,
  certification scope, and advisories: not obtained.
- Correct generic ACPI-owned MMIO/shared-memory lease: not currently exposed.
- Whether the target requires ACPI `_DSM`, chunking, or vendor workaround:
  unknown.
- How firmware-established TPM startup state should be handled without
  state-changing commands: target-specific question.

Initial support excludes TIS/FIFO, SPI/I2C, SMC/HVC, FF-A, AMD mailbox,
ACPI Start, chunking, locality seizure, interrupts, general TPM commands, and
generic physical hardware. No production TPM provider is justified yet.

## Phase 15.6I — ACPI TPM2 parser

The pure parser is exposed by `include/kernel/tpm2_acpi.h` as
`tpm2_acpi_parse(table, table_size, info_out)`. It supports only TCG ACPI
table revision 6 and the 52-byte mandatory layout, plus the 80-byte revision-6
layout containing the optional LAML/LASA pair. It rejects other table lengths
for this supported method. Offsets are read explicitly as little-endian byte
fields: ACPI signature 0, length 4, revision 8, checksum 9; platform class
36, reserved 38, control-area address 40, start method 48; start-method
parameter space 52–67 in the extended form; LAML 68 and LASA 72. The parser
accepts platform class 0 or 1, requires the reserved field to be zero, and
accepts only start method 7. Method 7 has no method-specific parameter
encoding in this implementation; for the extended form bytes 52–67 must be
zero. This is a conservative project policy, not a general parser for all
revision-6 TPM2 tables.

The result contains the validated table revision/length, platform class,
method, CRB control-area physical address, and optional event-log range. The
control-area field must be nonzero, 4-byte aligned, and representable for the
4-byte `TPM_CRB_CTRL_REQ_0` register; event-log length/address must be both
zero or form a non-overflowing range. These checks do not validate physical
address width, memory-map ownership, device presence, MMIO accessibility,
resource coverage, or safe mapping. Failure returns a stable enum and leaves
the caller output byte-for-byte unchanged. Success copies a fully initialized
local result. The parser performs no allocation, pointer retention, hardware
access, or entropy-manager registration.

The host target `make tpm2-acpi-test` uses synthetic revision-6 fixtures. It
covers valid minimum/extended tables, unaligned input, all short input
boundaries, signature/length/revision/checksum failures, reserved/platform
values, unsupported and unknown methods, address alignment/overflow, optional
parameter and event-log validation, output unchanged on failure, and
deterministic repeated parsing. Sanitizer outcomes are reported with the
Phase 15.6I implementation results. CRB transport remains blocked on a
validated ACPI-owned mapping/lifetime abstraction and separate transport
state-machine tests.

## References

- [TCG PTP v1.07 published (2026-03-23)](https://trustedcomputinggroup.org/wp-content/uploads/PC-Client-Specific-Platform-TPM-Profile-for-TPM-2p0-v1p07_Pub.pdf), §§6.5.1.3–5, 6.5.3.1–10; Tables 26, 27, 36, 41–44. See current [PTP errata index](https://www.trustedcomputinggroup.org/resources/documents/pc-client-platform-tpm-profile-ptp-specification).
- [TCG ACPI General Specification v1.50 (2025-12-12)](https://trustedcomputinggroup.org/wp-content/uploads/TCG-ACPI-General-Specification_v1.50_pub.pdf), §3.3/Table 8.
- [TCG TPM Library publication/errata index](https://www.trustedcomputinggroup.org/resources/documents/tpm-library-specification). Accessible RC-labelled PDFs are not final specifications.
- Linux [`tpm_crb.c`](https://github.com/torvalds/linux/blob/master/drivers/char/tpm/tpm_crb.c), [`tpm2-cmd.c`](https://github.com/torvalds/linux/blob/master/drivers/char/tpm/tpm2-cmd.c), [`tpm-chip.c`](https://github.com/torvalds/linux/blob/master/drivers/char/tpm/tpm-chip.c). Linux `master` is mutable implementation evidence; pin a commit for implementation review.
- [Linux TPM security documentation](https://kernel.org/doc/html/latest/security/tpm/tpm-security.html), [Linux CRB/FF-A documentation](https://www.kernel.org/doc/html/latest/security/tpm/tpm_ffa_crb.html), and [QEMU TPM device documentation](https://www.qemu.org/docs/master/specs/tpm).
