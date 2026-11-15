# HermOS V7 source review

Review state: **reviewed for the first architecture and integration decisions**. The supplied source set consists of three Genode PDFs, the HermOS build-prompt Markdown file, and the visual reference image. The three PDFs are readable and text-extractable; their page counts are 553, 146, and 193. This index records the sections used to choose the initial component model and build path. It is not a claim that every unrelated chapter is applicable to HermOS.

## Project requirements and postulates

| Source | Relevant material | How it constrains HermOS |
| --- | --- | --- |
| `HermOS_Master_Build_Prompt_v7_EXACT_VISUAL_REFERENCE_FIDELITY.md` | §§ 1, 3.1–3.3, 4–12 (especially § 3.1 first director milestone and § 5.7 visual verification) | Requires a source-grounded run path, a real director request/health contract, canonical images, accurate status reporting, and QEMU-only destructive tests. |
| Same | Visual-fidelity, peripheral, installer, and release requirements throughout | The supplied screenshot is the design reference; interactive controls, input, installation, and physical validation are distinct implementation gates. |

## Genode reference pack

Page numbers below are the printed page numbers in each book. All paths refer to the supplied originals in `/home/shiro/Downloads/HermOS_Master_Prompt_v7_EXACT_VISUAL_REFERENCE_FIDELITY_with_3_Genode_PDFs(1)`.

| Source | Sections/pages reviewed for this work | Use and limits |
| --- | --- | --- |
| `genode-foundations-26-05.pdf` | §§ 2.2–2.5, pp. 21–35: tree, build, initial scenario and component; §§ 3.1–3.6, pp. 39–93: capabilities, recursive structure, resource assignment, core, component creation and IPC; § 4.5, pp. 108–117: session interfaces; § 5.4, pp. 144–150: system integration; § 6.3, pp. 178–179: init configuration. | Basis for a native Genode component, session/RPC interface, startup composition, and actual run-tool integration. Details must still match 26.08 source. |
| `genode-applications-26-04.pdf` | §§ 3.1–3.3, pp. 19–29: init, native/libc/POSIX APIs and VFS; § 3.7, pp. 39–41: Nitpicker GUI and SDL, Qt, LVGL. | Confirms GUI/framework choices exist in the documented Genode ecosystem. Sculpt is not the HermOS policy or product foundation. |
| `genode-platforms-26-05.pdf` | § 2.1.3–2.1.4, pp. 13–16: known-good path and development workflow; § 2.7, pp. 74–86: run-script and test flow; § 2.8: user-level device-access guidance; §§ 2.12–2.14, pp. 140–177: networking, display and touch bring-up. | This guide is principally an SoC/ARM porting narrative, not evidence that this x86-64 laptop's devices are supported. It informs bring-up discipline only. |

## Verified in the Genode 26.08 checkout

Checkout: `/home/shiro/HermOS/genode-main/genode-26.08`, tag commit `d18ee9e917200b059549e21f2dd9a55bfe61a0eb`; current worktree is on `hermos/v7` and contains earlier task changes. The official scenario and tooling inspected were:

* `repos/os/run/demo.run`: builds `app/status_bar` and `test/nitpicker`, imports GUI/demo components, configures `nitpicker`, `drivers`, `scout`, and launchers, then calls `build_boot_image` and `run_genode_until forever` outside autopilot mode.
* `tool/run/boot_dir/nova`: constructs the NOVA boot directory, installs the ISO boot loader where selected, generates the GRUB configuration, and calls the run tool's `run_image` hook.
* `tool/run/image/iso` and `tool/run/iso.inc`: Genode's stock ISO path installs GRUB's BIOS El Torito image/modules and calls xorriso through the Genode run tool.
* `tool/run/image/uefi`: creates a UEFI disk image; in this revision the stock ISO module does not itself add a UEFI El Torito entry.
* `tool/run/run` and `tool/run/README`: run-tool inclusion and scenario execution behavior.

Earlier in this task, `tool/run/image/hermos_iso` was added as a **local extension** over `image/iso`, embedding standalone EFI GRUB and adding an EFI El Torito image. It is not an upstream Genode module. Any future image generated with it must be described as a HermOS run-tool extension based on Genode's image flow, with its local diff/log preserved; do not call the extension upstream or official.

## Engineering proposals (not Genode features)

1. `Hermes Core Director` is a HermOS native Genode component with a dedicated session interface, explicit in-memory task state, and a narrow deterministic proposal procedure. It does not receive block, raw disk, model/network, or broad parent capabilities in the first increment.
2. Use the existing Nitpicker GUI/session composition from Genode examples. Select the application rendering toolkit only after verifying its 26.08 recipes, packages, and build output; no toolkit is assumed to be present just because it appears in a reference book.
3. The planner, approval surface, and any disk-writing installer executor remain separate components. The live system will not expose the host physical disk to QEMU.
4. The desktop and installer images are composite visual references. Extracting reusable rules or a clean landscape crop is a derived asset and must be documented; the composite itself must not be used as a faux live screen.

## First milestone and acceptance evidence

The director component, shared-dataspace session, deterministic response, unsupported-operation handling, init composition, and integration client are implemented and actually passed the stock Genode 26.08/NOVA QEMU scenario. The shell source compiles and its startup health query reaches Hermes in the desktop QEMU composition. The screen itself remains blocked at framebuffer bring-up: BIOS supplied no boot framebuffer and the tested UEFI route did not supply the supported framebuffer fields. See [`MILESTONE_STATUS.md`](MILESTONE_STATUS.md) for exact gate states, commands, output hash, and logs.

The independent second-model review was not performed in this environment; it remains a validation item rather than an implied pass. Installer UI and installer execution remain unimplemented. Physical boot and disk-install tests were not run.
