# TPM 2.0 feasibility and entropy trust assessment

**Phase:** 15.6G — audit and documentation only
**Assessment date:** 2026-11-01
**Host result:** TPM 2.0 detected by Linux, inaccessible to the current unprivileged user
**Trust result:** not established; TPM output is not approved for CSPRNG initialization

## Scope and method

This assessment used read-only host discovery and inspected the repository's
entropy providers, manager, boot integration, and security documents. No
packages were installed, privileged command run, or TPM/firmware/boot setting
changed. The TPM command interface was checked only for access. No random
bytes or TPM authorization material were read, displayed, hashed, stored, or
transmitted.

The host reports x86-64 Fedora 44, kernel `7.2.13-200.fc44.x86_64`, Intel
Core Ultra 5 225H (family 6, model 197, stepping 2), and HONOR FMB-P /
FMB-P-PCB, BIOS 1.13. CPU flags include RDRAND and RDSEED. `systemd-detect-virt`
reported `none` and the prior CPU diagnostic found no hypervisor bit. These
are not hardware attestation; as in Phase 15.6E, execution remains
**unknown/ambiguous**, not confirmed physical.

## Discovery result

Classification: **TPM 2.0 detected but inaccessible**.

- `/sys/class/tpm/tpm0` exists and `tpm_version_major` is `2`.
- `/dev/tpm0` (10:224) and `/dev/tpmrm0` (252:65536) exist.
- Sysfs associates the device with `/sys/devices/platform/INTC7001:00/tpm/tpm0`,
  driver `tpm_crb_acpi`, and firmware node `\\_SB_.TPM_`, HID `INTC7001`, CID
  `MSFT0101`.
- Kernel logs report an ACPI `TPM2` table from HONOR and TPM event-log
  pointers. This establishes firmware-described TPM/CRB plumbing, not RNG
  quality or trustworthy measured boot.
- `tpm2-tools` 5.7 and `tpm2-tss` 4.1.3 are installed. Read-only
  `tpm2_getcap -T device:/dev/tpmrm0 properties-fixed` and the same query on
  `/dev/tpm0` both failed to open with `Permission denied` (exit 1). Node
  ownership is `tss:root` for `/dev/tpm0`, `root:tss` for `/dev/tpmrm0`; the
  current user is in neither `tss` nor `root`.
- `/sys/firmware/acpi/tables/TPM2` exists but is root-readable only and was not
  read. No privileged ACPI dump was attempted.
- `mokutil --sb-state` reports Secure Boot disabled and Platform in Setup
  Mode. This is not evidence about RNG quality or TPM implementation. Systemd
  TPM setup units were skipped on `ConditionSecurity=measured-uki`; that is
  not proof that a TPM event log is absent or authentic.

No capability response was obtained. Manufacturer/vendor ID, firmware
version, TPM specification revision, algorithms, command list, implementation
limits, PCR banks, and command behavior are unknown. `TPM2_GetRandom` was not
run because both device interfaces denied this unprivileged user. No elevated
access, group change, or `sudo` was used.

### Implementation type

The observed Linux driver and ACPI identifiers establish an **ACPI CRB
transport**. They do not establish silicon type. On this Intel platform,
Intel Platform Trust Technology (PTT) is plausible, but that is an inference,
not a verified device identity. Conservative classification: **likely
firmware TPM (possibly Intel PTT); exact implementation unknown**. Discrete
versus firmware-backed TPM and virtualization cannot be conclusively
distinguished from the available evidence.

CRB/TIS describe interfaces, not whether the TPM is discrete or firmware
based. Linux `/dev/tpm0` is its direct TPM character interface;
`/dev/tpmrm0` goes through the kernel resource manager for arbitration and
transient-resource virtualization. Neither is a freestanding-kernel transport
API. OpenSillicon would need its own discovery, transport, locality, framing,
timeout, and reset logic.

## TPM RNG and `TPM2_GetRandom`

At this assessment date the TCG TPM 2.0 Library page lists Version 185
(March 2026) and Version 185 errata. Part 1 RNG architecture is §8.4.11;
Part 3 `TPM2_GetRandom` is §16.1. TCG describes an RNG capability layered
over an internal entropy source/collector and state/DRBG. The collector is
intended to collect and condition source data and regularly update state.
The architecture says an external RNG call should be able to provide 32
octets; larger requests can fail if insufficient randomness is available.
This does not certify an unidentified implementation.

