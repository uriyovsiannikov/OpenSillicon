# Entropy trust and boot-chain assessment

**Phase:** 15.6C — documentation-only
**Assessment date:** 2026-11-01
**Decision:** No current supported configuration justifies CSPRNG readiness.
Keep `kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` false.

## Scope and method

Inspected the current entropy manager and providers, Limine handoff/request,
boot order, PCI and ACPI code, VirtIO QEMU profile, entropy tests, CSPRNG
design, and TCP ISN gate. Checked the development host's reported CPU model
and flags and reviewed primary specifications and vendor references below.
No runtime test, random-instruction experiment, firmware query, TPM query,
or entropy-quality measurement was performed in this documentation phase.

A structurally valid response proves formatting or device transfer only; it
does not prove unpredictable bytes. This report distinguishes code facts,
specification guarantees, vendor descriptions, observations, and assumptions.

## Current architecture

| Source | Actual behavior | What availability proves | Policy status |
|---|---|---|---|
| Limine | Pinned v12.9.6; protocol header commit `6e85ddd6f928b5b512015c4d47d1cfce2fd28079`. Request revision 0, four 64-bit words. Inspector validates revision/count, HHDM and overflow-safe ranges, and bootloader-reclaimable memory; copies at most four words. Manager keeps max 32 bytes. | Bounded best-effort response passed structural checks. | Untrusted candidate; no entropy credit. |
| RDRAND | CPUID.01H:ECX[30] before instruction; bounded 10 attempts; checks CF; one-shot init and permanent failure latch; max 256 bytes/request, all-or-nothing output. No pool or software DRBG. | Instruction returned data according to its interface at the time. | Raw output; not approved as seed source. |
| RDSEED | CPUID max-basic-leaf check, then leaf 7/subleaf 0 EBX[18]; one-shot detection; at most 10 attempts per 64-bit word with `pause`; CF-checked; max 256 bytes/request, all-or-nothing. Initialization does not execute RDSEED. | CPU advertises the instruction, and a CF=1 read supplied a valid destination. Retry exhaustion means temporary unavailability. | Separate untrusted candidate; no entropy credit or trust promotion. |
| VirtIO RNG | Optional QEMU-only transitional legacy PCI device `1af4:1005`; max 256 bytes, one in-flight request, bounded polling. Test profile uses host `rng-random` backend and `virtio-rng-pci,disable-modern=on`. Normal boot does not initialize/read it. | Device completed the guest protocol and returned a checked length in the previously reported QEMU run. | Host/hypervisor candidate; not approved. |
| Entropy manager | Explicit source selection, no fallback, max 256 bytes, source and length provenance, unchanged output on failure. Temporary failures can be retried; permanent failures stay latched. RDSEED has a stable, separate source ID. | Candidate source status only. | `kernel_entropy_cryptographic_ready()` is hard-coded false. `TRUSTED` is reserved and never set in production. |

Boot order is Limine inspection, raw RDRAND initialization, then manager
initialization before bootstrap allocation. The stack copy of Limine data is
cleared after manager copy. VirtIO probing is much later and only behind
`OPEN_SILLICON_ENABLE_QEMU_VIRTIO_RNG`. Its sample is cleared and reported
uncredited. Optional provider failure does not stop boot. The normal build
does not enable this QEMU gate.

Candidate availability, trusted input, conditioned input, and CSPRNG
readiness are distinct concepts.

## Limine and boot-chain trust

The Limine Entropy Feature returns a requested count of 64-bit values, subject
to an implementation-defined limit, and an HHDM pointer in bootloader-
reclaimable memory. The protocol explicitly calls them best-effort: Limine
uses its strongest available sources but may fall back to weaker sources
such as a timing-seeded PRNG. It says they are suitable for seeding an RNG,
but gives no guarantee of hardware-grade entropy. The kernel's bounded
validation establishes memory safety/ownership, not unpredictability.

