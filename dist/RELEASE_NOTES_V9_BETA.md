# HermOS V9 Beta

UEFI/BIOS x86_64 installer ISO for HermOS on Genode 26.08 / NOVA. This is a **beta**: not tested on real
hardware, not for installing onto a computer you care about.

V9 Beta has the same features as V8. What changed is the project around it: one repository, one default
branch, images outside git, and a reproducible build command.

## In this release

| Feature | State |
|---|---|
| HermOS desktop (Cells, command bar, Files, Agents pane, Capability Broker, Kill Mode Meta+K) | as in V8 |
| Shiro Brain: llama.cpp on Genode, answers questions about HermOS offline from a knowledge base of 203 facts | as in V8 |
| Shiro-Mini model (4.2 M parameters): copies its answer from the retrieved facts; 27/40 on unseen questions | as in V8 |
| MyKee router: local `local://shiro` first, cloud routes after (no cloud key in the image) | as in V8 |
| Forge: checks component proposals ("Create a component named stopwatch.") | as in V8 |
| Installer with persistence partition (HERMOS-DATA as the terminal's /home) | as in V8 |
| Version shown in the System window: "HermOS V9 Beta (Genode 26.08)" | new |

## Not in this release

- Reasoning: the model picks and copies facts. It can pick a wrong fact; a question the facts do not cover may
  get a loosely related one.
- Storing or approving generated components (no Vault, no approval step, no REA Cell UI).
- Hermes Director, workers and procedures in the image; approval and audit log.
- A HermOS boot screen. Testing on real hardware.

## Repository changes

- HermOS-Home-Pro is the only repository. HermOS-V4beta, hermos5 and HermOS-Private are fully contained in it;
  the HermOS-Server history is kept as `archive/hermos-server`, and its design documents are in `docs/server/`.
- `main` is the default branch and follows the newest release line. Old branches are archived as `archive/*` tags.
- ISO images are attached to releases instead of committed. Python bytecode is no longer committed.
- `tools/build-hermos-v9-beta.sh` builds the release files and records the exact commit, Genode tree and model
  checksum in `BUILD-INFO.txt`.

## Test evidence

<!-- Fill in from the laptop run before publishing. Do not publish with these lines empty. -->

| Check | Status | Evidence |
|---|---|---|
| Build (`tools/build-hermos-v9-beta.sh`) | NOT TESTED | |
| ISO boots to the desktop in QEMU (UEFI) | NOT TESTED | |
| System window shows "HermOS V9 Beta" | NOT TESTED | |
| Shiro Brain answers "What does Meta+K do?" | NOT TESTED | |
| Install to a blank 16 GB target image, reboot from it | NOT TESTED | |
| Real hardware | NOT TESTED | |

## Checksums

See `SHA256SUMS` attached to this release.