Part 3 defines `TPM_CC_GetRandom` (`0x0000017B`) with a `UINT16
bytesRequested`. Its response includes the TPM response code and a
length-delimited `TPM2B_DIGEST randomBytes`. When the request exceeds the
implementation's `TPM2B_DIGEST` capacity, the TPM returns only the amount
that fits without an error for that size condition. The maximum is therefore
implementation-dependent. Part 1 also permits larger requests to fail when
the RNG lacks sufficient data; Part 3 recommends shortages be infrequent for
requests no larger than the maximum digest size. A caller must validate all
framing and actual lengths, treat short output as incomplete, and avoid an
unbounded retry loop. It must handle TPM response errors separately from
transport timeouts/failures.

The command returns output from the TPM RNG architecture, not a promise that
each returned byte is raw physical entropy. The TPM may use an internal DRBG
seeded by internal entropy sources. Successful command completion proves
only interface-level completion. It does not prove entropy quality, proper
reseed behavior, source independence, or resistance to firmware/hypervisor
control. `TPM2_StirRandom` was not used; its input is not thereby proven to
contain entropy.

No GetRandom functional request occurred, so there are no request/response
lengths or TPM return codes to report.

## Trust and threat analysis

| Threat/deployment | Established | Unknown or attacker-controlled | Approved now? |
|---|---|---|---|
| This HONOR system | Linux sees an ACPI TPM 2.0 CRB device. | Execution is not attested physical; TPM type, vendor, firmware and RNG design unknown. | No. |
| Discrete TPM | Nothing identifies a discrete component. | Component identity, board wiring, firmware, RNG and evaluation. | No. |
| Intel PTT / firmware TPM | Intel platform and `INTC7001` make PTT plausible. | Exact implementation and firmware revision; no capability fingerprint. | No. |
| Virtual TPM | Hypervisor checks reported none. | Virtualization is not conclusively excluded; a host may synthesize/control responses. | No. |
| Secure/measured boot | Secure Boot disabled / Setup Mode; event-log pointers appear in boot logs. | No authenticated measurement chain or validated event log. Measurement is not entropy. | No. |
| Malicious firmware/hypervisor | No interface authenticates the implementation. | Could emulate or influence a firmware/vTPM and returned data. | No. |
| RNG defect | No command or vendor evidence obtained. | Internal source, collector, DRBG, reseed policy, advisories and certification scope. | No. |

TCG's architecture explains intended RNG components but does not identify
this host's implementation or bind it to a vendor, revision, security target,
certification scope, or RNG evaluation. Secure Boot and measured boot would
address boot integrity, not prove RNG output quality. An event log is not an
entropy source.

Independence from RDSEED, RDRAND, Limine, and VirtIO is **unknown**. TPM
firmware may depend on platform or CPU sources; RDSEED and RDRAND share a CPU
trust boundary; Limine may draw from firmware/CPU sources; VirtIO depends on
the host. Different interfaces do not establish independent entropy.
Mixing may improve robustness only under explicit independence assumptions;
concatenation or hashing cannot turn deterministic/attacker-controlled input
into unpredictable seed material. No entropy credit is assigned.

## Comparison with current candidates

| Source | Availability | Current trust | Repeated reads | Main limitation |
|---|---|---|---|---|
| Limine | Early boot only | Untrusted candidate | No; one-shot | Best-effort, unauthenticated quality. |
| RDSEED | CPU-dependent | Untrusted candidate | Yes, bounded | Frequent CF=0/retry exhaustion in Phase 15.6E; environment ambiguous. |
| RDRAND | CPU-dependent | Untrusted candidate | Yes, bounded | CPU implementation/platform trust. |
| VirtIO RNG | Explicit QEMU test profile | Untrusted host-provided candidate | Yes, bounded | Legacy transitional PCI test scope; host controls bytes. |
| TPM 2.0 | Detected, inaccessible to current user | Unapproved | Protocol supports requests; not tested | Implementation and entropy path unknown. |

A TPM can add a separate hardware trust boundary on some platforms, but this
host's evidence does not show that it is separate from Intel firmware or CPU
RNGs. A TPM is not yet demonstrated to add independent entropy assurance.

## Future integration feasibility

**Driver decision: technically feasible but trust evidence insufficient.**
Linux's CRB binding suggests a platform-specific route. OpenSillicon currently
has no TPM driver, TPM2 ACPI parser, or command transport. The ACPI table's
start method and MMIO regions were not read because the table is root-only.
This is not a general hardware-support commitment.

