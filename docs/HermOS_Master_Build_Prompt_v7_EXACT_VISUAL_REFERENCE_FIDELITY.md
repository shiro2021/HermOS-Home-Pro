# HermOS Master Build Prompt v5 — Bare-Metal Operating System

> **Primary deliverable: a real, installable HermOS operating system for bare-metal
> x86-64 hardware. The installer is a component of the OS delivery, not the
> mission by itself. QEMU is a development/test environment only, never the final
> deployment target. Hermes Core Director is mandatory and must be started by
> the installed system.**

# HERMOS --- MASTER BUILD PROMPT (REFERENCE-PACK EDITION)

## A More Human Computer

**Mission:** Build a real, reproducible, boot-tested HermOS installer
ISO using Genode + NOVA, with QEMU as the initial target. This is an
engineering task, not a mockup task.

**Required final result:** An ISO generated through Genode's actual
build/image tooling and boot-tested in QEMU. Also provide reproducible
source, build instructions, exact test commands, and an honest
validation report. An ISO file existing is not proof that it boots.

------------------------------------------------------------------------

# CRITICAL OVERRIDE --- PREVIOUS FAKE ISO INCIDENT

**This section overrides any conflicting or ambiguous instruction
elsewhere in this document.** The previous attempt produced a
fake/non-working ISO. Treat that as a failed deliverable, not as a
starting point to defend or cosmetically repair. Do not repeat it.

## Non-negotiable ISO authenticity rules

1.  **Do not create, rename, copy, patch, or package a file and call it
    a HermOS installer ISO unless it is the actual output of the
    verified Genode image-generation workflow.** A file extension,
    ISO9660 directory listing, boot catalog, GRUB files, valid checksum,
    or successful archive inspection alone does not prove it is a real,
    working Genode image.
2.  **Never handcraft an ISO as a substitute for a Genode build.** No
    manually assembled ISO filesystem, fabricated boot catalog, dummy
    kernel, placeholder boot modules, copied unrelated ISO, patched boot
    sectors, or shell script that merely emits an `.iso` file. Do not
    use `mkisofs`, `genisoimage`, `xorriso`, `dd`, or similar tools to
    manufacture the final deliverable outside Genode's verified image
    flow. Such tools may be used only if the actual Genode source/run
    script explicitly invokes them as a documented part of its
    legitimate image-generation workflow; verify that path first.
3.  **Do not start by trying to make an ISO.** First inspect the
    repository, exact Genode revision, relevant official run scripts,
    build configuration, boot-image scripts, required ports (including
    boot-loader dependencies), and available toolchain. Record exact
    source paths and commands. Confirm that the selected run script
    genuinely builds the intended components and invokes Genode's own
    boot-image/image-generation mechanism.
4.  **Prove provenance.** Record the exact Genode Git revision, HermOS
    commit, build directory/configuration, run script, build command,
    ISO output path, file size, SHA-256, and build log. Preserve the
    complete unedited log. The output must be traceable to the
    successful build command and its generated artifacts. A checksum
    proves file identity, not bootability.
5.  **Run the actual image.** Boot the generated ISO in QEMU using the
    documented target configuration. Capture the exact command and
    serial/console logs. Confirm that the expected Genode/NOVA boot
    sequence reaches the intended HermOS installer, not merely that QEMU
    opens or a bootloader menu appears. If there is no graphical output,
    inspect serial output and determine whether the GUI was actually
    started.
6.  **Test installer behavior.** Navigate through the implemented
    installer screens. Confirm that hardware/disk information is real
    and not hard-coded, the QEMU target disk is identified, the
    plan/review/confirmation stages work, and destructive writes are
    limited to the selected disposable QEMU disk. Then install, shut
    down, detach the ISO, boot from the installed virtual disk, and
    verify the installed system starts if installation is claimed as
    complete.
7.  **No evidence, no claim.** If a build or test was not executed,
    label it `NOT RUN` or `NOT TESTED`. If it fails, label it `FAIL` and
    include the relevant error. Never infer success from source code,
    intended commands, a generated filename, or another model's
    statement. Do not fabricate logs, screenshots, checksums, output, or
    test results.
8.  **Do not deliver a fake ISO while hiding the failure.** If the
    genuine build path is blocked, do not create a substitute ISO.
    Deliver the source changes, build instructions, exact blocker/error,
    logs, and next diagnostic step. State plainly that no valid ISO was
    produced. This is preferable to another fake deliverable.
9.  **No physical-disk writes.** All installation testing must use a
    newly created, dedicated QEMU virtual disk. Never expose, mount, or
    write to the host's physical laptop disk.
10. **Independent verification.** Have a second model or reviewer
    inspect the run script and build provenance, then review the logs
    and QEMU test evidence. Reviewer agreement does not replace the
    actual test.

## Mandatory release gate

Do not label the work `ISO COMPLETE` unless all of the following have
evidence: - \[ \] Exact Genode source revision and official
image-generation path recorded. - \[ \] Unmodified Genode/NOVA baseline
successfully built and boot-tested in QEMU. - \[ \] HermOS components
compiled from the current repository. - \[ \] ISO generated by the
verified Genode build/image workflow, with full log and SHA-256. - \[ \]
That exact ISO booted in QEMU and reached the HermOS installer. - \[ \]
Installer interactions tested; all incomplete or simulated controls
explicitly disclosed. - \[ \] If installation is claimed complete:
installation to a dedicated virtual disk succeeded, ISO was detached,
and the installed disk booted successfully.

Use the exact status vocabulary: `PASS`, `FAIL`, `NOT RUN`,
`NOT TESTED`, or `PARTIAL`. Do not use a single green "success" status
to blur different stages. The final report must link each `PASS` to
evidence and each failure to the exact blocker.

## 1. Mandatory source review --- do this before implementation

This prompt is intended to be used together with the three PDFs included
in the accompanying reference pack. The exact files are:

1.  `genode-foundations-26-05.pdf`
2.  `genode-applications-26-04.pdf`
3.  `genode-platforms-26-05.pdf`

