# Kernel CSPRNG design proposal

**Status:** design only, Phase 15.6B, with provider status updated in Phase
15.6D. No CSPRNG, entropy pool, entropy-credit accounting, or production ISN
generator is implemented by this document. The current cryptographic-
readiness and TCP live-ISN gates remain closed.

## Scope and current architecture

The current code has three candidate providers and a source inventory:

| Provider | Actual implementation | What availability means | Trust decision today |
|---|---|---|---|
| Limine | `boot_entropy_inspect()` validates revision/count, HHDM/range arithmetic and bootloader-reclaimable memory, then copies up to four 64-bit values. `kernel_entropy_init()` copies these into at most 32 bytes of kernel-owned staging. | A bounded best-effort response passed structural checks. The protocol permits weaker fallback sources; the kernel cannot authenticate how values were generated. | Untrusted candidate; no entropy credit. Consumed staging is wiped; otherwise it remains kernel-owned for boot lifetime. |
| RDRAND | `kernel_random_init()` checks CPUID.1:ECX[30], performs a bounded probe, checks the instruction carry flag, and latches failure. Requests are capped at 256 bytes and all-or-nothing. | The advertised instruction returned words successfully at probe/request time. It does not establish source quality or trust. The API returns raw instruction output, not a kernel DRBG. | Untrusted raw hardware output; not approved as the sole seed source. |
| VirtIO RNG | Optional, explicitly gated QEMU profile; transitional legacy PCI provider only, one request in flight, bounded to 256 bytes and bounded polling. | The device completed a validated request. The host/hypervisor controls the bytes. Ordinary production boot does not probe or read it. | Untrusted host-provided candidate; no production trust approval. |
| RDSEED | `kernel_rdseed_init()` checks max basic CPUID leaf, then CPUID.7.0:EBX[18]; reads use a 64-bit destination, check CF, retry at most 10 times per word with `pause`, and cap requests at 256 bytes. | CPUID advertises the instruction; CF=1 indicates a valid destination for that execution. Retry exhaustion is temporary. | Separate untrusted hardware candidate; no trust approval or entropy credit. |

`kernel_entropy_get_candidate()` requires the caller to choose one source,
preserves source identity, caps requests at 256 bytes, reports the byte count,
does not fall back, and leaves output unchanged on failure. The manager's
`kernel_entropy_cryptographic_ready()` always returns false. A candidate,
source status, successful device transfer, or successful RDRAND instruction
is not a DRBG readiness signal. The source inventory is serialized with local
interrupt masking/busy state for the current single-CPU model; it is not an
SMP lock and is not for interrupt context.

The boot order initializes raw RDRAND and the manager before the bootstrap
allocator. VirtIO probing is later and only in its QEMU test profile. Optional
provider failure does not fail boot. No current implementation supplies an
approved seed to a CSPRNG because no CSPRNG exists.

RDSEED detection and initialization are also allocation-free and happen before
entropy-manager initialization. Detection checks the architectural feature
bit without restricting the CPU vendor. It does not execute the instruction.
The selected retry limit is an OS policy; Intel describes transient
underflow and recommends a finite retry bound, but does not prescribe this
count for RDSEED. A valid all-zero word is accepted when CF is set. Exhaustion
sets temporary-unavailability status, and a later successful read returns
the provider to available. There is no fallback to RDRAND or another source.
Candidate output remains explicitly selected and source-tagged by the
manager. RDSEED and RDRAND are not presumed independent because both reside
inside the same processor trust boundary.

The host RDSEED tests use fake CPUID/instruction callbacks and cover feature
boundary detection, CF=0 retries, valid zero output with CF=1, temporary
unavailability and recovery, bounds, and failure atomicity. QEMU 10.2.2 tests
CPU profiles with RDSEED/RDRAND independently toggled; enabled profiles
completed the bounded guest request. These results validate software and
emulator feature exposure, not seed unpredictability or physical hardware
quality. The harness intentionally terminates QEMU after a guest serial
marker.

Phase 15.6E ran the separate native RDSEED diagnostic on a Fedora 44 host
reporting Intel Core Ultra 5 225H (family 6/model 197/stepping 2), maximum
basic CPUID leaf 35, RDSEED/RDRAND present, and no CPUID hypervisor bit. OS
virtualization checks also reported none, but the environment is conservatively
classified unknown/ambiguous because those checks are not hardware
attestation. Six bounded 100-request bursts yielded 498 CF=1 results, 1,239
CF=0 results, and 102 ten-attempt request exhaustions, with no fault. This
shows the API must surface temporary unavailability under burst load; the
counts do not estimate entropy and do not change source trust. The native
binary did not invoke the kernel provider, and physical bare-metal kernel
execution remains unverified.