The protocol does not authenticate the bootloader or this response. A
malicious loader can select deterministic/replayed bytes or alter the loaded
kernel and policy. Pinned binary provenance in `limine/PROVENANCE.md` does
not attest runtime values. The repository pins binary artifacts and does not
contain or audit the bootloader's entropy implementation; the source behind
a particular response cannot be determined. The protocol itself disclaims a
hardware-grade guarantee.

Limine's x86-64 UEFI handoff exits EFI boot services before kernel entry.
The kernel requests firmware type, HHDM, memory map, and RSDP; it does not
request the EFI System Table or TPM event log. Header declarations for those
features are not active providers. EFI boot-service protocols cannot be
called after `ExitBootServices`. Limine can copy a TPM event log before exit
if requested, but an event log is measurement history, not random output.

**Finding:** Limine output can be supplemental candidate data, not an
approved seed source alone. Trusted boot-chain integrity helps against loader
substitution, but does not strengthen the protocol's source guarantee.

## RDRAND and RDSEED

Intel defines RDRAND CPUID.01H:ECX[30] and RDSEED CPUID.07H:EBX[18]. The
provider checks the maximum basic leaf before querying leaf 7, subleaf 0, and
does not restrict detection by vendor string. Initialization only caches
feature support; it does not consume a seed. CF=1 makes the destination valid;
CF=0 means that instruction supplied no valid seed. Success is never inferred
from a nonzero value. The provider retries at most 10 times per word with
`pause`; this is the project's bounded policy, not an Intel-prescribed RDSEED
retry count. Exhaustion is temporary and does not disable the provider.

Intel describes RDRAND as output of its on-chip DRNG deterministic generator
and RDSEED as output supplied by an Enhanced NRBG designed around NIST
SP 800-90B/90C, intended to supply seed material to software PRNGs. These are
vendor architecture descriptions, not independent evidence of source
quality, firmware integrity, implementation health, or a virtualized source.
CPUID establishes only advertised feature support; CF=1 establishes only
that the instruction supplied a valid destination at that execution. Neither
establishes an entropy amount, authenticates silicon or microcode, or resists
a hypervisor that controls or observes the guest. RDRAND and RDSEED are
distinct interfaces but share a processor trust boundary and are not assumed
independent.

The provider API is `kernel_rdseed_init()`, `kernel_rdseed_status()`,
`kernel_rdseed_word()`, and `kernel_rdseed_bytes()`. Requests are allocation-
free, capped at 256 bytes, all-or-nothing, and preserve output on failure.
The provider follows the current single-CPU serialization convention and is
not for interrupt context. `RDSEED_TESTING` adds injectable CPUID/instruction
backends only to host tests. The QEMU profiles validate feature detection and
bounded instruction behavior, not physical entropy quality or independence
from the hypervisor.

The current environment's `lscpu` reports Intel Core Ultra 5 225H and
`rdrand`/`rdseed` flags. This is host OS feature exposure, not an
OpenSillicon boot, physical attestation, firmware/microcode audit, or promise
for future supported CPUs. QEMU profiles vary exposed flags; their successful
VirtIO transfer says nothing about entropy quality.

Intel INTEL-SA-00320 / CVE-2020-0543 (SRBDS) documents transient disclosure
of RDRAND/RDSEED results on affected processors and microcode mitigations.
It is a confidentiality issue, not proof that all generated values are
predictable. It shows that processor generation and microcode matter. This
audit does not say the 225H is affected; current code checks no mitigation
state and has no CPU/microcode allowlist.

RDRAND might be accepted only in a profile that explicitly trusts the CPU
vendor/implementation, firmware/microcode, and virtualization layer. Project
policy has not approved those assumptions; RDRAND alone is not approved.
RDSEED is a plausible future candidate due to its seeding purpose, but still
needs CPUID gating, bounded retries, failure latching, vendor/VM analysis,
and validation. Neither instruction is an unconditional trust guarantee.