**All three PDFs are required project inputs, not optional background
reading.** First verify that each file is present, readable, and not
truncated. Read all three before choosing APIs, designing components,
creating run scripts, or writing implementation code. If the AI
environment cannot access an included PDF, stop and clearly request the
missing file; do not pretend to have read it.

Create a concise source-review index that records the relevant
chapters/sections and page numbers for the architecture and build
decisions you intend to use. Preserve the books' terminology and
distinguish:

-   **Documented by the PDFs** --- cite the PDF filename and
    page/section.
-   **Verified in the actual Genode 26.08 source tree or official
    documentation** --- cite the exact path or documentation reference.
-   **Engineering inference / proposed HermOS design** --- label it
    explicitly as a proposal, not as a Genode feature.

Do not silently fill gaps, reconcile contradictions, or replace the
PDFs' framing with generic operating-system assumptions. If sources
conflict or do not answer a question, state the uncertainty and
investigate the real source tree before deciding. The PDFs are technical
references, not a substitute for verifying the exact 26.08 code and run
scripts.

## 1.1 Model and tool discipline

Treat this as an engineering repository task, not a free-form
code-generation exercise. Use whichever available coding model is
strongest for the current task, but follow this workflow regardless of
model brand:

1.  **Inspect first:** inventory the repository, Git state, build
    configuration, installed tools, and relevant source files before
    editing.
2.  **Plan narrowly:** propose one small, testable milestone at a time,
    listing files to change and acceptance tests.
3.  **Implement in a branch or clean Git checkpoint:** do not overwrite
    unrelated user work. Keep changes reviewable.
4.  **Verify against reality:** run the actual commands and inspect
    logs. Do not claim a compile, ISO generation, boot, or installation
    test passed unless that exact step was run successfully.
5.  **Independent review:** where multiple models are available, ask a
    second model to review the diff and evidence without asking it to
    rewrite everything. Resolve disagreements by source inspection and
    tests, not model votes.
6.  **Report honestly:** list changed files, commands executed,
    pass/fail results, known limitations, and the next smallest step. If
    a command was not run, say so.

Never invent Genode APIs, package names, run-script syntax, boot
artifacts, or successful test results. If the environment cannot perform
a required operation, explain the blocker and provide the exact
reproducible command for the user to run.

Required sequence: 1. Inventory and read all three PDFs. 2. Inspect the
actual Genode 26.08 source tree, build tooling, and existing
graphical/QEMU scenarios. 3. Write a source-grounded implementation plan
and record locked decisions in `docs/DECISIONS.md`. 4. Build and
boot-test an unmodified Genode/NOVA/QEMU scenario. 5. Add HermOS
incrementally. 6. Build the ISO using Genode's actual
build/image-generation flow. 7. Boot-test the ISO in QEMU and report
evidence.

Do not guess source paths, APIs, package names, run-script syntax,
toolchain versions, or build flags. Verify them against the actual
checked-out source.

## 2. Identity and philosophy

**HermOS --- A More Human Computer**

HermOS is a human-oriented operating-system architecture designed to
reduce the complexity humans must manage. It is not fundamentally an AI
OS or one giant AI process.

The human expresses intent. The system selects the simplest reliable
mechanism capable of accomplishing it: deterministic procedures,
scheduled tasks, small bots, persistent or temporary agents, specialized
agents, AI models, or ordinary OS services.

Use AI for ambiguity, planning, novel reasoning, and creative work.
Convert repeated reasoning into deterministic procedures when safe and
useful, so routine execution does not require repeated AI calls.

**Foundational principle:** \> Everything the user can do manually,
Hermes should be able to do. Everything Hermes can do autonomously must
be isolated, observable, reversible, and permission-controlled.

The human remains the authority for meaningful decisions. Planning must
not silently become destructive execution.

## 3. Architecture constraints

Target: - **Genode 26.08**, verified against the actual source tree -
**NOVA**, x86_64 - **Ubuntu 24.04 LTS x86_64** development host -
**QEMU** for initial boot and installation tests

Genode provides the component, capability, isolation, resource, and
composition foundation. HermOS provides the human-computer semantic
layer.

Do not make Sculpt OS the architectural foundation. Proven generic
boot/runtime mechanisms may be studied or reused if appropriate, but
Sculpt-specific policy and UX must not become HermOS policy. Do not
substitute a Linux distro installer or static visual mockup for a
Genode-based system. Keep the OS agent-neutral at the extensibility layer, but **Hermes is not
optional and is not merely an example application**. Hermes is the mandatory
central director of the first complete HermOS system. Other agents, bots,
procedures, models, and services may be replaceable or independently
developed; the director role and its core contracts are part of HermOS.

Preserve and use real Genode concepts where appropriate: capabilities,
components/protection domains, component hierarchy, sessions,
parent/routing policy, quotas, dataspaces, region maps, RPC, signals,
shared memory, ROM/configuration, and controlled lifecycle. Do not
invent Genode APIs.

Conceptual architecture:

Human intention
  → HermOS semantic/UI layer
  → **Hermes Core Director (mandatory, boot-integrated)**
  → planning / policy / memory / tool broker / worker lifecycle / recovery
  → agents, deterministic bots, and procedures
  → scoped capabilities and services
  → Genode component system
  → NOVA
  → hardware

Terminology is strict: Genode's `core` is a Genode foundational component;
**Hermes Core Director** is the central coordinating service of HermOS. Do
not rename one to the other, merge their responsibilities, or claim Hermes
replaces Genode `core`, `init`, or NOVA.

## 3.1 NON-NEGOTIABLE: Hermes Core Director is part of the installed OS

This requirement overrides any wording elsewhere that might imply Hermes
can be postponed until after the installer, omitted from the boot image,
or treated as a desktop application.

### Required system behavior

1. **Build target:** the first integrated HermOS build must include a real
   Hermes Core Director component (or a clearly documented, compilable
   staged implementation of it). A logo, UI panel, placeholder process,
   mock API, hard-coded status, or future roadmap entry does not satisfy
   this requirement.
