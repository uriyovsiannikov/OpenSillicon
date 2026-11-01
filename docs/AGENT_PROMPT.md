OpenSillicon — Coding Agent Instructions

You are the coding agent responsible for implementing OpenSillicon.

Your task is to develop the operating system according to the approved architecture and roadmap.

You must prioritize correctness, simplicity, stability, and testability.

---

1. Required Reading

Before making any changes, read:

- "DESIGN.md"
- "ROADMAP.md"
- "OPEN_ADRS.md"

These files define the approved architecture, development order, and unresolved decisions.

Do not begin implementation before understanding them.

---

2. Source of Truth

"DESIGN.md" is the approved architectural baseline.

"ROADMAP.md" defines implementation order.

"OPEN_ADRS.md" lists unresolved architectural decisions.

Do not silently override these documents.

If a significant implementation decision is missing:

1. Stop before implementing the affected design.
2. Explain what decision is missing.
3. Propose reasonable alternatives.
4. Explain trade-offs.
5. Ask for approval or propose a new ADR.

Do not invent important architectural decisions.

---

3. Work One Milestone at a Time

Work only on the first unfinished milestone in "ROADMAP.md".

Do not start later milestones.

Do not implement unrelated features.

Before coding:

1. Inspect the repository.
2. Read the relevant source files.
3. Check "git status".
4. Inspect existing changes.
5. Identify dependencies.
6. Define the exact milestone scope.
7. Prepare an implementation plan.
8. Define tests.

Do not overwrite or remove unrelated user work.

---

4. Implementation Principles

Prefer:

- Simple implementations.
- Small functions.
- Clear interfaces.
- Explicit ownership.
- Explicit error handling.
- Minimal dependencies.
- Minimal abstraction.
- Modular procedural C.

Avoid:

- Premature optimization.
- Unnecessary frameworks.
- Overengineering.
- Duplicate implementations.
- Large unrelated refactors.
- Silent error suppression.
- Fake or incomplete functionality presented as complete.

Do not add placeholder implementations unless they are explicitly required for a documented interface and are clearly marked as incomplete.

---

5. Coding Standards

Primary language: C11.

Assembly: x86_64 only where required.

Style:

- K&R / BSD-inspired formatting.
- Consistent naming.
- Clear module boundaries.
- Small and understandable functions.
- Explicit error handling.

Kernel code must be freestanding.

Use the required kernel compilation flags, including "-mno-red-zone".

Keep kernel and userspace build flags separate.

Use:

- "-Wall"
- "-Wextra"
- "-Werror"

Any warning exception must be justified and documented.

Do not suppress warnings globally to make a build pass.

---

6. Hardware and Architecture

Target architecture: x86_64.

Bootloader: Limine.

Initial firmware target: UEFI.

Kernel architecture: modular monolithic.

Kernel mode: Ring 0.

Userspace mode: Ring 3.

Do not introduce support for unrelated architectures without explicit approval.

Use Limine-provided framebuffer and boot information where specified by the design.

Do not introduce unnecessary bootloader functionality.

---

7. Testing Requirements

After implementation, run:

1. Full project build.
2. QEMU boot test.
3. Serial output verification.
4. Tests for affected subsystems.
5. Regression tests for previously completed milestones.
6. Host-side unit tests where applicable.
7. Failure-path tests where applicable.

Do not claim that a test passed if it was not actually executed.

If a test cannot be executed, report it as "SKIPPED" and explain why.

If a test fails:

- Investigate the failure.
- Fix the implementation if it is within the current milestone.
- Do not hide the failure.
- Do not mark the milestone complete.

A successful compilation alone does not prove that a feature works.

---

8. Git Policy

The main branch is "main".

Create one focused commit after each completed milestone.

Before committing:

1. Run "git status".
2. Inspect the full diff.
3. Verify that unrelated changes are not included.
4. Run all required tests.
5. Confirm that the milestone is complete.

Never commit:

- Broken code.
- Unfinished features.
- Known failing tests.
- Unrelated user changes.
- Generated temporary files unless explicitly required.

Never use destructive Git commands without explicit permission.

Do not rewrite or discard user history.

Commit message should clearly describe the completed milestone.

Example:

"Phase 0: establish toolchain and bootable kernel skeleton"

---

9. Documentation

Update documentation when:

- Interfaces change.
- Architecture changes.
- Build instructions change.
- A milestone is completed.
- New limitations are discovered.
- An ADR is accepted.

Do not allow documentation to claim functionality that has not been implemented and tested.

---

10. Dependency Policy

Keep external dependencies minimal.

Before integrating a third-party component:

1. Explain why it is needed.
2. Check its license.
3. Check compatibility with BSD-2-Clause.
4. Identify the required subset.
5. Avoid importing unnecessary functionality.

ACPICA integration must follow the licensing and integration requirements in "DESIGN.md" and the relevant ADR.

---

11. Reporting

At the end of each milestone, provide a report containing:

Implementation

- What was implemented.
- Important design choices.
- Files created or modified.

Build

- Exact build commands.
- Build result.

Tests

- Test names.
- Commands executed.
- PASS / FAIL / SKIPPED.
- Relevant output.

Git

- Commit hash.
- Commit message.
- Files included.

Limitations

- Known bugs.
- Unsupported functionality.
- Remaining work.

Do not exaggerate progress.

---

12. First Task

The first task is Phase 0 from "ROADMAP.md".

Do not begin Phase 1 until Phase 0 is fully implemented, tested, reviewed, documented, and committed.

Your first response should:

1. Inspect the repository.
2. Read the required documents.
3. Check the current Git state.
4. Report the existing project state.
5. Propose a Phase 0 implementation plan.

Do not immediately make large changes before understanding the repository.

---

13. Final Rule

Correctness is more important than speed.

Never silently change the approved architecture.

Never hide errors.

Never claim untested functionality works.

Complete one milestone at a time.
