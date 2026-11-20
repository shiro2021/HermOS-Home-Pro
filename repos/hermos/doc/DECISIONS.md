# HermOS decisions

Decisions below are based on the V7 prompt, the supplied postulates, and the inspected Genode 26.08 tree. `LOCKED` means the design constraint is fixed; implementation details may change only with an explicit recorded rationale.

| ID | Status | Decision |
| --- | --- | --- |
| D-001 | LOCKED | Target Genode 26.08, NOVA, x86-64. HermOS does not replace Genode `core`, `init`, or NOVA. |
| D-002 | LOCKED | Never expose or write the laptop's internal disk in automated tests. Destructive installation tests, if implemented, use only a newly created QEMU virtual disk. |
| D-003 | LOCKED | Hermes Core Director is required in the HermOS live and installed system composition. A desktop/installer-only image is only a prototype and must be named accordingly. |
| D-004 | LOCKED | Intent planning and execution are separate. Unsupported actions return a truthful unsupported/unavailable state; the director does not invent results or hold raw disk access. |
| D-005 | LOCKED | Genode components, sessions, capabilities, init composition, and resource quotas are the enforcement substrate. Any HermOS authority is explicitly routed and scoped. |
| D-006 | LOCKED | The approved stretched-horizontal hexagon has exactly six vertices `(C,0)`, `(W-C,0)`, `(W,H/2)`, `(W-C,H)`, `(C,H)`, `(0,H/2)`. One shared geometry definition must be used by the frame, title bar, border, and content mask. |
| D-007 | LOCKED | Use supplied reference pictures and exact emblem asset unchanged as canonical sources; do not substitute a generated approximation. Record derived crops separately from originals. |
| D-008 | LOCKED | Status displayed by the UI must originate in director/runtime state. No fabricated active workers, progress, hardware values, installation success, or recovery claims. |
| D-009 | LOCKED | A custom ISO extension must be identified as local HermOS code, tied to a precise source diff and run log. Only the stock Genode `image/iso` path is upstream; the 26.08 stock module is BIOS El Torito only. |
| D-010 | LOCKED | QEMU boot success does not establish physical compatibility. Bare-metal live boot and install remain separate, explicit tests. |
| D-011 | IMPLEMENTED | Hermes uses a dedicated Genode session service with bounded shared-dataspace ABI v1, health RPC, submit RPC, and a real integration client. The director returns `AWAITING_CLARIFICATION`/`NOT_IMPLEMENTED` for recognized intents and rejects unknown operations. |
| D-012 | PARTIAL | The shell is a native Genode Nitpicker client and requests Input directly from the single-session filtered-input bridge. UEFI QEMU verifies pointer motion, click-to-focus, and keyboard text insertion; physical hardware input is untested. |
| D-013 | LOCKED | Preserve the user's 2026-10-10 desktop screenshot as a separate canonical composition reference. The prior desktop concept and newer screenshot have distinct visual systems. A clean wallpaper may be maintained as a documented derived asset, never as a flattened fake desktop. |
| D-014 | IMPLEMENTED (QEMU) | Genode's filtered Event stream is bridged into a single-session Input service routed to the HermOS shell. The bridge enables its input queue when the shell connects. Pointer coordinates from the QEMU USB tablet are normalized from 0–32767 to framebuffer pixels. Physical device behavior and per-window focus routing remain future work. |
| D-015 | PARTIAL | The desktop home surface follows the screenshot's overall panel placement and central hub, but pixel fidelity is unverified and the installer storyboard remains unimplemented. |
| D-016 | IMPLEMENTED | HermOS V8 Aladar begins with a static, versioned, read-only Hermes procedure registry. `hermes.system-status@1` reports success only after checking the live director health interface. No AI backend or worker authority is implied. |
| D-017 | IMPLEMENTED (QEMU) | Runtime composition follows SculptOS: a component owns a config document, publishes it as a Report, `report_rom` converts it into a ROM, and a nested `init` applies it at runtime. HermOS V0.16 applies this to a worker sandbox owned by the Hermes Core Director; the rest of the system composition is still static in the boot image. |
| D-018 | LOCKED | A worker is only "live" when its Hermes session exists. Declaring a worker in a config is recorded as `STARTING`; the director increments its live count in the session constructor and decrements it in the destructor. The UI may only display that count. |
| D-019 | LOCKED | The director's worker allowlist (binary, caps, RAM, service routes) is compile-time data in the director. A request may name a worker but never selects a binary, a quota or a route. |

## Milestone 1

* Build the native Hermes director and a test client from the HermOS repository.
* Route a real session from the test client to the director in a Genode `init` configuration.
* Verify health, one structured request/response, proposal/unsupported distinction, and runtime logs.
* Build a system image with Genode's `build_boot_image`/run flow and QEMU boot it without attaching a block device.
* Keep visual desktop fidelity, interactive input, and installer gates incomplete until they have functioning display/input and reference comparison evidence.

Acceptance evidence must contain the command, source commit, generated output and unedited build/serial log. No generated filename or screenshot alone satisfies a build or runtime gate.