## TPM 2.0

`TPM2_GetRandom` (TPM 2.0 Library Part 3 section 16.1) requests random octets from
the TPM RNG. The response is length-delimited and can return fewer bytes
than requested when the request exceeds available/representable data. A
caller must validate response code, framing, and returned length and bound
retries. The TPM architecture describes an RNG module with entropy collection,
state and mixing. Successful command completion proves device-protocol
response, not source quality, device authenticity, firmware integrity, or
trust in a virtual TPM's host.

No TPM command transport/driver or TPM2 ACPI table processing exists.
ACPICA is initialized; the OS explicitly retrieves APIC and MCFG tables,
not TPM2. There is no TIS/CRB transport. Limine's header has an optional TPM
event-log structure, but the kernel does not request it and it is not an
RNG. Generic PCI enumeration is not TPM discovery; PC-client TPMs commonly
use ACPI-described TIS/CRB interfaces. The current QEMU profiles do not add
a TPM or `swtpm` backend. This audit cannot establish TPM presence on the
developer host or any future guest.

Discrete TPM support needs discovery, locality, transport, command framing,
timeouts/reset, and platform tests. Firmware TPM trusts more of platform
firmware; vTPM trusts the hypervisor. Either could be evaluated under a
specific platform policy, but neither exists here. A TPM alone does not
repair a compromised boot chain.

## EFI RNG

UEFI `EFI_RNG_PROTOCOL.GetRNG()` returns requested bytes and offers algorithm
discovery; this is not a universal source-quality guarantee. Firmware/source
trust remains necessary. Since Limine exits boot services and the kernel
does not request the EFI System Table or implement an EFI protocol caller,
EFI RNG is not safely available after handoff. A future path requires Limine
to acquire/copy bounded output before exit and pass it through a handoff
feature, or a separate EFI loader. The bytes would still be candidates until
the firmware source policy is approved. EFI Runtime Services do not preserve
a boot-service RNG protocol after exit.

## VirtIO and virtualization threats

The driver targets QEMU transitional legacy PCI RNG (`1af4:1005`), not modern
VirtIO transports or generic hardware. It uses a bounded legacy queue, 256
byte read cap, one request in flight, bounded polling, and identity-DMA /
coherent-x86 assumptions. The QEMU profile uses `-machine q35`, a
`rng-random` host backend, and `virtio-rng-pci,disable-modern=on`. The absent
profile omits the device. Ring validation proves protocol delivery, not host
entropy. No backend/device means unavailable; normal boot does not initialize
it.

| Threat case | What a successful request proves | Remaining issue |
|---|---|---|
| Honest host with initialized host RNG | QEMU delivered bytes. Under an explicit host/QEMU trust assumption, plausible candidate input. | Guest cannot verify host RNG readiness/configuration or independence; policy not approved. |
| Misconfigured deterministic backend | Data moved through the device. | Output may repeat or be predictable. |
| Malicious hypervisor | Hypervisor supplied bytes. | It can choose, observe, replay, correlate data and inspect guest state. Not trusted. |
| Snapshot/rollback | Device can still return bytes after resume. | Restored DRBG state/output may repeat; no VM generation signal or mandatory reseed exists. |
| VM clone | Clones can receive bytes. | State or streams may be copied/correlated. |
| Host RNG failure | Device may return, short-return, fail, or stall. | Success does not reveal source quality; failures must prevent trusted initialization. |

A controlled VM profile could accept VirtIO only if it explicitly trusts
host OS RNG, QEMU/hypervisor, backend configuration, and clone/rollback
handling. This is a deployment assumption, not a device guarantee, and does
not cover a malicious hypervisor.

## Threat-model matrix

