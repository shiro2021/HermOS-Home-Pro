# HermOS V8 Aladar delivery gates

V8 Aladar is the requested product edition. This roadmap consolidates the
requirements in the supplied V7 master build prompt and reference pack. It
does not treat the prompt as a command to claim completeness. Every release
gate requires an artifact and the stated evidence.

## Product definition

HermOS is a Genode 26.08 / NOVA x86-64 operating system with a native desktop,
Hermes Core Director as a boot-integrated system service, an integrated
installer, least-authority component composition, and a real procedure/worker
workflow. The supplied HermOS images define the visual target. Sculpt OS is a
hardware and integration reference, not HermOS's product or policy foundation.

## Required V8 contents

| Area | Required behavior | Current evidence/state |
| --- | --- | --- |
| Boot and system image | Reproducible Genode build; genuine BIOS/UEFI image; system boots from live media and installed target | Prototype hybrid ISO and native GPT/EFI disk image boot in UEFI QEMU; not a completed V8 image |
| HermOS desktop | Match the supplied 1600x900 desktop composition, preserve exact emblem, native controls, workspaces, panels, and app entry points | Native 1024x768 shell and input work in QEMU; visual match is partial; most apps are not configured |
| Hermes Director | Starts with the system; receives structured intent; tracks tasks; exposes plans, events, truthful state; selects services, deterministic procedures, and AI when available | Health interface and one verified read-only procedure; no persistent task history, approval broker, dynamic workers, or AI backend |
| Agentic workflow | Safe deterministic dispatch; unavailable AI reported honestly; scoped capabilities; approval for consequential changes; observe/verify; cancellation and cleanup | One procedure verified in Genode QEMU; general worker fabric is not implemented |
| Installer | Native nine-screen flow from boot/welcome through detection, target, plan, review, write, verification, and completion | Guarded host-side raw-image writer; Genode GPT/EFI image copied and booted from disposable QEMU disk; no integrated graphical installer or partition setup |
| Peripherals | Enumerate and verify actual target graphics, keyboard, pointer, storage, Wi-Fi/network, audio, power and boot mode | QEMU USB keyboard/tablet, virtual AHCI storage and framebuffer verified; target inventory and source-based gaps are in `HARDWARE_SUPPORT.md`; target-laptop support not verified |
| Installed system | Hermes and required drivers start from installed generation; installer media can be removed | V8 native GPT/EFI image booted directly and after copy to a QEMU disk with no ISO attached; physical install remains untested |
| Security and recovery | Least authority, explicit approval, audit records, verified updates, rollback/recovery | Capability routing exists for initial composition; approval, update, rollback and recovery workflows remain future gates |

## Implementation order

1. Preserve build reproducibility and QEMU boot/input baselines while updating
   the product to V8 Aladar.
2. Expand Hermes from the first deterministic procedure into task/event
   records, multiple declared procedures, independent verification, and a
   truthful UI view.
3. Implement capability broker and approval states before any write-capable
   executor. Add cancellation, failure cleanup and recovery evidence.
4. Add an AI backend interface with explicit unavailable behavior; then add
   bounded workers and hybrid AI-plan / deterministic-execution / verification
   only after the non-AI flow is secure and testable.
5. Implement the installer as a native HermOS workflow with separate planner,
   review/confirmation, and narrowly authorized executor. Test only against a
   dedicated disposable QEMU disk until target hardware support is established.
6. Close visual fidelity against the unchanged user references at the same
   viewport and geometry, then implement the referenced apps and system
   surfaces as functional Genode components.
7. Bring up target laptop peripherals one by one from actual device
   enumeration and driver evidence, then test live boot, install, and boot
   without installer media.

## Release labels

Use `HERMOS V8 ALADAR PROTOTYPE` until every required gate has evidence. Report
Hermes as BUILT, INCLUDED, STARTED, and READY separately. Report live boot,
QEMU installation, physical live boot, physical installation, installed boot,
input, graphics, storage, network, audio, and power independently. Never call
the system complete while any required release gate is partial, not run, or
not tested.
