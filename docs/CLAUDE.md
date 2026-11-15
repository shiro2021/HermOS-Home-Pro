# HermOS — briefing for Claude Code sessions in this folder

Project spec: `HermOS_Master_Build_Prompt_v7_EXACT_VISUAL_REFERENCE_FIDELITY.md` (and the claude.ai
Project "HermOS" master prompt). Genode references: the three `genode-*.pdf` files here.

## Layout
- Genode 26.08 build tree: `worktrees/HermOS-readiness` (commit d18ee9e9…), build dir `build/x86_64`.
- HermOS code: `worktrees/HermOS-readiness/repos/hermos` (run script `run/hermos_desktop.run`,
  apps under `src/app`, docs incl. `doc/MILESTONE_STATUS.md`, `doc/V8_ALADAR_ROADMAP.md`, `doc/DECISIONS.md`).
- Release scripts: `tools/` (`build-hermos-installer-release.sh VERSION`, `run-hermos-headless-boot.sh`).
- Release outputs: `HermOS_V<ver>.iso/.img/.sha256` in this folder; logs in `artifacts/logs/`.
  Scripts refuse to overwrite an existing version — always bump the version.

## Last tested release (2026-10-31, V0.17)
- `./tools/build-hermos-installer-release.sh 0.17` — build PASS (system image + installer ISO).
- `./tools/run-hermos-headless-boot.sh HermOS_V0.17.iso artifacts/logs/V0.17-boot 60` — UEFI QEMU
  boot PASS: installer Welcome screen renders (screenshot `artifacts/logs/V0.17-boot.png`, PPM data).
- Serial log only shows OVMF lines, no Genode output — fix serial logging for evidence.
- Host: Intel N97 4c/4t, 16 GiB RAM, Ubuntu 24.04.

## Installer gaps — source work done 2026-10-31 (V0.18, NOT yet built on Genode)
Details: `repos/hermos/doc/INSTALLER.md`, decisions D-017…D-022 in `doc/DECISIONS.md`.
- Serial: root cause = Bender's `serial_fallback` only added for `image/uefi`; fixed via
  `repos/hermos/tool/run/hermos_bender_serial.inc` used by `tool/run/image/hermos_iso` and new
  `repos/hermos/tool/run/image/hermos_disk` (release script includes it after `image/disk`).
- "Garbled stripe" was the old UI's 1-px "sunrise bands"; old UI replaced entirely.
- Shared window system `include/hermos_ui/canvas.h`: six-vertex hexagon (C = W×0.085), carbon
  title bar, Solar Orange border + glow, controls drawn disabled, title-bar drag. All 9 pages in
  `include/hermos_installer/screens.h`; planner/flow in `flow.h`; UI `src/app/hermos_installer_ui`.
- Target bound by identity (AHCI policy model `HERMOS_INSTALL_TARGET`, serial `HERMOSTARGET01`);
  UI cross-checks executor status vs AHCI `ports` report; approve RPC carries reviewed sizes (ABI 2);
  executor revalidates Block info right before writing.
- Checked here (no Genode toolchain): host geometry/flow tests + previews PASS
  (`repos/hermos/src/test/hermos_ui_host/run.sh`); UI/executor/test client compile to objects
  with Genode's strict flags against 26.08 headers (host gcc 13). Tcl run-module changes untested.

## No QEMU (user decision 2026-10-31, D-023)
- Latest Genode release is 26.08 = this tree; GitHub `staging` is newer but unreleased.
- Tests run in **VirtualBox 7 on Genode/NOVA**, booted natively from a USB stick:
  `repos/hermos/run/hermos_vbox_host.run` (+ `.vbox`), host image via `tools/build-hermos-vbox-host.sh`.
  No storage driver in the host; install target = empty VDI in RAM (`repos/hermos/tools/make-empty-vdi.py`).
- `build.conf` and run scripts are build-only. `tools/install-hermos-to-disk.sh` writes USB disks only.
- Reboot into the installed disk is not testable yet (D-024): needs an installer reboot path.

## Live desktop (user request 2026-10-31, D-025) — source done, NOT built yet
- Desktop = 10 Oct screenshot layout, `repos/hermos/include/hermos_desktop/desktop.h`; shell
  `src/app/hermos_shell` rewritten: real RTC clock, platform_info facts, Hermes Director panel,
  embedded Bash terminal (shell provides the Terminal service), System window, honest notices.
- Live image has no storage driver. Build: `tools/build-hermos-live.sh 0.19` -> `HermOS_Live_V0.19.img`.
- Host checks pass (`repos/hermos/src/test/hermos_ui_host/run.sh`, incl. desktop_test + previews);
  shell compiles to an object with Genode's strict flags against 26.08 headers (host gcc 13).

## Next commands (user runs; Claude has file access but no shell on the laptop)
0. Prepare ports once: `./worktrees/HermOS-readiness/tool/ports/prepare_port virtualbox7 libiconv`
   (the run tool names any further missing port/depot archive).
1. `bash worktrees/HermOS-readiness/repos/hermos/src/test/hermos_ui_host/run.sh`
2. `./tools/build-hermos-installer-release.sh 0.18`
3. `./tools/build-hermos-vbox-host.sh 0.18`
4. `sudo ./tools/install-hermos-to-disk.sh HermOS_VBoxHost_V0.18.img /dev/disk/by-id/usb-<stick>`
5. Boot the laptop from that stick (UEFI, Secure Boot off, VT-x on); walk the installer in the VM.
Live desktop instead: `./tools/build-hermos-live.sh 0.19`, then step 4 with `HermOS_Live_V0.19.img`.

## Open items
- Everything above is NOT TESTED on Genode until steps 2–5 pass.
- Delete QEMU leftovers: `tools/{run-hermos-headless-boot.sh,run-hermos-installer-qemu.sh,
  run-hermos-interactive.sh,run-hermos-qemu.sh,run-hermos-system-qemu.sh,qmp-drive.py,
  test-hermos-install-qemu.py,build-hermos-release.sh}`.
- Delete unused `repos/hermos/include/hermos_ui/window_geometry.h`, `src/app/hermos_shell/emblem.rgba` (D-022 resolved).
- Backup GPT not moved to end of target disk; no recovery/rollback; Reboot button disabled.

## Agreed goal (user, 2026-10-31)
"Usable desktop after install": 1) finish installer + install to the test disk + reboot;
2) persistent storage; 3) desktop apps (terminal, file manager, text editor, settings) as native
Genode components in the HermOS window system; 4) Hermes procedures with approval + audit.

## Rules
- Never write to the laptop's internal disks; images go only to dedicated USB sticks,
  install targets only to disposable VM disk images. No QEMU.
- No sudo/root from Claude; ask the user to run any sudo command.
- Report PASS / FAIL / NOT TESTED honestly; never claim boot/install without evidence.