## Candidate constructions

NIST SP 800-90A Rev. 1 specifies Hash_DRBG, HMAC_DRBG, and CTR_DRBG mechanisms.
SP 800-90B covers entropy-source design and validation; SP 800-90C specifies
constructions combining sources and DRBGs. These documents describe
constructions and requirements, not proof that this machine has a trustworthy
source. RFC 8439 specifies the ChaCha20 cipher, not a complete DRBG.

| Candidate | Advantages for this freestanding C11 kernel | Costs and security questions |
|---|---|---|
| ChaCha20-based DRBG | Portable scalar implementation, small fixed state, no AES hardware dependency, and good throughput. The ChaCha20 primitive has published test vectors. | RFC 8439 does not define a DRBG. State update, rekeying, counter limits, reseed and compromise recovery would need a separately specified construction and careful independent review. A primitive KAT does not validate a DRBG. |
| AES-256 CTR_DRBG | A defined SP 800-90A construction with an established test approach and compact key/counter state. Can use a portable implementation. | Requires AES and any needed derivation function to be implemented or imported and reviewed. AES-NI is optional; software fallback, side-channel behavior, feature dispatch, and duplicated implementations add complexity. Needs correct key/counter update and reseed handling. |
| HMAC_DRBG with SHA-256 | A defined SP 800-90A construction with a relatively small dependency set: SHA-256 and HMAC. Portable scalar C avoids CPU-specific dispatch. Its state is fixed and modest (K and V plus hash/HMAC scratch); implementation and known-answer tests are straightforward to isolate. Kernel demand is expected to be low-volume. | Requires correct SHA-256, HMAC, update/reseed logic, request limits, and state protection. It may be slower than a stream-cipher design. KATs cannot establish seed entropy or implementation security. State disclosure compromises the current state until a successful reseed; recovery behavior must be explicit. |

### Recommendation

For a later implementation, use **HMAC_DRBG with SHA-256**, following the
applicable SP 800-90A mechanism and SP 800-90C construction guidance, rather
than inventing a ChaCha20 DRBG around RFC 8439. This is an engineering fit,
not a security claim: the mechanism is specified, portable in this kernel's
freestanding C environment, does not rely on optional AES instructions, and
is suitable for low-volume kernel requests. Prefer a separately reviewed
implementation or a line-by-line reviewed implementation with published
vectors; do not add an ad hoc crypto framework. An independent security
review remains required.

This recommendation is conditional. There is no source approved today to
provide the required seed material. A correct DRBG seeded with predictable or
attacker-controlled input is not ready for cryptographic use.

## Threat model and source policy

The present platform is an experimental x86-64, single-CPU kernel. There is
no evidence from physical hardware testing about any provider's entropy
quality. A virtual-machine guarantee necessarily trusts the host and
hypervisor: a malicious hypervisor can observe or modify guest execution and
memory, replace device data, snapshot state, or replay an old VM image. A
malicious bootloader can likewise control boot handoff values. This design
does not claim protection against a compromised kernel, boot chain, CPU
vendor/firmware, or hypervisor.

| Source | Contribution policy proposed for a future design | What must not be inferred |
|---|---|---|
| Limine | May be mixed as tagged, domain-separated supplemental input if structurally valid. It is not a required seed source and receives no credited entropy under the current policy. | Non-empty or changing values do not prove unpredictability, authentication, or a minimum entropy amount. |
| RDRAND | Consider as supplemental input, or as an approved source only after a documented platform/vendor threat policy and health/failure review explicitly accepts it. Preserve CPUID gating, bounded attempts and sticky failure. | CPUID support and CF=1 show advertised support and successful output, not a healthy, independent, trustworthy entropy source. Virtualized RDRAND is under hypervisor control. |
| VirtIO RNG | Candidate/supplemental input only. A future platform profile could approve a trusted host/hypervisor as part of its threat model, with that assumption explicit. | A valid used-ring completion proves protocol data delivery, not entropy quality, host trust, independence, or physical randomness. |
| RDSEED | Implemented as a bounded, separate candidate provider in Phase 15.6D. | The instruction's architectural intent is seed material for software generators, but feature support and successful reads do not establish platform trust or solve VM/firmware assumptions. |

