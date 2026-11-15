# HermOS on Genode 26.08

This repository contains HermOS V8 Aladar in staged form: a native Genode Hermes Core Director, bounded session ABI, a static deterministic procedure registry with one verified read-only workflow, integration client, and native Nitpicker shell. It remains an early system prototype, not a complete V8 release.

## Current implementation

* `app/hermes_director` publishes the Hermes service, returns truthful proposals, and dispatches `hermes.system-status@1`. It has no AI backend or disk/device authority.
* `test/hermes_director_client` verifies health, a clarification response, the deterministic procedure's verified terminal result, and unknown-operation rejection.
* `app/hermos_shell` draws the supplied desktop composition with native Genode drawing calls, embeds the supplied Golden Compass bitmap, and queries Hermes. The hub, dock, sidebar, prompt suggestions, and keyboard field respond to input; applications outside the director are explicitly marked unconfigured.
* `run/hermes_director.run` is the passing minimal IPC composition. `run/hermos_desktop.run` composes the shell, director, Nitpicker, boot framebuffer, filtered USB/PS2 input, AHCI, and NVMe.

## Disk installation

Sculpt OS documents whole-disk installation by writing its verified hybrid image from a separate live Linux system. HermOS provides the same approach via [`doc/INSTALLER.md`](doc/INSTALLER.md) and the guarded [`tools/install-hermos-to-disk.sh`](tools/install-hermos-to-disk.sh). The script requires a whole, unmounted target disk and exact-path confirmation, then verifies the written image. It is not a graphical in-OS wizard, and persistent data partition setup is not implemented.

## Build and run the passing Hermes milestone

From the Genode 26.08 checkout root:

```sh
make -C build/x86_64 app/hermes_director test/hermes_director_client \
  CROSS_DEV_PREFIX=/home/shiro/HermOS/genode-main/genode-26.08/toolchain/bin/genode-x86-

make -C build/x86_64 run/hermes_director KERNEL=nova BOARD=pc \
  CROSS_DEV_PREFIX=/home/shiro/HermOS/genode-main/genode-26.08/toolchain/bin/genode-x86- \
  QEMU_RUN_OPT='--include power_on/qemu --include log/qemu --include image/iso' \
  QEMU_OPT='-accel kvm -display none'
```

## Read next

* [`doc/MILESTONE_STATUS.md`](doc/MILESTONE_STATUS.md) — actual builds/runs, pass/failure state, and safety boundary.
* [`doc/INSTALLER.md`](doc/INSTALLER.md) — Sculpt-based image installation procedure and limits.
* [`doc/SOURCE_REVIEW.md`](doc/SOURCE_REVIEW.md) — supplied postulates, V7 prompt, and Genode references reviewed.
* [`doc/DECISIONS.md`](doc/DECISIONS.md) — locked constraints and current architecture decisions.
* [`doc/HERMES_DIRECTOR.md`](doc/HERMES_DIRECTOR.md) — implemented ABI and future protocol boundaries.
* [`doc/V8_ALADAR_ROADMAP.md`](doc/V8_ALADAR_ROADMAP.md) — required V8 contents, current evidence, and release gates.
* [`doc/HARDWARE_SUPPORT.md`](doc/HARDWARE_SUPPORT.md) — observed target devices, wired drivers, and unsupported peripherals.
* [`doc/DESIGN.md`](doc/DESIGN.md) — visual sources, geometry rules, and incomplete display gate.

The custom `tool/run/image/hermos_iso` helper is local code layered over the stock Genode ISO module. The test image is a UEFI/BIOS hybrid. Its GRUB configuration takes its graphical terminal, font, and background pattern from the Sculpt run script in `repos/gems/run/sculpt.run`. QEMU confirms visible UI, pointer click, and keyboard text entry; the physical laptop has not been boot-tested. See the milestone report for remaining visual and hardware gaps.
