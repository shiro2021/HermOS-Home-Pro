# HermOS V8 Aladar milestone status

Updated 2026-10-11. This report separates source/build, runtime, display, ISO, and hardware evidence. A green state below means only the stated command and assertions passed.

## Installer executor update (2026-10-11)

The installer executor is now integrated beside the AHCI driver in the nested
Genode driver subsystem. A **separate test-only** QEMU ISO routes a disposable
AHCI target to it and includes a client that sends `CONFIRM_ERASE`; the client
then waits for executor completion. The 7,979,008-byte system image was
written and fully read back byte-for-byte on a 64 MiB virtual target. USB HID
keyboard and pointer enumeration also appeared in the same serial log:
`artifacts/logs/hermos-v8-installer-qemu.serial.log`.

A separate product prototype ISO was rebuilt without the test client or
test-only AHCI writable-default policy:
`/home/shiro/HermOS/HermOS Versions/HermOS-Genode-26.08-V8-Aladar-Installer-Prototype.iso`
(SHA-256 `dd76885c6c37ffdbf8fcf21ae3a21ea75c95f7ef6b038c847762b353a13691f5`).
That ISO contains the executor but was not boot-tested after its production
composition was built. There is no graphical disk selector/confirmation
wizard; a top-level test client timed out waiting for the nested installer
service, so the API is not yet bridged to the HermOS desktop. Failure evidence
is in `artifacts/logs/hermos-v8-installer-service-bridge-failed.serial.log`.
This remains a prototype and is not ready for physical installation.

## V8 Aladar update (2026-10-11)

* Hermes deterministic procedure registry: **PASS in Genode/NOVA QEMU** for
  `hermes.system-status@1`. The integration client verified the exact task
  id, `SUCCEEDED`, `deterministic`, and `VERIFIED` result after the director
  health check. Build log: `artifacts/logs/hermos-v8-hermes-build.log`;
  runtime log: `artifacts/logs/hermos-v8-hermes-qemu.log`.
* V8 prototype hybrid ISO was built by the local Genode `run/hermos_desktop`
  workflow and copied to
  `/home/shiro/HermOS/HermOS Versions/HermOS-Genode-26.08-V8-Aladar-Prototype.iso`.
  SHA-256: `721d4a3333230c341d7ad9962be05f9472a929f3bee5ef0787e2fd8afc6de699`.
* Direct UEFI QEMU boot of that exact ISO reached the live desktop at 1024x768;
  QMP reported the VM running and the capture is
  `artifacts/visual-regression/hermos-v8-aladar-boot.png`. QEMU was stopped by
  QMP after capture. The first wrapper attempt failed because an OVMF variable
  file was missing; a later wrapper run timed out after launching QEMU, then a
  direct QMP boot reached the desktop. No physical disk was used.
* Genode's native `image/disk` flow produced
  `HermOS-Genode-26.08-V8-Aladar-System-Prototype.img` (7,938,048 bytes,
  GPT/EFI, three partitions), SHA-256
  `1a81328926ab3a338e348c2dcb6dd9f5054c5954388049a215b76e7f219cc74b`.
  The image was copied with `dd` to a same-size disposable QEMU target; the
  complete read-back hash matched, and that target booted the live HermOS
  desktop under OVMF with no ISO attached. Capture:
  `artifacts/visual-regression/hermos-v8-gpt-installed-target.png`.
  Details: `artifacts/logs/hermos-v8-gpt-installed-target.txt`.
* `HermosInstall` executor is integrated in the nested driver composition and
  passed a test-only QEMU write/read-back test. It has no graphical target
  selector or confirmation UI and has not been tested on physical hardware.
* The images remain **V8 Aladar prototypes**. The raw-image writer is still
  host-side; the in-system executor now passes a disposable QEMU disk test,
  but there is no integrated installer UI or bare-metal install validation.
  General AI/worker fabric, configured applications, persistent storage
  workflow, and bare-metal validation are also missing. See
  `V8_ALADAR_ROADMAP.md` for independent release gates.

## V0.16 worker sandbox (2026-10-25)

Runtime composition taken from SculptOS: the director owns a sandbox config
document, publishes it as the `workers` report, `report_rom` turns it into the
nested init's `config` ROM, and `init` starts/stops children at runtime.

* **PASS (QEMU, NOVA)** — full worker lifecycle on the release ISO and on the
  release disk image: `worker.start w1` → `STARTING` → worker session ACTIVE →
  `[worker] alive capabilities={LOG,Timer,Hermes} hardware=none storage=none`
  → duplicate start rejected → `worker.stop w1` → session ENDED → `live=0`
  → `[hermes_test] PASS`.
  Evidence: `artifacts/visual-regression/HermOS_V0.16-release-iso.serial.log`,
  `artifacts/visual-regression/HermOS_V0.16-release-disk.serial.log`.