| Model | Attacker and required trust | Dependencies/candidates | Readiness today? |
|---|---|---|---|
| Physical, trusted firmware | Attacker lacks firmware/kernel control; trust CPU vendor/silicon, firmware/microcode and bootloader. | RDRAND/RDSEED share CPU boundary; Limine may use CPU/firmware or timing. | **No.** RDSEED is implemented as a candidate, but no platform policy or CSPRNG exists. |
| Physical, untrusted firmware | Attacker alters UEFI/loader/handoff; independent kernel and CPU trust root required. | Limine/EFI attacker-controlled; CPU may be separate only if kernel and CPU remain trusted. | **No.** No verified boot policy or approved independent source. |
| QEMU, honest host | Exclude host attacker; trust host RNG, backend, QEMU and VM operations. | VirtIO host random backend; Limine/CPU may overlap host sources. | **No under current policy.** Could be a narrow profile after operational assumptions are enforced. |
| QEMU, malicious host | Hypervisor controls features, device, guest memory/state and scheduling. | All guest-visible sources may be chosen/replayed/observed. | **No.** A guest cannot resist a controlling hypervisor. |
| VM snapshot/clone | Host can restore/copy state; post-restore source freshness required. | Saved generator state and uncertain virtual source freshness. | **No.** No detection or required reseed. |
| Bootloader compromised | Loader controls handoff and could change kernel; trust rooted beneath it. | Limine bytes/map attacker-controlled; code integrity also lost. | **No.** Structural checks/provenance do not authenticate runtime loader. |

No current configuration supports an unconditional CSPRNG `READY` claim.
A future narrow build profile can define a different trust boundary but must
state it and fail closed outside it.

## Source independence and combination

Independence is unproven. Limine may use CPU or firmware and duplicate
RDRAND/RDSEED inputs. RDRAND and RDSEED are separate interfaces but share
vendor, microcode, hardware and likely DRNG components. VirtIO depends on
host RNG, hypervisor, backend and VM lifecycle; guests may receive correlated
bytes or cloned state. Every source can be deterministic/replayed or
attacker-controlled within its boundary.

Mixing tagged source bytes, conditioning input, estimating min-entropy,
approving entropy input, and constructing a complete RBG are distinct tasks.
A hash/HMAC can combine or condition input, but cannot make known or
attacker-chosen input unpredictable. Mixing may improve robustness only if
at least one independent input is unpredictable and the combiner is
appropriate. That premise is not established here. No entropy estimate is
assigned.

## Deployment strategies

| Strategy | Complexity/portability | Trust and failure assumptions | Assessment |
|---|---|---|---|
| A — hardware (RDSEED/TPM) | RDSEED is small but needs feature/vendor policy; TPM needs ACPI discovery, TIS/CRB, command and timeout/reset work. | Trust CPU/firmware/microcode or TPM and platform wiring; absent/failure means unavailable. | Physical-profile candidate after evidence; not implemented. |
| B — firmware/bootloader | Low kernel effort if bounded seed handoff; EFI requires pre-exit integration or loader change. | Trust firmware/loader and documented source. Current Limine contract is best-effort. | Limine data is supplemental only. |
| C — hypervisor VirtIO | Existing QEMU path helps testing; production transport portability remains poor. | Trust host RNG, QEMU, backend and VM lifecycle. | Plausible only for explicit trusted-host deployment; fails malicious-host model. |
| D — multiple independent sources | Highest policy/integration effort, provenance and independence review. | Must prove sources aren't correlated or under one attacker. | Not currently established; combination is not automatic improvement. |
| E — no approved source | Minimal and portable. | Return unavailable for cryptographic use; boot continues. | **Recommended default now.** |

For controlled VM use, C could be considered with an explicit host trust
assumption and clone/rollback mitigation. Evaluate A separately for physical
platforms. B may supplement. D is not inherently stronger. E remains default.

## NIST standards

As of 2026-11-01, official NIST pages list:

- **SP 800-90A Rev. 1 Final (June 2015):** hash/block-cipher DRBG mechanisms,
  including HMAC_DRBG and CTR_DRBG. Rev. 2 is an initial preliminary draft
  dated 2025-09-04, not a final replacement.