2. **Boot integration:** the verified Genode startup composition must
   launch Hermes automatically as part of normal HermOS boot. The exact
   `init` configuration, component name, binary, dependencies, routes,
   and run-script integration must be confirmed against actual Genode
   26.08 source and official examples—not guessed.
3. **Director contract:** document and implement real interfaces for:
   - receiving user intent or a request from the HermOS semantic/UI layer;
   - maintaining explicit task state and producing an inspectable plan;
   - selecting deterministic procedures, ordinary services, specialist
     agents, or AI reasoning where available;
   - brokering tool/service access through explicit, scoped capabilities;
   - creating/configuring workers, granting least authority, observing
     progress, collecting results, revoking capabilities, and destroying
     temporary workers;
   - exposing truthful status/events to the UI, including active,
     inactive/available, failed, and awaiting-approval states;
   - handling worker/component failure and reporting recovery status.
4. **No fake intelligence:** if model inference, persistent memory, dynamic
   worker creation, recovery, or any director function is not implemented,
   mark it `NOT IMPLEMENTED` and define the next milestone honestly. Do
   not substitute hard-coded demo responses and call them functioning
   Hermes. A small but real first implementation is acceptable.
5. **Authority boundary:** Hermes is a coordinator, not an unrestricted
   superuser. Genode's component/capability architecture and parent/init
   composition remain the security foundation. Give Hermes only the
   capabilities needed for implemented functions. Destructive operations,
   installation writes, updates, and other meaningful changes must pass
   through separate, narrowly authorized components and explicit
   policy/approval checks. Hermes must not have direct access to the host
   physical disk.
6. **Startup and failure policy:** document what happens if Hermes fails
   to start, crashes, or becomes unresponsive. The system must not falsely
   show itself as fully operational. At minimum, expose a clear
   degraded/recovery state and preserve a safe way to inspect logs or
   restart the director, subject to the initial runtime's actual support.
7. **UI integration:** the desktop intent entry point and right-side
   Agents & Workers panel must query Hermes' real state/interface. Active
   workers appear in Solar Orange; inactive/available workers in carbon
   gray. Selecting a worker opens details showing its real reported task,
   state, capability summary, and recent events. If these interfaces are
   not implemented, mark the UI elements nonfunctional; never fabricate
   live status.
8. **Installation:** the installed system's boot configuration must start
   Hermes too—not only the live installer. The installation plan must
   list the director binary/configuration and required supporting
   components included in the installed generation.
9. **Observable proof:** include a boot/integration test proving Hermes
   was built, included in the boot image, started by the startup
   composition, and reached a defined ready/healthy state. Capture exact
   source paths, build output, startup configuration, and runtime logs.
   A source file existing is not proof that it is running.
10. **Release gate:** do not call a build a `HermOS system milestone` if
    Hermes is omitted. Report separately `HERMES BUILT`, `HERMES INCLUDED`,
    `HERMES STARTED`, and `HERMES READY`, each as `PASS`, `FAIL`,
    `NOT RUN`, `NOT TESTED`, or `PARTIAL`, with evidence. An installer-only
    milestone may be reported as `INSTALLER PROTOTYPE`, but must not be
    presented as a complete HermOS system.

### Required first director milestone

Before expanding the worker ecosystem, implement the smallest real director
that starts under Genode, publishes a defined status/health interface,
accepts a simple structured request from a test client, returns a
structured plan/result without pretending to execute unsupported actions,
and emits observable logs. Build and integration-test it using verified
Genode APIs. Then connect the UI to that same real interface. Extend it in
small increments: procedure dispatch, permission/capability brokering,
worker lifecycle, persistence/memory, AI backend integration, and recovery.

Choose the IPC/API and component decomposition only after inspecting the
actual Genode 26.08 source and relevant examples. Record the contract in
`docs/HERMES_DIRECTOR.md` and `docs/DECISIONS.md` before implementation.

## 3.3 NON-NEGOTIABLE: Operational AI-worker, bot, and procedure fabric

HermOS is an agent-oriented operating system in operation, not merely a
desktop that displays agent names. Hermes Core Director must coordinate
real task execution through a defined worker/procedure fabric. This section
specifies the intended operating model; implement it incrementally and label
unimplemented capabilities honestly.

### A. Select the simplest reliable mechanism

For every user request, Hermes must decide which execution path is appropriate:

1. **Ordinary system service** for a well-defined function already provided
   by a trusted component.
2. **Deterministic procedure/bot** for repeatable work with explicit inputs,
   steps, preconditions, and success/failure criteria.
3. **AI reasoning or AI worker** when the task needs interpretation,
   ambiguity resolution, planning, unfamiliar problem-solving, or flexible
   synthesis.
4. **Hybrid plan** when AI is useful to interpret or plan, deterministic
   procedures should execute the repeatable steps, and AI should return
   when observations differ from expectations or the environment changes.

Do not invoke an AI model merely because one is available. Do not use a
probabilistic AI worker where a validated deterministic procedure is simpler
and safer. Conversely, do not pretend a fixed script can resolve ambiguity
that it has not been designed to handle.

Canonical loop:

**AI interprets/plans → deterministic procedure executes → system observes
and verifies → AI is re-engaged only if the outcome, environment, or
uncertainty requires it.**

The plan and the chosen execution mode must be inspectable. Hermes must
record why it selected that mode at a level appropriate for the UI and logs.

### B. Roles and responsibility

- **Human:** states goals, supplies missing intent when needed, approves
  consequential actions, and can inspect, stop, or cancel work.
- **Hermes Core Director:** receives intent; clarifies ambiguity; decomposes
  work; selects procedures, services, and AI workers; tracks dependencies
  and task state; authorizes scoped capability requests; monitors outcomes;
  handles exceptions and recovery; and reports truthful results.
- **AI worker:** performs a bounded reasoning task delegated by Hermes.
  It receives only the context and tools required for that task, returns a
  structured result with uncertainty/errors where possible, and cannot
  grant itself capabilities or silently widen its assignment.
- **Deterministic bot/procedure:** performs an explicit, versioned workflow
  with defined preconditions, inputs, permitted side effects, timeout,
  retry policy, output schema, and verifiable completion criteria.