* **PASS (QEMU)** — standalone gate `run/hermes_director` (no shell, no drivers)
  passes the same lifecycle: `HermOS_V0.16-director-gate.serial.log`.
* **PASS** — `Active Agents` in the desktop is fed by `response.workers`, i.e.
  by session count, and shows 0 when no worker exists.
* **NOT IMPLEMENTED** — applications and drivers are still started from the
  static boot config; only the worker sandbox is runtime-composed. No depot
  package resolution, no installation, no update/rollback, no persistent
  worker state.
* **NOT RUN** — physical hardware.

## Target and safety boundary

* Genode source tag: 26.08, commit `d18ee9e917200b059549e21f2dd9a55bfe61a0eb`.
* Kernel target: NOVA x86-64; test machine: QEMU with KVM, Q35/Skylake CPU model, 800 MiB guest RAM.
* No host physical disk was attached or written. Installer test writes targeted only `/tmp/hermos-installer-qemu-target-20261011.img`, a 64 MiB disposable QEMU raw disk. The separate 512 MiB boot-target file was attached for the QEMU system composition.
* Hardware inspection identified an Intel N97 laptop and its onboard devices, but no live boot or device support was tested on the physical machine.

## Gates

| Gate | State | Evidence |
| --- | --- | --- |
| Upstream Genode/NOVA stock ISO baseline | PASS | Stock `run/nova` scenario completed its test and printed `Test succeeded` / `Run script execution successful.` |
| Hermes Director build | PASS | `app/hermes_director` and `test/hermes_director_client` compile under Genode 26.08. |
| Installer executor build | PASS | `app/hermos_installer_executor` builds and runs in the nested Genode driver subsystem. |
| Hermes IPC/runtime integration | PASS | `artifacts/logs/hermos-v8-hermes-qemu.log` verifies director `READY`, clarification behavior, one `hermes.system-status@1` request returning `SUCCEEDED` only after the health check, and unknown-operation rejection. |
| Native shell build | PASS | `app/hermos_shell` compiles and links; it embeds the exact supplied emblem and a separate 1024×768 RGB landscape derivative. |
| Shell runtime start | PASS | QEMU log prints `HermOS intent UI connected to Hermes; health=READY`. |
| Display / framebuffer | PASS (UEFI QEMU) | Direct OVMF QEMU capture at `artifacts/visual-regression/hermos-desktop-user-reference-render.png` shows the live desktop at 1024×768. BIOS QEMU does not expose a usable framebuffer. |
| Visual match | PARTIAL | The 1024×768 UEFI QEMU view now follows the reference's major panel proportions, tall central compass hub, and icon dock more closely. Wallpaper, text/icon detail, and truthful zero-worker content still differ from the 1600×900 reference. |
| Mouse and keyboard input | PASS (basic UEFI QEMU interaction) | QMP tablet click focuses the Hermes field; the cursor is visible and follows motion; a QEMU key event inserted `a`. Evidence: `artifacts/visual-regression/hermos-native-ui-input-proof.png`. Bare-metal input is untested. |
| Interactive controls | PARTIAL (QEMU) | Sidebar, dock, hub nodes, and field accept clicks; selected views report unconfigured application services truthfully. Full apps and all screenshot affordances are not implemented. |
| Genode GPT/EFI system disk | PASS (QEMU image-copy/boot) | `image/disk` produced a GPT/EFI disk image; `dd` copied it to a disposable same-size QEMU target; read-back SHA matched; OVMF booted from the target with no ISO. Physical disk and writer-script execution are untested. |
| Sculpt-style hybrid-image copy | PASS (QEMU image-copy/boot) | Earlier hybrid ISO copy/read-back/boot proof remains in the prior prototype history. The current release candidate uses the smaller native Genode GPT/EFI image for disk installation. |
| SculptOS-inspired GRUB bootloader | PASS (local extension) | `tool/run/image/hermos_iso` uses Sculpt’s graphical-terminal, font, and centered `tool/boot/boot.png` background pattern while retaining HermOS boot labeling and hybrid BIOS/UEFI boot. Reference: `repos/gems/run/sculpt.run`. |
| Hybrid ISO image creation | PASS (local extension only) | V8 artifact: `/home/shiro/HermOS/HermOS Versions/HermOS-Genode-26.08-V8-Aladar-Prototype.iso` (SHA-256 `721d4a3333230c341d7ad9962be05f9472a929f3bee5ef0787e2fd8afc6de699`). Uses local `tool/run/image/hermos_iso` layered over stock `image/iso`; it is not upstream Genode. |
| HermOS desktop ISO gate | PASS (UEFI visual boot) | The generated ISO booted under OVMF in QEMU and rendered the HermOS desktop. The run wrapper was stopped after the ISO build and QEMU launch; this is not a clean wrapper-completion PASS. |
| USB keyboard/tablet driver enumeration | PASS (QEMU) | Serial log shows `usb_hid` connected a USB keyboard and absolute USB tablet. Pointer and keyboard events reached `event_to_input`; BIOS output confirms its queue remained disabled because BIOS had no usable framebuffer. |
| Shell-side keyboard/pointer interaction | PASS (basic UEFI QEMU) | Pointer cursor, field focus, and inserted keyboard text appear in the captured screen. This is QEMU evidence, not bare-metal proof. |
| Installer executor write/read-back | PASS (QEMU test-only ISO) | Serial log verifies explicit erase RPC, write of 7,979,008 bytes, full read-back, and completion on a 64 MiB disposable AHCI disk. |
| Graphical installer UI and target selection | NOT IMPLEMENTED | No user-facing installer wizard, device selector, or final confirmation screen. |
| Dedicated-laptop boot | NOT RUN | No physical boot test was performed. |
| Installer disk boot test | PASS (QEMU prototype) | Current native disk image was copied to a disposable QEMU target; full image read-back SHA matched; OVMF booted without installer media. This validates the disk artifact and boot path, not an integrated installer UI or executor. |
| Independent second-model review | NOT RUN | Not available in this execution. |