- **SP 800-90B Final (January 2018):** entropy-source design, requirements,
  and validation tests. NIST notes errata identified for a future update.
- **SP 800-90C Final (September 2025):** RBG constructions composing 90A
  DRBGs and 90B sources, with RBG1, RBG2, RBG3, and RBGC classes.

90A informs a future mechanism, 90B source characterization, and 90C source /
DRBG composition. Citing them does not approve sources or make this project
compliant or validated.

## Prerequisites for a future DRBG implementation

| Requirement | Status | Evidence/remaining work |
|---|---|---|
| Approved source strategy and deployment profile | **Unsatisfied** | No trust policy; choose physical and VM threat boundaries. |
| Source trust, independence and failure rules | **Unsatisfied** | Provenance exists, but no source qualifies; correlations unknown. |
| Minimum seed strength/construction | **Unsatisfied** | Select 90C construction/90A mechanism and meet requirements with defensible input; no byte-to-entropy assumption. |
| Bounded boot init | **Partially satisfied** | Candidate collection bounded and nonfatal; trusted instantiation absent. |
| No-source fail-closed | **Partially satisfied** | Manager has no fallback/readiness false; future DRBG state machine absent. |
| Runtime reseed/source failure | **Unsatisfied** | No DRBG counters, reseed transitions or recovery policy. |
| VM snapshot/clone | **Unsatisfied** | No generation signal or mandatory fresh reseed. |
| CPU/microcode policy | **Partially satisfied** | RDRAND and RDSEED check advertised features and carry status with bounded attempts; no allowlist/advisory gate or platform trust policy. |
| TPM/EFI source, if selected | **Future hardware/bootloader prerequisite** | No TPM driver; EFI services exited. |
| State protection, concurrency, overflow, wipe | **Unsatisfied** | No DRBG state; reviewed wipe and SMP-safe design needed before SMP. |
| Algorithm/policy/integration tests | **Partially satisfied** | Candidate tests exist, not DRBG/source-quality tests; add standard vectors, sanitizers, QEMU tests. |
| Independent review | **Unsatisfied** | No trust profile/implementation reviewed. |

Before implementation: approve source assumptions and seed requirements;
define boot/no-source, reseed/failure and rollback policy; specify state
protection; add published vectors, policy tests, sanitizers and production
isolation checks; obtain independent review. If these gates fail, remain
unavailable.

## TCP ISN boundary

`tcp_core_live_isn_available()` returns `false`. No TCP files or behavior
changed. Candidate bytes are not connected to ISNs. Production TCP dispatch,
TX, timers, syscalls and live connections remain disabled. A future CSPRNG is
necessary but not sufficient for separately reviewed ISN generation.

## Phase 15.6D provider update

The earlier Phase 15.6C statement that RDSEED was absent was accurate at that
time. Phase 15.6D added a provider without changing the trust decision. Host
tests inject CPUID registers and instruction results, including CF=0 with a
nonzero fake destination, CF=1 with a zero result, exact retry exhaustion,
recovery after temporary failure, output atomicity, and maximum request size.
The entropy-manager test verifies explicit RDSEED selection, provenance,
temporary status/retry, and no RDRAND fallback.

QEMU 10.2.2 accepted `max,+rdseed,+rdrand`, `max,-rdseed,+rdrand`,
`max,+rdseed,-rdrand`, and `max,-rdseed,-rdrand`. Guest detection matched
each profile, and both RDSEED-enabled profiles completed an eight-byte
bounded request. Each harness terminated QEMU after its serial marker; QEMU
did not exit through normal guest shutdown. This validates instruction
exposure and code paths in that emulator, not entropy quality, physical
hardware, or hypervisor independence. A transient CF=0 result was forced in
the host fake backend, not observed in QEMU.