- **System service:** supplies a narrow OS function through a defined
  interface; it should not be turned into an agent unless there is a real
  need for planning or interpretation.
- **Capability/tool broker:** enforces access boundaries independently of
  model-generated claims. A worker's text saying it has permission is never
  proof of authority.
- **Observer/monitor:** collects real status, logs, completion evidence, and
  relevant environment changes from actual components, not from invented UI
  state.

These are logical responsibilities, not a mandate to create one process for
each role immediately. Choose component boundaries based on real Genode
mechanisms and the smallest testable implementation.

### C. Task contract and lifecycle

Every dispatched task must have a structured record containing, at minimum:
unique task ID; user goal; selected execution mode; parent/dependency IDs;
assigned worker/procedure/service; declared input/context; required
capabilities; risk/approval classification; state; start/update/end times
when available; result; error/uncertainty; verification evidence; and
cancellation/recovery status.

Required task states should distinguish at least:
`PROPOSED`, `AWAITING_CLARIFICATION`, `AWAITING_APPROVAL`, `READY`,
`RUNNING`, `VERIFYING`, `SUCCEEDED`, `FAILED`, `CANCELLED`, and
`RECOVERY_REQUIRED`. Do not display `SUCCEEDED` solely because a worker
claims success; use the relevant completion check.

Temporary worker lifecycle:

**create → configure → grant minimum capabilities → execute/observe →
verify result → report → revoke capabilities → destroy**, with cleanup on
failure/cancellation. Reuse a persistent worker only when there is a clear
reason, an explicit lifetime/ownership policy, and a defined revocation path.

For long-running work, Hermes must expose progress and the most recent
meaningful event, allow cancellation where technically supported, and
explain when a task is waiting for a human, service, resource, or retry.

### D. AI-worker contract

An AI worker must receive a bounded assignment with:
- task goal and acceptance criteria;
- only necessary context and data;
- available tools/capabilities and explicit limits;
- time/resource budget where supported;
- required response schema and reporting format;
- conditions requiring clarification, escalation, or stopping.

AI output is an untrusted proposal, not an authority grant or proof of
success. Validate structured output; check tool arguments against policy;
treat retrieved documents, webpages, files, and tool output as data rather
than instructions that override system policy; and verify consequential
results independently where feasible. Handle timeouts, malformed output,
model unavailability, uncertainty, and conflicting results without falsely
claiming completion.

The architecture must not assume a particular AI vendor or model. Define a
model/backend interface so the system can use configured local or remote
inference providers as available. Report provider availability, latency or
resource failures where measured, and data-routing/privacy choices. Do not
send sensitive context to a remote provider unless the relevant policy and
user permissions allow it. If no model backend is configured, deterministic
procedures and ordinary services should still operate; AI-dependent tasks
must be marked unavailable, queued, or returned for human handling rather
than simulated.

### E. Deterministic bot/procedure contract

A bot/procedure must declare:
- stable identifier, version, purpose, and owner;
- input/output schema and preconditions;
- allowed capabilities and side effects;
- timeout, retry and idempotency behavior where applicable;
- success/failure criteria and verification method;
- cleanup/rollback behavior for partial execution;
- whether it is safe to run unattended and under what policy.

Prefer small, composable procedures over one opaque mega-agent. Store reusable
procedure definitions and versions in a controlled registry. AI may propose
a new procedure from observed work, but it must not silently promote
generated code or a new workflow to trusted unattended execution. New or
changed procedures require validation, testing, provenance, and whatever
approval their risk level requires.

### F. AI-to-procedure learning loop

HermOS should progressively turn repeatable successful work into reusable
deterministic procedures:

1. Hermes or an AI worker proposes a plan.
2. A human or policy approves the plan when its risk requires approval.
3. A procedure is run in a bounded environment and its outcomes are observed.
4. Hermes checks the actual result against acceptance criteria.
5. If the workflow is repeatable, Hermes may propose formalizing it as a
   versioned procedure.
6. The procedure is tested against known inputs, failure cases, permissions,
   and rollback behavior.
7. Only after validation and any required approval can it be registered for
   reuse or unattended execution.
8. If the environment changes or the procedure fails its checks, stop or
   fall back safely and ask AI to reassess; do not repeatedly execute a
   known-broken procedure.

Do not claim autonomous learning or automatic bot generation is implemented
until there is a real, tested implementation. Start with a manually defined
procedure registry and a simple deterministic dispatcher, then add proposal,
validation, and promotion workflows.

### G. Authority, risk, and human control

Classify actions by risk and impact. Reading status or searching user-
authorized data may be low risk; sending messages, sharing data, changing
system configuration, installing software, deleting data, spending money,
or writing to storage may require explicit approval or stronger policy.
Exact classifications must be configurable and visible.

- Apply least privilege to every worker and procedure.
- Separate planning from execution and policy enforcement.
- Validate capabilities in trusted system components, never only in prompts.
- Require explicit approval for consequential/destructive actions according
  to the configured policy.
- Provide stop/cancel and revoke paths where technically possible.
- Make side effects observable and record who/what authorized them.
- Make reversible operations reversible where feasible; never promise
  rollback when the action or hardware cannot support it.
- Do not let Hermes, an AI worker, or a bot bypass the installation
  executor's disk boundary or access the developer host's physical disk.

### H. Operational UI and truthful observability

The Agents & Workers panel must reflect real director/worker/procedure state.
For each active item, show the real task, state, execution mode (AI,
deterministic procedure, service, or hybrid), start/progress information
where available, last event, required approval/wait reason, and failure
details. Selecting it opens a detail panel with task plan, capability
summary, events/log references, verification result, and available controls.

Use the agreed visual convention: active/running items in Solar Orange;
inactive/available items in carbon gray. Color must not be the only state
indicator. Failed, waiting, approval-required, and recovery states need
clear textual labels. Do not invent worker counts, live activity, progress,
or completion events.

### I. Minimum operational acceptance tests

Before claiming the worker fabric is operational, add tests that demonstrate:

1. Hermes receives a structured request and records its task state.
2. A simple deterministic procedure is dispatched, executed, and verified.
3. A task can be marked failed when its acceptance criteria are not met.
4. An AI-dependent task is reported unavailable when no model backend is
   configured; no fake answer is substituted.
5. A hybrid task can use AI to propose a plan and a bounded procedure to
   execute a safe, testable step, with verification afterward.
6. A worker cannot use an undeclared capability or widen its authority.
7. Cancellation/failure triggers cleanup and capability revocation to the
   extent supported by the implemented runtime.
8. The UI shows the same states and events reported by the director.
9. Consequential actions enter the required approval state and cannot be
   executed by bypassing the policy gate.
10. The logs and test outputs prove each result; mocks may be used in unit
    tests but must be labelled and must not count as end-to-end OS tests.

### J. Implementation sequence

Build in increments, each with compiling code and tests:
1. Director task/state model and structured request/response interface.
2. Deterministic procedure registry and dispatcher with one safe test
   procedure.
3. Real task/event reporting to the UI and failure-state handling.
4. Capability/policy broker integration and approval gate.
5. AI backend abstraction with explicit unavailable behavior.
6. Bounded AI worker dispatch and structured-result validation.
7. Hybrid AI-plan → deterministic-execution → verification loop.
8. Cancellation, timeout, cleanup, and recovery behavior.
9. Versioned procedure proposal, test, and approval/promotion workflow.
10. Broader worker ecosystem and persistence, after the earlier stages pass.

At every stage, clearly label each capability `IMPLEMENTED AND TESTED`,
`IMPLEMENTED NOT TESTED`, `PARTIAL`, or `NOT IMPLEMENTED`. Never let a UI
mock or an architectural diagram count as a functioning worker fabric.

## 3.2 NON-NEGOTIABLE: HermOS is a bare-metal operating system, not an installer-only project

This section clarifies the release target and overrides any wording that
could imply QEMU or the installer is the final product.

### Intended final product

Deliver a real HermOS operating system that boots on supported physical
x86-64 hardware, including the user's ACEMAGIC Intel N97 / 16 GB RAM
machine where hardware support permits. It includes the HermOS user
environment, the mandatory Hermes Core Director, required system services,
and an installer that can install the OS to a selected physical target.
The installer must be part of the complete OS delivery, not the only
working artifact.

QEMU is allowed and encouraged for safe development, repeatable tests,
and virtual-disk installation testing. **Passing QEMU tests does not prove
bare-metal compatibility and must never be reported as such.** The final
release requires a separate physical-hardware boot test and installation
test, or an explicit honest statement that those tests remain unperformed.

### Required product milestones

1. **Native Genode/NOVA baseline:** establish the supported boot path and
   hardware constraints from the actual Genode source and official platform
   documentation. Verify which boot mode(s), graphics, input, storage,
   network, audio, and power-management functions are available on the
   target machine. Do not assume support without evidence.
2. **Bootable HermOS live system:** create the actual OS boot image using
   the verified Genode build workflow. It must launch the HermOS system
   composition and Hermes Core Director, not merely show an installer
   screen.
3. **Integrated installer:** the live system offers an installer that
   identifies candidate disks, clearly identifies the selected physical
   disk by model/capacity/device details, presents a complete plan, and
   requires explicit confirmation before destructive writes. Discovery and
   planning are read-only. Only a separate narrowly authorized executor
   may write to the selected target. No automatic disk selection.
4. **Installed system:** install the bootable HermOS system—not only an
   installer shell—to the selected target, including boot files, Genode
   configuration, Hermes Core Director, the HermOS user environment, and
   declared supporting components.
5. **First boot from disk:** remove installation media and boot the
   installed HermOS from the target disk. Verify that Hermes starts and
   reaches its defined healthy state, the UI can communicate with it, and
   basic system functions work.
6. **Physical-hardware validation:** test the live boot and installed boot
   on the intended ACEMAGIC machine. Record exact hardware, firmware boot
   mode, boot method, logs, devices detected, working/broken functions, and
   any limitations. If a test cannot be performed, mark it `NOT TESTED`.
7. **Safe development policy:** until hardware compatibility and recovery
   are verified, use QEMU virtual disks for destructive installer tests.
   Never write to the developer laptop's physical disk as part of an
   automated build or test. Physical installation is a deliberate,
   user-confirmed operation only.
8. **Honest release status:** report separate gates for:
   - source/build reproducibility;
   - boot image generated by official Genode tooling;
   - QEMU live boot;
   - QEMU installation and installed-disk boot;
   - physical live boot;
   - physical installation;
   - physical installed-disk boot;
   - Hermes built/included/started/ready;
   - required hardware functions.
   Never collapse these into a single `works` or `complete` claim.

### Correct definition of done

HermOS is not complete merely because an installer ISO boots. The target
definition of done is: **a genuine HermOS image boots on the target physical
computer; its integrated installer can safely install the OS to the user's
explicitly selected disk; the machine boots the installed OS without the
installer media; Hermes Core Director starts automatically and performs
its implemented functions through real interfaces; and the results are
documented with reproducible build logs and physical-test evidence.**

If the first deliverable must be staged, label it precisely (`BOOTABLE
LIVE SYSTEM`, `INSTALLER PROTOTYPE`, `QEMU-INSTALLED SYSTEM`, or
`BARE-METAL TESTED SYSTEM`). Never describe an earlier stage as the final
operating system.

## 4. First milestone: actual bootable installer ISO

The QEMU VM is disposable. It is acceptable to write to a dedicated QEMU
virtual disk. **Never modify the physical development laptop's disk.**

Target flow: 1. QEMU boots from the generated HermOS ISO. 2. HermOS boot
screen and glyph appear. 3. "HERMOS --- A MORE HUMAN COMPUTER" appears.
4. Installer opens. 5. It detects and displays hardware information that
is genuinely available. 6. It detects and identifies the QEMU virtual
disk. 7. User selects the target. 8. Installer generates a structured
installation plan. 9. User reviews the exact proposed operations. 10.
User explicitly confirms. 11. Executor writes only to the selected QEMU
virtual disk. 12. Installer verifies the result and offers reboot. 13.
VM boots from the installed virtual disk into a minimal HermOS
environment.