A future narrow provider could expose operations conceptually like:

```c
enum tpm_rng_result kernel_tpm_rng_status(void);
enum tpm_rng_result kernel_tpm_get_candidate(void *out, size_t capacity,
    size_t *bytes_returned);
```

This is a design sketch only. One serialized request at a time suits the
current UP kernel. The provider should run only in normal context, use fixed
bounded command/response buffers, cap request size, preserve source identity
and actual returned length, and never fall back to another provider. It must
validate ACPI table/range bounds, supported CRB method, resource ownership,
ordered MMIO, locality/start/completion, command and response tags/sizes,
TPM return code, and `TPM2B_DIGEST` length. Polling and cancellation/reset
must be finite. It must not reuse or free command memory until device access
is quiesced. HHDM translation alone does not establish safe MMIO ownership or
DMA suitability. A future provider should avoid startup, clear, provisioning,
authorization, and other persistent-state operations; a minimal capability
query and GetRandom are sufficient for this candidate-source use.

Linux's resource manager is unnecessary for a narrow kernel source with no
sessions/handles, but the kernel must provide its own serialization and
arbitration. Firmware may have initialized the TPM before kernel entry; this
must be handled without issuing state-changing commands. Device failure,
short output, malformed response, or timeout must fail closed. Candidate
availability must not itself make the entropy manager cryptographically
ready.

## Decisions and prerequisites

- **Detection:** TPM 2.0 detected by Linux sysfs; classified detected but
  inaccessible.
- **Transport:** ACPI CRB (`tpm_crb_acpi`); exact table start method/regions
  unknown.
- **Type:** likely firmware TPM, possibly Intel PTT; not established. A
  discrete or virtual TPM is not ruled out.
- **Capabilities:** unknown; `tpm2_getcap` denied.
- **GetRandom:** not executed; no returned bytes.
- **Integration:** technically feasible for a reviewed, narrowly supported
  CRB profile, but platform and vendor evidence insufficient.
- **Entropy trust:** **not established; vendor/implementation evidence
  required**. No approval or entropy credit.

Before a trust review: obtain an authorized read-only capability report;
identify exact device type/vendor/firmware; obtain vendor RNG/DRBG and reseed
documentation plus applicable evaluation/certification scope and advisories;
state firmware, hypervisor, physical attacker, snapshot/clone, and device
substitution assumptions; then separately review entropy input and CSPRNG
requirements. A future driver also needs ACPI/CRB, response-validation,
timeouts, recovery, malformed-device, and emulator tests before production
use.

## References

- [TCG TPM 2.0 Library specification page](https://trustedcomputinggroup.org/resources/documents/tpm-library-specification) — Version 185 (March 2026) and errata index at review date.
- [TCG Part 1 Architecture, Version 185 PDF, §8.4.11](https://trustedcomputinggroup.org/wp-content/uploads/Trusted-Platform-Module-2.0-Library-Part-1_Architecture-V185-RC4_12Dec2025.pdf) — accessible PDF is labelled RC4 (Dec. 2025); use published final version/errata for implementation.
- [TCG Part 3 Commands, Version 185 PDF, §16.1](https://trustedcomputinggroup.org/wp-content/uploads/Trusted-Platform-Module-2.0-Library-Part-3_Commands-V185-RC2_10July2025.pdf) — accessible PDF is labelled RC2 (July 2025); check published final/errata before implementation.
- [TCG ACPI Specification](https://trustedcomputinggroup.org/resources/documents/tcg-acpi-specification).
- [Linux TPM CRB driver source](https://github.com/torvalds/linux/blob/master/drivers/char/tpm/tpm_crb.c) and [Linux TPM security documentation](https://kernel.org/doc/html/latest/security/tpm/tpm-security.html).
- [Intel PTT overview](https://www.intel.com/content/www/us/en/support/articles/000094205/processors/intel-core-processors.html) — general PTT information; does not identify this TPM.
- Local read-only discovery commands listed above, collected 2026-11-01.

## Boundary

No kernel source, tests, or build behavior changed. No TPM provider or
automatic source mixing exists. `kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` remain false; production TCP routing,
transmission, timers, and syscalls remain disabled.

Phase 15.6H adds a source-backed CRB transaction and staged implementation
proposal in [`tpm2-crb-driver-design.md`](tpm2-crb-driver-design.md). It does
not resolve the unreadable host TPM2 table: its start method, control/data
addresses and capacities remain unknown. No driver or TPM command was added
or run.