The normal kernel object includes the provider and has no fake-backend or
QEMU-test symbols. Only the dedicated QEMU test build enables the request
probe macro. Candidate status continues to be untrusted, and
`kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` remain false.

## Phase 15.6E — host RDSEED reliability assessment

The validation host reported Fedora Linux 44 Workstation, kernel
`7.2.13-200.fc44.x86_64`, x86-64, Intel GenuineIntel, Core Ultra 5 225H,
family 6/model 197/stepping 2. CPUID reported maximum basic leaf 35 (0x23),
RDSEED and RDRAND present, and the hypervisor-present bit clear. The `lscpu`
flags also listed both instructions. `systemd-detect-virt`, including its VM
and container modes, returned `none`; DMI reported HONOR / FMB-P / FMB-P-PCB.
These observations are consistent with bare metal, but are not an attestation
and can be masked by a sufficiently capable virtualizer. The execution
environment is therefore classified **unknown/ambiguous**, not confirmed
physical. The observed CPU model is the expected target.

`tools/rdseed_hw_test.c` is a separate unprivileged Linux executable. It
checks the CPUID maximum leaf and RDSEED bit before executing the instruction,
uses a 64-bit destination with adjacent CF capture, and issues exactly 100
requests per run. Each request makes at most 10 attempts with `pause`. It
prints CPUID/aggregate operational data only; the returned words are neither
printed, hashed, persisted, nor transmitted. The native wrapper is separate
source from the kernel wrapper, but both compile to a 64-bit RDSEED followed
by `setb` (the CF-set alias for `setc`). Native code does not disable OS
interrupts; the kernel provider follows its existing single-CPU local
interrupt-masking convention around the bounded request.

Six 100-request runs were made; the first three used millisecond-resolution
timing output, and the final three used microseconds:

| Run | Attempts | CF=1 valid results | CF=0 results | 10-attempt exhaustions | Elapsed |
|---|---:|---:|---:|---:|---:|
| 1 | 278 | 83 | 195 | 17 | under 1 ms (reported as 0 ms) |
| 2 | 270 | 83 | 187 | 17 | under 1 ms (reported as 0 ms) |
| 3 | 299 | 84 | 215 | 16 | under 1 ms (reported as 0 ms) |
| 4 | 301 | 83 | 218 | 17 | 140 µs |
| 5 | 289 | 83 | 206 | 17 | 140 µs |
| 6 | 300 | 82 | 218 | 18 | 146 µs |
| **Total** | **1,737** | **498** | **1,239** | **102** | — |

All six processes exited 0, reported
`PASS_WITH_TEMPORARY_UNAVAILABLE`, and experienced no unexpected fault or
crash. The status means the bounded instruction path produced valid results
and reported exhausted requests as temporary; it does not mean all requests
completed. The observed CF=0 frequency and exhaustion counts are operational
measurements under these rapid short bursts only. They are not entropy
estimates, statistical randomness tests, or evidence of a defective source.
The optional longer run was not performed because the short runs already
showed repeated retry exhaustion; no attempt was made to deliberately drain
the hardware source.

The provider's existing host-fake tests separately verify zero with CF=1,
nonzero fake destination with CF=0, exact retry bounds, unchanged output,
recovery, manager temporary status, and provenance. These tests do not
exercise the physical kernel provider. No safe invocation path exists to call
that provider in the currently running Fedora kernel, and this phase did not
reboot or replace the active OS. QEMU tests remain emulator-only. Thus the
native CPU instruction ran on the available host environment, while
production kernel integration on physical hardware remains unverified.

The observed transient failures fit the provider contract: it returns
temporary unavailability after ten consecutive CF=0 results, preserves the
caller's output, and can be retried later. A 256-byte kernel read can make up
to 32 words times 10 instruction attempts under the same request guard; this
is bounded but not latency-guaranteed. No retry policy change is made from
these host observations. RDSEED remains an untrusted candidate with no
entropy credit, no CSPRNG readiness, and no TCP ISN use.