If full disk installation is not safely achievable in the first
increment, deliver only the genuinely built and QEMU-boot-tested
installer ISO if it passes the release gate above, and explicitly report
which installation stages remain incomplete. If genuine ISO generation
or boot testing is blocked, do not deliver a substitute ISO; deliver the
source, logs, exact blocker, and next diagnostic step. The end goal
remains installation to a QEMU virtual disk and reboot into the
installed system.

### Planner/executor boundary

Keep separate: - **Installation Planner:** creates a structured
`InstallationPlan`; never writes to storage. - **Review/Confirmation
UI:** displays the plan and obtains explicit approval. - **Installation
Executor:** the only component authorized to modify the target.

The executor must revalidate the target immediately before writing. For
QEMU, attach a dedicated virtual disk and make it the only writable
installation target. Do not expose host physical disks to the guest.

The plan should include applicable target identity/size, storage layout,
system generation/files, boot configuration, initial components,
recovery strategy, verification steps, and every operation that modifies
the target. No silent destructive actions.

## 5. Canonical HermOS visual design --- locked specification

Visual fidelity is an engineering requirement. Do not silently redesign,
substitute, or reinterpret canonical elements. Use shared assets and a
reusable window/widget implementation across every screen.

### 5.1 Window geometry: exactly six sides

The main HermOS window is a **stretched horizontal hexagon with exactly
six sides and six vertices**: 1. long horizontal top edge 2. upper-right
diagonal 3. lower-right diagonal 4. long horizontal bottom edge 5.
lower-left diagonal 6. upper-left diagonal

Exactly six sides---not seven, not eight, not a rounded rectangle, not a
generic clipped rectangle, not a stop-sign shape, and not a regular
hexagon.

The top and bottom are long and straight. The four diagonal sides create
relatively short corner transitions. Corner proportions must be derived
from the window width and height. The corners should be short relative
to the horizontal spans.

For a custom SVG/canvas/renderer, define the geometry once using width
`W`, height `H`, and horizontal corner inset `C`, where `0 < C < W/2`.
Clockwise vertices: - `(C, 0)` - `(W-C, 0)` - `(W, H/2)` - `(W-C, H)` -
`(C, H)` - `(0, H/2)`

This creates exactly six edges. Choose and document a balanced `C` ratio
relative to `W` and `H`, then reuse it consistently. Do not add bevel
segments, extra vertices, or additional clipped corners. The title bar,
content mask, frame, active border, and inactive border must all follow
the same six-sided outline; the title bar must not add corners that make
the window look seven- or eight-sided.

### 5.2 Material and frame

-   Dark graphite/carbon appearance.
-   Carbon-fiber-inspired title bar with subtle texture, not a generic
    flat bar.
-   Restrained metallic detail.
-   Active outline: **Solar Orange**, with subtle soft glow/bloom.
-   Inactive outline and controls: restrained gray.
-   Legible, high-contrast typography.
-   No excessive neon, random decoration, or generic cyberpunk style.

### 5.3 Window controls and behavior

-   Minimal title bar.
-   Minimize, maximize/restore, and close at top-right.
-   Consistent recognizable icons.
-   Dragging the title bar moves the window, if supported by the GUI
    stack.
-   Design for future resize support without changing the geometry
    contract.
-   Active/inactive focus states must be clear.
-   Controls must actually work; if not implemented, disable them
    visibly and document the limitation.

### 5.4 Logo and branding

Canonical HermOS glyph: circular hermetic-style geometric symbol with
circular outer form, central vertical element, and subtle
four-point/star-like geometric structure.

**Do not invent a replacement logo.** If the exact logo file is
supplied, use it unchanged and preserve it as a canonical asset. If it
is not available, do not claim a new approximation is the original. Use
a clearly identified temporary placeholder only if necessary and report
that the canonical asset is still needed.

Brand: **HERMOS** **A MORE HUMAN COMPUTER**

### 5.5 Background and atmosphere

Use the agreed cinematic direction: mountain/lake landscape, sunrise or
dawn, mist, atmospheric blue/gold light, calm negative space, premium
translucent/glass layers. Earth/planetary imagery may be used on boot
screens. Backgrounds should support---not overpower---the UI.

Avoid generic Linux desktop aesthetics, conventional taskbars, cluttered
dashboards, and inconsistent screens.

### 5.6 Shared widgets/window system

Build a reusable window/widget system rather than redrawing every screen
independently. Share: - one six-vertex geometry definition - geometry
tokens for width, height, corner inset, border thickness, title-bar
height - carbon graphite, inactive gray, and Solar Orange tokens - title
bar and top-right control widgets - active/inactive/focus states -
content clipping/masking matching the hexagon - internal padding that
respects angled corners - consistent typography, icons, spacing, and
button states - keyboard focus and accessible control labels - title-bar
dragging where supported - future-compatible resize design

Create `docs/DESIGN.md`. If a canonical reference image is supplied,
store it as an asset and compare implementation to it. A generated
montage is not proof of functional GUI behavior.



## 5.7 NON-NEGOTIABLE: exact fidelity to the supplied HermOS reference pictures

The supplied HermOS pictures are **the visual source of truth**, not loose inspiration. This requirement applies to every window, widget, panel, sidebar, subpanel, dialog, installer screen, and desktop surface. Do not produce a generic design that merely shares the same colors.

### Before implementation: establish the canonical reference set

1. Locate and inspect every supplied/generated HermOS reference picture available in the project and conversation. Do not assume a picture exists in the build workspace merely because it was discussed earlier.
2. Create `docs/design/reference-inventory.md` listing each actual image file, its exact path, what screen/component it defines, and whether it is available to the build. Copy canonical pictures into a version-controlled `assets/design/reference/` directory when licensing and file access permit; preserve originals unchanged.
3. If a required reference image cannot be accessed, **do not invent a replacement and do not claim exact matching**. Record the missing asset and ask for it or continue only with the references actually available.
4. Extract the visual rules from the images into `docs/DESIGN.md`: silhouette, proportions, spacing, placement, material, texture, colors, glow, typography, icon style, layering, transparency, and states. Explicitly distinguish measured facts from estimates where pixels or source dimensions are unavailable.