No source presently meets the project's approved-source policy. Do not assign
numeric entropy credits. Repeated or deterministic bytes, or multiple
correlated sources, do not add entropy. A future combiner should use
unambiguous source identifiers, lengths, and domain separation in the DRBG
instantiate/reseed inputs. Such mixing can improve robustness if at least one
input is independently unpredictable and the construction is sound; it
cannot prove that premise or rescue a threat model where every source is
attacker-controlled. Do not silently fall back from an approved source to an
unapproved one.

To claim readiness, the project must first approve a source profile that
states whom it trusts and how approved seed material is obtained. Possible
future routes include a deliberately trusted hardware/platform source or an
explicit deployment assumption trusting the host-provided source. Neither
route is established by current code. In a VM threat model that distrusts the
host, current sources are insufficient. Readiness must remain false until a
reviewed policy and implementation say otherwise.

## Initialization and generator lifecycle

The future generator must distinguish candidate collection from trusted
instantiation. Proposed states:

| State | Meaning and permitted behavior |
|---|---|
| `UNINITIALIZED` | Init has not run. No output. |
| `COLLECTING` | Bounded provider reads and policy checks are in progress. No output. It must not wait indefinitely. |
| `READY` | A reviewed DRBG was instantiated from seed material meeting the approved source policy. Bounded generation is permitted. |
| `RESEED_REQUIRED` | Output/age limit reached, a required provider became unhealthy, or an approved policy requires reseed. No output until a reseed succeeds. |
| `FAILED` | Internal invariant, state-integrity, counter-overflow, or unrecoverable DRBG failure. Sticky for this boot; no output. |
| `DISABLED` | No policy-approved source/configuration is available. No output. May be entered at boot and remain for the boot lifetime. |

Allowed paths are `UNINITIALIZED → COLLECTING → READY`; failed source policy
or no accepted source gives `COLLECTING → DISABLED`; successful reseed gives
`READY/RESEED_REQUIRED → READY`; reaching a limit gives `READY →
RESEED_REQUIRED`; internal fault gives any initialized state to `FAILED`.
No ordinary candidate status transitions to `READY`. A failed reseed leaves
the generator unavailable (`RESEED_REQUIRED`, or `FAILED` if state integrity
is uncertain). It never emits degraded output. Reinitialization with the
same seed is not a recovery procedure.

Before claiming 256-bit security strength, the seed path must provide the
required strength under the selected NIST construction and source policy;
256 candidate bytes are not equivalent to 256 bits of credited entropy. Seed
length, nonce/personalization handling, and source conditioning must follow
the selected standard, not an invented entropy estimate.

Proposed conservative initial engineering limits are a 256-byte maximum per
call (matching the existing provider bound), at most 1 MiB output or one hour
between successful reseeds, whichever occurs first, and a fixed bound on
reseed attempts. These are proposed policy limits, not NIST-mandated values;
they must be checked against the selected mechanism's limits and workload
before implementation. Monotonic time may enforce age but is not entropy. If
reseed is due and no approved source is available, generation stops. Every
counter addition must be overflow-checked; exhaustion fails closed.

There is no shutdown/restart contract today. A future reinitialization must
not reset state to a fixed value. Reset is permitted only after a new approved
seed and explicit lifecycle policy; otherwise the state remains disabled or
failed until reboot.

## Proposed kernel API

Names below are illustrative and are not declarations in the current tree:

```c
enum kernel_csprng_status kernel_csprng_init(void);
bool kernel_csprng_ready(void);
enum kernel_csprng_result kernel_csprng_generate(void *out, size_t length);
enum kernel_csprng_result kernel_csprng_request_reseed(void);
enum kernel_csprng_status kernel_csprng_status(void);
```

The API should not let callers select a source or read internal state. Source
provenance/status belongs in bounded diagnostic inspection, without exposing
bytes, keys, DRBG state, or per-request seed material. Errors should
distinguish invalid arguments, not-ready/disabled, reseed-required, and
internal failure. Requests above the fixed maximum fail atomically. Define a
zero-length request as a no-output success only while `READY`; not-ready state
still returns a readiness error. On any failure, leave caller output
unchanged. Generation should use bounded kernel scratch and copy to caller
storage only on success; no heap allocation is required.

The current kernel has no general user-copy behavior in this API because it
is kernel-internal. Any later syscall must validate and copy user buffers
through the VM user-copy helpers. Callers must not invoke generation from
interrupt context. Current local interrupt masking is only a single-CPU
serialization mechanism; before SMP, use a real lock/CPU-local design and
define interrupt/reentrancy behavior. Early boot initialization may not
depend on scheduler, LAPIC timer, network service, or dynamic allocation.