## References

The Phase 15.6H CRB protocol and implementation proposal, including bounded
command handling and its trust boundary, is in
[`tpm2-crb-driver-design.md`](tpm2-crb-driver-design.md). The source policy is
unchanged: TPM is unapproved and receives no entropy credit.

- [Pinned Limine Boot Protocol — Entropy Feature](https://github.com/limine-bootloader/limine-protocol/blob/6e85ddd6f928b5b512015c4d47d1cfce2fd28079/PROTOCOL.md#entropy-feature) — response/best-effort wording; x86-64 handoff says EFI services exited. Local provenance: `limine/PROVENANCE.md`.
- [Limine v12.9.6 release](https://github.com/limine-bootloader/Limine/releases/tag/v12.9.6).
- [Intel DRNG Software Implementation Guide Rev. 2.1 (2018)](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-digital-random-number-generator-drng-software-implementation-guide.html) and [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
- [Intel INTEL-SA-00320 / SRBDS](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00320.html) and [technical guidance](https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/advisory-guidance/special-register-buffer-data-sampling.html).
- [TCG TPM 2.0 Library page](https://trustedcomputinggroup.org/resources/documents/tpm-library-specification): at review date it lists Version 185 (March 2026); Part 3 section 16.1, `TPM2_GetRandom`. Also see [Part 3 Rev. 1.83](https://trustedcomputinggroup.org/wp-content/uploads/TCG_TPM2_r1p83_Part3_Commands_19March2024.pdf); check current errata before implementation.
- [UEFI Specification 2.11, section 37.5](https://uefi.org/specs/UEFI/2.11/37_Secure_Technologies.html) and [section 7](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html).
- [NIST SP 800-90A Rev. 1 Final](https://csrc.nist.gov/pubs/sp/800/90/a/r1/final); [Rev. 2 preliminary draft](https://csrc.nist.gov/pubs/sp/800/90/a/r2/iprd); [SP 800-90B Final](https://csrc.nist.gov/pubs/sp/800/90/b/final); [SP 800-90C Final](https://csrc.nist.gov/pubs/sp/800/90/c/final).
- [Intel Core Ultra 5 225H specs](https://www.intel.com/content/www/us/en/products/sku/241749/intel-core-ultra-5-processor-225h-18m-cache-up-to-4-90-ghz/specifications.html) identify the CPU; feature flags above were from host `lscpu`.

## Phase 15.6F — bounded availability and caller-driven retry

The entropy manager now reports capability and acquisition state separately.
`UNSUPPORTED` means an instruction feature is absent; `DEVICE_ABSENT` means
the optional VirtIO device was not enumerated; `SUPPORTED` means a bounded
read may be attempted but no recent data result has established candidate
availability; `CANDIDATE_AVAILABLE` means material was present or a provider
read recently succeeded; `TEMPORARILY_FAILED` means the last bounded attempt
could not complete; `EXHAUSTED` means the one-shot Limine staging copy was
consumed; and `PERMANENTLY_DISABLED` means the provider/transport has latched
itself unavailable. `UNAVAILABLE` covers a source with no candidate material
for this boot. None of these states is a trust level.

Acquisition is always explicitly requested for one source. There is no
internal request queue, background poller, timer, wait, fallback, or source
combination. Candidate reads are capped at 256 bytes. Manager and providers
are allocation-free and bounded, serialized by the existing UP local-IRQ
guard/busy flags, and not safe for interrupt callers or SMP. When all
providers are unsupported, absent, exhausted, or temporarily failed, callers
receive the corresponding error promptly; boot and unrelated kernel paths do
not wait for entropy.

RDSEED retains ten instruction attempts per 64-bit word. CPUID now reports
`SUPPORTED`, not `AVAILABLE`; only a successful CF=1 acquisition changes the
manager to `CANDIDATE_AVAILABLE`. CF=0 exhaustion is temporary. The explicit
`kernel_entropy_retry_source(RDSEED)` call re-arms a later bounded read; it
does not poll or increase the instruction retry budget. RDRAND has the same
ten-attempt bound: initialization/request exhaustion is now
`TEMPORARILY_UNAVAILABLE`, and a caller must explicitly request a bounded
`kernel_random_retry()` through the manager before another read. Unsupported
CPUID is reported separately and cannot be retried as if it were temporary.
No RDRAND fallback is used for RDSEED or vice versa.

For a 256-byte instruction-provider read, the worst case is 32 words × 10
instruction attempts (320 attempts), plus at most ten attempts for a separate
RDRAND retry probe. The provider does not nest a retry probe inside a read.
The VirtIO driver accepts at most 256 bytes and polls its used ring no more
than `VIRTIO_RNG_POLL_LIMIT` (1,000,000 iterations) for that single request;
it has one in-flight request and no wait queue. The Limine staging area is at
most four 64-bit values (32 bytes). These are finite work bounds, not latency
guarantees.

VirtIO is reported `SUPPORTED` only after the gated driver initialization
succeeds; successful manager reads establish candidate availability. A busy
single-request slot is temporary and retryable by a later explicit caller.
The current driver disables itself after timeout or malformed completion and
does not permit reinitialization in that boot, so those errors are accurately
recorded as permanently disabled rather than advertised as retryable. If
`quiesce_and_release()` cannot prove reset and bus-master quiescence, the
driver quarantines device-visible memory and remains unavailable. A missing
device is `DEVICE_ABSENT`. Limine remains one-shot: after all copied bytes are
consumed, its status is `EXHAUSTED` and it is never replenished.

The explicit retry API has no clock-based cooldown. Repeated callers can
repeat one bounded provider attempt each, so integration code must avoid a
tight retry loop. There are currently no production consumers scheduling
these retries; boot calls `kernel_random_init()` and `kernel_rdseed_init()`
once, while VirtIO discovery/read is behind its QEMU test gate. The gated
VirtIO QEMU probe is the only kernel-side candidate retrieval. The manager
remains single-CPU only. Availability/provenance does
not modify the trust decision: all sources remain untrusted candidates,
`kernel_entropy_cryptographic_ready()` remains false, and
`tcp_core_live_isn_available()` remains false.

## Phase 15.6G — TPM 2.0 host feasibility

Read-only discovery on 2026-11-01 found a Linux-visible TPM 2.0 device:
`/sys/class/tpm/tpm0/tpm_version_major` reports 2, and Linux binds it through
ACPI CRB driver `tpm_crb_acpi` at `INTC7001:00` (HID `INTC7001`, CID
`MSFT0101`). Both `/dev/tpm0` and `/dev/tpmrm0` exist, but are inaccessible to
the current unprivileged user. Read-only `tpm2_getcap` calls failed with
permission denied. No GetRandom command was issued. The implementation is
likely a firmware TPM, possibly Intel PTT, but exact type is not established.

The current TCG TPM 2.0 Library v185 publication (March 2026) describes the
TPM RNG as an entropy-source/collector and state/DRBG/RNG architecture. Part 3
§16.1 defines `TPM2_GetRandom` as a length-delimited response with an
implementation-dependent output maximum; output can be limited by available
RNG data. This does not identify this host's implementation or prove entropy
quality. Manufacturer, firmware revision, capabilities, command support,
certification scope, and RNG/reseed behavior remain unknown because the TPM
could not be queried. See `docs/security/tpm2-feasibility-assessment.md` for
host evidence and a possible future CRB-provider design.

Decision: a narrowly scoped TPM provider appears technically feasible for a
supported ACPI CRB profile, but platform and vendor trust evidence is
insufficient. No TPM output was obtained or tested; no entropy credit is
assigned. TPM remains unapproved. `kernel_entropy_cryptographic_ready()` and
`tcp_core_live_isn_available()` remain false.