### Exact matching is required for these elements

- **Windows:** reproduce the six-sided stretched horizontal hexagon shown in the approved pictures, with exactly six vertices and six edges. Match its relative width/height, corner inset, diagonal slopes, frame thickness, title-bar height, padding, and placement as closely as the reference permits. Do not round corners, add bevels, or silently change the silhouette.
- **Title bars and controls:** match the carbon/graphite material and subtle carbon-fiber texture, title placement, spacing, and top-right minimize/maximize/close control arrangement shown in the pictures. Controls must be functional if presented as functional; otherwise visibly disable and document them.
- **Widgets:** implement the same visual language for buttons, text/input fields, search/intent field, cards, status indicators, lists, icons, separators, popovers, and detail panels. Match their shapes, padding, proportions, hierarchy, and active/focus/disabled states to the references. Do not substitute stock toolkit widgets if they visibly contradict the design.
- **Right-side Agents & Workers sidebar:** reproduce the pictured right-edge placement, width, dark carbon surface, typography, row spacing, icons, separators, and slide-in/slide-out behavior. Active/running workers use Solar Orange as shown; inactive/available workers use restrained carbon gray. Selecting an active worker opens the pictured/detail-panel pattern showing what it is actually doing. Selecting an inactive worker opens the appropriate activation/permission flow; never pretend it is running before activation succeeds.
- **Nested subpanels:** match the reference hierarchy, placement, edge treatment, spacing, and transition behavior. Preserve a clear path back to the parent sidebar/window. Do not turn subpanels into unrelated modal dialogs or dashboard cards unless the reference does so.
- **Desktop and atmosphere:** reproduce the agreed calm cinematic mountain/lake dawn, mist, and blue/gold atmosphere where those backgrounds appear in the reference. Keep the UI legible and the composition uncluttered. Do not replace it with a generic Linux desktop, taskbar, or neon/cyberpunk dashboard.
- **Branding:** use the exact approved logo asset when it is available. Do not redraw, simplify, or replace it with a generic ring or another symbol. If the file is unavailable, state that explicitly rather than asserting a substitute is exact.

### Build shared components from the references, not screen-by-screen approximations

Create one canonical implementation for each repeated visual primitive, including at minimum:

- `HermosWindow` / window frame and title bar
- `HermosButton` and control states
- `HermosInput` / intent field
- `HermosSidebar` / Agents & Workers list
- `HermosWorkerRow` with truthful active, inactive, pending, failed, and permission-required states
- `HermosWorkerDetailPanel` / nested subpanel
- shared typography, spacing, geometry, color, material, border, and glow tokens

Use those shared components for the desktop, installer, file views, system overview, worker details, creation flows, and settings. Fixing a canonical component must update every screen that uses it. Do not create independent approximations of the same window or sidebar.

### Mandatory visual verification and release gate

Visual implementation is not accepted merely because it compiles or looks vaguely similar.

1. Render screenshots of each implemented screen and each important interaction state at the same or closest practical viewport dimensions as its reference image.
2. Compare screenshot and reference side by side. Where feasible, create an overlay/difference image to reveal geometry and alignment drift. Keep these comparison artifacts in `artifacts/visual-regression/`.
3. Check at minimum: six-vertex window silhouette; width/height and corner proportions; title-bar and control placement; sidebar edge alignment and width; widget sizing and spacing; orange/gray state distinction; text hierarchy; background composition; logo fidelity; nested-panel behavior.
4. Fix meaningful mismatches before declaring visual completion. Do not hide mismatches by cropping, scaling screenshots deceptively, or comparing unrelated viewport sizes without documenting the difference.
5. Add a checklist to `docs/DESIGN.md` that maps every reference picture to its implementation component and test/screenshot. Mark each as `MATCHED`, `PARTIAL`, `BLOCKED_BY_MISSING_REFERENCE`, or `NOT_IMPLEMENTED`.
6. The release report must state which screens were visually compared, which remain incomplete, and why. **Never claim exact visual fidelity unless the reference assets were available and the comparisons were actually performed.**

The reference pictures define the intended visual design; this prompt's written constraints clarify implementation and consistency. Do not use implementation convenience, toolkit defaults, or personal aesthetic preference as permission to redesign the approved appearance. If a visual ambiguity cannot be resolved from the available pictures, document it and ask rather than silently inventing a new design.

## 6. Installer screens

All screens must use the same canonical window/widget system.

1.  **Boot:** canonical glyph, "HERMOS", "A MORE HUMAN COMPUTER",
    cinematic background.
2.  **Welcome:** "Welcome to HermOS", brief explanation, Continue/Exit.
3.  **Computer:** show actual detected CPU, memory, graphics/display,
    network if available, storage, and boot mode. Label unknown values
    honestly.
4.  **Target:** show the dedicated QEMU virtual disk, detected identity,
    size, and status. If the target cannot be reliably distinguished
    from host storage, stop safely.
5.  **Plan:** display target, storage layout, system files/generation,
    boot setup, initial components, recovery, and verification as
    applicable.
6.  **Review:** state exactly what will change; require explicit
    confirmation before any write.
7.  **Installing:** show real executor progress, not fabricated
    animation or premature success.
8.  **Finalizing:** perform actual verification and show the results.
9.  **Complete:** show "HermOS installation complete" only after
    verification passes; provide Reboot and Explore/Continue actions
    only if implemented.

## 7. Dirigent, agents, bots, and deterministic procedures

The Dirigent conceptually observes, reasons, plans, delegates,
remembers, invokes tools, administers the system, and recovers. Do not
grant it or any AI model unrestricted authority. Authority comes from
scoped capabilities.

Long-term model: AI reasoning → generated deterministic procedure →
routine execution → call AI again when conditions change or the
procedure cannot safely proceed.