## Commands actually run

Upstream stock Genode/NOVA baseline:

```sh
make -C build/x86_64 run/nova KERNEL=nova BOARD=pc \
  CROSS_DEV_PREFIX=/home/shiro/HermOS/genode-main/genode-26.08/toolchain/bin/genode-x86- \
  QEMU_RUN_OPT='--include power_on/qemu --include log/qemu --include image/iso' \
  QEMU_OPT='-accel kvm -display none'
```

Shell build:

```sh
make -C build/x86_64 app/hermos_shell \
  CROSS_DEV_PREFIX=/home/shiro/HermOS/genode-main/genode-26.08/toolchain/bin/genode-x86-
```

Hermes director/client build and QEMU integration:

```sh
make -C build/x86_64 app/hermes_director test/hermes_director_client \
  CROSS_DEV_PREFIX=/home/shiro/HermOS/genode-main/genode-26.08/toolchain/bin/genode-x86-

make -C build/x86_64 run/hermes_director KERNEL=nova BOARD=pc \
  CROSS_DEV_PREFIX=/home/shiro/HermOS/genode-main/genode-26.08/toolchain/bin/genode-x86- \
  QEMU_RUN_OPT='--include power_on/qemu --include log/qemu --include image/iso' \
  QEMU_OPT='-accel kvm -display none'
```

Desktop runtime was also run with the local hybrid ISO extension and QEMU UEFI pflash. The exact invocation and serial output are in `artifacts/logs/hermos-desktop-uefi-qemu.log`. QEMU was deliberately quit after screen capture; the run wrapper reports the resulting process exit as an error.

## Run records

* `artifacts/logs/hermes-component-build.log` — director/client compile.
* `artifacts/logs/hermes-director-qemu.log` — passing Hermes QEMU integration.
* `artifacts/logs/hermos-shell-build.log` — passing native shell compile/link.
* `artifacts/logs/hermos-desktop-qemu.log` — BIOS/NOVA desktop boot reaches Genode and the shell but framebuffer setup fails.
* `artifacts/logs/hermos-desktop-final-build-run.log` — unedited Genode build/run output showing the local hybrid ISO path and OVMF launch; the QEMU wrapper was stopped after image creation.
* `artifacts/logs/hermos-desktop-final-bios-input.serial.log` — unedited QEMU serial output showing NOVA, Hermes, USB HID keyboard/tablet enumeration, and bridge event reception.
* `artifacts/logs/hermos-desktop-final-uefi.serial.log` — UEFI serial capture from the desktop boot.
* `artifacts/visual-regression/hermos-native-ui-input-proof.png` — 1024×768 QEMU screen showing the live pointer, focused prompt field, typed character, and responding Home state.
* `artifacts/visual-regression/hermos-installed-disk-boot.png` — desktop booted from the QEMU target disk after image write.
* `artifacts/visual-regression/hermos-desktop-user-reference-side-by-side.png` — reference and runtime capture at natural sizes; no aspect-ratio distortion.
* `build/x86_64/artifacts/logs/genode-nova-stock-iso-baseline.log` — upstream stock baseline pass.

The image was rebuilt with pointer redraw and reference-proportioned hub/dock changes. QEMU verifies basic pointer/keyboard interaction and boot from an image-written virtual disk. The CLI writer itself has syntax-only verification; persistent-storage setup, the nine-screen graphical installer, and physical laptop support remain incomplete.