## State protection and failure handling

State disclosure permits prediction from the disclosed state until a
successful reseed with fresh, approved input. State evolution can provide
backtracking resistance only if the selected construction's update/rekey
procedure is implemented correctly; it cannot erase information already
copied by an attacker. State overwrite or an invariant failure must move to
`FAILED`, not continue with corrupted state. Source failure before readiness
leaves the generator disabled. Failure of a required source after readiness
forces `RESEED_REQUIRED`; optional supplemental provider failure is recorded,
but never silently replaced with an unapproved source.

Snapshot rollback or VM cloning can reproduce generator state and outputs.
Mitigation needs a trusted post-resume/clone signal plus fresh approved
reseed, such as a validated virtual-machine generation identifier mechanism
or a deployment anti-rollback guarantee. No such hook was found in the
current kernel. A malicious hypervisor able to inspect guest memory is
outside the protection this guest can provide.

Seed and state must never be logged, exposed through userspace, or included
in crash diagnostics. Clear temporary buffers and old state after reseed and
on failure. A plain `memset()` may be optimized away; use a reviewed
non-elidable wipe primitive and inspect optimized output. Even volatile wipes
cannot erase register copies, compiler spills, caches, snapshots, or prior
memory disclosures. Review stack use, interrupt state, and core-dump/debug
paths. Fixed static state avoids early allocation but creates a high-value
kernel secret.

Concurrency, interrupted generation and reentrancy must be serialized as one
state transition. The current single-CPU IF masking pattern is not an SMP
solution. Request/byte counters and reseed deadlines must use checked
arithmetic. Failure leaves caller output unchanged and the generator
unavailable; no deterministic or partially generated output is returned.

## Test and verification plan

Implementation must add:

1. **Algorithm tests:** published SP 800-90A known-answer tests; independently
   generated deterministic vectors; hash/HMAC vectors; empty, boundary and
   maximum requests; repeated calls; reseed and state-update tests; counter
   exhaustion/rollover; output accounting. A primitive KAT alone is not a
   DRBG test.
2. **Source/policy tests:** no providers; each provider independently; several
   providers; absent/partial/malformed source results; repeated identical
   input; provider failure before and after instantiate; trust-policy reject;
   successful/failed reseed; provenance and no-fallback assertions.
3. **Failure/security tests:** no output before `READY`; output unchanged on
   all errors; no partial output; request/accounting overflow; injected state
   corruption; sticky failure; no seed/state logging; concurrency/reentrancy
   tests for the implemented locking model.
4. **Build/integration tests:** normal production build without test hooks;
   QEMU with VirtIO RNG present/absent; feature profiles with and without
   RDRAND; RDSEED profiles only after an implementation exists; boot when all
   optional providers are unavailable; production symbol/flag audit.
5. **Sanitizers and review:** host ASan/UBSan where supported, static analysis,
   independent code review, assembly inspection for wipe behavior, and
   independent test-vector verification.

These establish behavior of code under tested inputs. They do not establish
entropy quality, source independence, resistance to a malicious host, formal
verification, certification, or physical hardware quality. QEMU data-path
tests prove only virtual device protocol behavior under that configuration.

## Implementation prerequisites and phases

1. **Trust-policy approval:** define supported deployment profiles and name
   the authority trusted to supply seed material; decide whether a source is
   sufficient or whether independent sources are required. Until this is
   approved, remain `DISABLED`.
2. **Source assurance:** obtain source-specific evidence and specify failure
   and health handling. Do not turn current candidate status into entropy
   credit by renaming it.
3. **DRBG implementation:** implement or import HMAC_DRBG/SHA-256 to the
   applicable standards, with reviewed state/update/wipe behavior and KATs.
4. **Lifecycle/API:** add bounded state machine, request/reseed limits,
   failure atomicity, single-CPU serialization, and later an SMP design before
   SMP support.
5. **Audit and integration:** run host, sanitizer, QEMU, production symbol and
   boot tests; obtain independent cryptographic review. Keep consumers
   disabled until review accepts the seed policy and implementation.
6. **Separate TCP ISN phase:** only after CSPRNG readiness is justified, define
   a keyed tuple/time construction consistent with RFC 9293 §3.4.1 and RFC
   6528, initialize its secret from the CSPRNG, test tuple/time behavior and
   wraparound, and review it separately. This still does not authorize TCP
   dispatch, TX, timers, or syscalls.