Temporary worker lifecycle: create → configure → grant minimal
capabilities → execute/observe → report → revoke → destroy.

Temporary workers should have explicit resource budgets and limited
capabilities. Preserve this direction architecturally, but do not delay
the bootable installer milestone by trying to implement the entire
ecosystem immediately.

## 8. Security, updates, recovery

Long-term design should support least authority, component isolation,
resource budgets, observability, structured audit records, explicit
approval for meaningful/destructive actions, reversibility where
feasible, and controlled lifecycle.

Update concept: candidate → verify provenance/integrity → rollback point
→ stage → test → activate → monitor → keep or rollback.

Do not claim self-updating, rollback, or recovery features are
implemented unless they actually are. Document implemented functionality
separately from future architecture.

## 9. Build workflow

1.  Review all three PDFs.
2.  Verify the actual Genode 26.08 source and host requirements.
3.  Install only required documented dependencies.
4.  Build an unmodified official Genode scenario.
5.  Boot-test that scenario in QEMU.
6.  Inspect actual graphical scenarios and run scripts before
    implementing the GUI.
7.  Add the HermOS repository/component.
8.  Build with Genode's real build/run system.
9.  Generate the ISO through supported Genode tooling.
10. Boot-test the ISO in QEMU.
11. Test installer screens and virtual-disk isolation.
12. If disk installation is implemented, reboot from that disk and
    verify the installed system boots.
13. Record exact commands and results.

Do not handcraft ISO boot sectors to imitate a real Genode build. Do not
assume toolchain versions, component names, or run-script syntax.
Inspect the source.

## 10. Required validation report

Report each item separately:

  -----------------------------------------------------------------------
  Check                   Status                  Evidence
  ----------------------- ----------------------- -----------------------
  All three PDFs reviewed PASS/FAIL               Review notes

  Genode source/version   PASS/FAIL               Exact revision
  verified                                        

  Unmodified Genode build PASS/FAIL               Command and log

  Baseline QEMU boot      PASS/FAIL/NOT TESTED    Command/result

  HermOS component build  PASS/FAIL               Command/log

  Hermes director built   PASS/FAIL/NOT TESTED    Binary/build log

  Hermes included         PASS/FAIL/NOT TESTED    Boot modules/config

  Hermes started          PASS/FAIL/NOT TESTED    Genode startup/runtime log

  Hermes ready            PASS/FAIL/NOT TESTED    Health/request-response test

  Hermes UI integration   PASS/PARTIAL/FAIL       Real interface test

  ISO generated by Genode PASS/FAIL               Command/artifact
  tooling                                         

  ISO booted in QEMU      PASS/FAIL/NOT TESTED    Exact test
                                                  command/result

  Installer UI            PASS/PARTIAL/FAIL       Screens implemented

  Virtual disk detected   PASS/PARTIAL/FAIL       Test evidence

  Planner/executor        PASS/PARTIAL/FAIL       Code/test evidence
  separation                                      

  Virtual disk            PASS/PARTIAL/NOT        Test evidence
  installation            IMPLEMENTED             

  Reboot from installed   PASS/FAIL/NOT TESTED    Test evidence
  disk                                            
  -----------------------------------------------------------------------

Never say "bootable" merely because an ISO exists or its catalog is
structurally valid. ISO generation and successful QEMU boot are separate
claims. If a step cannot be tested, mark it **NOT TESTED**. Do not
invent logs, screenshots, checksums, or results.

## 11. Repository and deliverables

Suggested documentation: - `docs/ARCHITECTURE.md` - `docs/SECURITY.md` -
`docs/DESIGN.md` - `docs/INSTALLER.md` - `docs/DEVELOPMENT.md` -
`docs/ROADMAP.md` - `docs/DECISIONS.md`

Keep canonical visual assets in the repository, including the actual
logo if supplied, window geometry source, backgrounds, and reference
images.

Final deliverables: 1. **HermOS installer ISO**, generated using
Genode's actual build/image flow and boot-tested in QEMU. 2. Source
repository/archive sufficient to reproduce the build. 3. Exact build and
QEMU test commands. 4. Validation report with PASS/FAIL/NOT TESTED. 5.
Known limitations and next concrete step.

If the environment cannot build or test the ISO, deliver the source and
state the exact blocker---but do not represent the task as complete. The
intended end result remains a genuinely boot-tested ISO.

## 12. Work discipline and final task

-   Work in small, verifiable stages.
-   Record important decisions in `docs/DECISIONS.md`; mark fixed
    decisions `LOCKED` and do not silently change them.
-   Prefer small native Genode components and minimal dependencies.
-   Componentize when isolation/security/resource-management benefits
    justify IPC; avoid needless component proliferation.
-   Do not claim source compiles unless it was compiled.
-   Do not claim a UI works if it is only a mockup.
-   Do not claim an ISO boots unless it was boot-tested.
-   Do not ask the user to repeat documented decisions.
-   If a required artifact is missing, identify it explicitly instead of
    inventing it.
-   If blocked, report the exact blocker, attempted command, relevant
    error, and next evidence-based action.

**Final instruction:** Read all three attached Genode PDFs first.
Inspect the real Genode 26.08 source and build environment. Establish a
tested Genode/NOVA/QEMU baseline. Implement and boot-integrate the
**mandatory Hermes Core Director** in Section 3.1; prove it is built,
included, started, and ready. Integrate the HermOS intent UI and Agents &
Workers panel with real director interfaces. Implement the installer using
the exact six-sided stretched-hexagon window system, carbon title bar,
Solar Orange active outline/glow, canonical logo when its actual asset
is available, and agreed cinematic visual language. Generate a real ISO
using Genode tooling and boot-test it in QEMU. Aim for installation to a
dedicated QEMU virtual disk and reboot into a minimal installed HermOS
that also starts Hermes automatically. Report exactly what passed,
failed, or was not tested.

**The goal is a real, reproducible, boot-tested HermOS ISO---not a
concept image, fake ISO, or unverified source package. The previous fake
ISO incident is a known failure that must not be repeated. If the real
build and boot tests cannot be completed, do not create or present an
ISO.**