Production TCP ISN generation may be considered only after all of the above
and a separate production TCP integration gate. `tcp_core_live_isn_available()`
must remain false until that later approval. No current candidate source
alone satisfies this condition.

## References

- [NIST SP 800-90A Rev. 1: Recommendation for Random Number Generation Using Deterministic Random Bit Generators](https://csrc.nist.gov/pubs/sp/800/90/a/r1/final) — normative DRBG mechanisms and requirements.
- [NIST SP 800-90B: Recommendation for the Entropy Sources Used for Random Bit Generation](https://csrc.nist.gov/pubs/sp/800/90/b/final) — entropy-source design and validation.
- [NIST SP 800-90C: Recommendation for Random Bit Generator (RBG) Constructions](https://csrc.nist.gov/pubs/sp/800/90/c/final) — constructions combining entropy sources and DRBGs.
- [RFC 8439](https://www.rfc-editor.org/rfc/rfc8439) — ChaCha20 and Poly1305 specification; it does not define a DRBG.
- [Intel Digital Random Number Generator Software Implementation Guide](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-digital-random-number-generator-drng-software-implementation-guide.html) — vendor description of RDRAND/RDSEED interfaces; not independent proof of this platform's trustworthiness.
- [Intel 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — architectural instruction and feature semantics.
- [RFC 9293 §3.4.1](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.4.1) and [RFC 6528](https://www.rfc-editor.org/rfc/rfc6528.html) — TCP ISN security guidance.

## Phase 15.6F availability policy update

The existing source manager distinguishes `UNSUPPORTED`, `DEVICE_ABSENT`,
`SUPPORTED` (bounded acquisition is permitted), `CANDIDATE_AVAILABLE`,
`TEMPORARILY_FAILED`, `EXHAUSTED`, and `PERMANENTLY_DISABLED`. These describe
provider lifecycle only; they do not contribute entropy credit and cannot
transition this design to `READY`.

Reads remain explicit and source-specific, capped at 256 bytes. A failed
read leaves caller output unchanged. Temporary failures stop further reads
until the caller explicitly invokes `kernel_entropy_retry_source()`. This
operation performs only the provider-specific bounded retry/re-arm and never
selects another provider. There is no background scheduler, timer, wait, or
pending request queue. Repeated retry calls from a tight caller loop are not
rate-limited by time, so callers must use deliberate control flow. No current
production subsystem depends on candidate acquisition.

The raw RDRAND provider no longer latches a bounded CF=0 exhaustion as a
permanent failure. It reports temporary unavailability and can perform one
new ten-attempt probe through an explicit retry operation. Unsupported CPUID
remains distinct and non-retryable. RDSEED status after CPUID is `SUPPORTED`,
not candidate-available; a successful CF=1 read establishes a recent
candidate result. VirtIO similarly becomes `SUPPORTED` after successful
driver setup, while BUSY is temporary. Its current timeout/malformed paths
disable the driver for the remainder of boot, so manager state is permanent
because this driver cannot reinitialize. Limine remains a consumed one-shot
source. All operations retain provenance and do not mix sources.

These changes improve availability reporting only. The current host
measurements (unknown/ambiguous execution environment, not attested physical
hardware) remain unchanged. Candidate source availability, including a
successful hardware instruction, is not source approval. The manager remains
UP-only, is not interrupt-safe, and is not an SMP synchronization design.
Cryptographic readiness and live TCP ISN availability remain false.

### Phase 15.6G — TPM candidate assessment

The development host has a Linux-visible TPM 2.0 (`tpm_crb_acpi`, ACPI CRB),
but `/dev/tpm0` and `/dev/tpmrm0` are inaccessible to the current unprivileged
user. No capabilities or `TPM2_GetRandom` output were obtained. Intel PTT is
plausible, but the exact TPM type is unverified and the host environment is
not attested physical. TCG's TPM RNG architecture includes an internal
entropy collector and DRBG; GetRandom's returned byte count is
implementation-dependent and must be checked. This interface response alone
does not establish the underlying source, source independence, or the TPM
implementation's trustworthiness. TPM remains an unapproved candidate and is
not connected to a CSPRNG. See
`docs/security/tpm2-feasibility-assessment.md`. No readiness transition is
authorized by TPM presence; both the cryptographic-ready and production ISN
gates remain false.

Phase 15.6H documents a possible CRB transport and `TPM2_GetRandom` provider
in [`tpm2-crb-driver-design.md`](tpm2-crb-driver-design.md). This is a design
only: the platform method and TPM capabilities remain unknown, no command was
tested, TPM remains unapproved, and readiness remains false.
