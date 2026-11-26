# HermOS V5 Beta — what the system does

Written from the source tree and from tests run on the V5 Beta image. Each
function carries one of these marks:

| Mark | Meaning |
|---|---|
| **VERIFIED** | Run and observed working (QEMU/UEFI, evidence noted). |
| **BUILT** | The code is in the image and starts, but its behaviour was not tested. |
| **STUB** | The code exists but the real work is a placeholder. |
| **VISUAL** | Drawn on screen, no function behind it. |
| **ABSENT** | Not in the system. |
| **SOURCE ONLY** | In the repository, but not part of the V5 image. |

"VERIFIED" means **QEMU only**. Nothing has been run on physical hardware.

---

## 1. Platform and boot

| Function | State | Notes |
|---|---|---|
| Genode 26.08 on the NOVA microhypervisor, x86-64 | VERIFIED | Boots; `platform_info` reports the kernel and CPUs. |
| UEFI boot (GRUB, hybrid ISO and GPT disk image) | VERIFIED | Boots to the desktop at 1536×1024 under OVMF. |
| Legacy BIOS boot | ABSENT | Kernel starts but no display: `hermos_fb` needs a UEFI framebuffer. |
| Secure Boot | ABSENT | Must be off. |
| Direct-scanout framebuffer (`hermos_fb`) | VERIFIED | Maps the UEFI GOP framebuffer; no compositor in between. |
| Boot from the installed disk, no media | VERIFIED | SATA and NVMe, both. |
| Boot on real hardware | ABSENT | Never tested. |

## 2. Drivers (managed `drivers` subsystem)

| Function | State | Notes |
|---|---|---|
| ACPI parsing, PCI decode, platform driver (IRQ, DMA, IOMMU) | VERIFIED | |
| USB host controller + USB HID keyboard/pointer | VERIFIED | QEMU USB keyboard and tablet. |
| SATA/AHCI block driver | VERIFIED | Port numbering handled per port (`ahci0..7`). |
| NVMe block driver | VERIFIED | Needed 128 KiB transfers; see §6. |
| Wired NIC (iPXE driver: Intel e1000 family etc.) | VERIFIED | DHCP lease obtained in QEMU. Which real cards work is not known. |
| Real-time clock | VERIFIED | Taskbar clock shows the correct date and time. |
| Event distribution to the desktop (`event_to_input`) | VERIFIED | |
| Reboot (ACPI reset via the platform driver) | BUILT | Wired and routed; never pressed in a test. |
| Power off, suspend, battery, brightness | ABSENT | The platform driver can suspend; nothing asks it to. |
| Audio, Wi-Fi, Bluetooth, camera, SD reader | ABSENT | Taskbar speaker/network glyphs are VISUAL. |
| GPU drivers, multi-monitor | ABSENT | Boot framebuffer only. |
| PS/2, I²C-HID touchpads | ABSENT | Not in this composition. |

## 3. Desktop shell (`desktop_builder`, `theme_engine`)

| Function | State | Notes |
|---|---|---|
| Wallpaper, header, greeting, single search field | VERIFIED | |
| Search field typing | VERIFIED | Text is entered; Enter submits it to the agent (§5). |
| Clock (taskbar) | VERIFIED | Live from the RTC. |
| Left launcher dock: Files, Terminal, Install | VERIFIED | Auto-hides; hover the left edge. Install only on installable media. |
| Hide/show panels: hover, `Meta+L`, `Meta+B`, grip tabs, taskbar button | VERIFIED | Animated by elapsed time. |
| Agents & Workers pane: running apps with a close button, ▶ to launch | VERIFIED | Lists real apps only; "Active (n)" / "Inactive (n)". Hides and reveals by user click: the pane's X, the rail's Agents tile, or `Meta+A`; a slim full-height bar stays on the edge while it is hidden (click it to reveal; width `panels > right > slim`) with the rail docked beside it; Cells may use the freed width. Mode and width are `panels > right` in `theme.hid`. The mock-up's per-card ⋯ menu and sample thumbnails are not drawn (nothing real behind them). |
| "Hermes Agent: Ready" status | BUILT | Always shows Ready; the agent's real state is not queried. |
| Windows: frame, close, minimise, restore from the taskbar | VERIFIED | |
| Window maximise (uses the Plasma-style client area) | BUILT | |
| Cell drag by the title strip | VERIFIED | Moves; a grab in the top 10 px is an edge resize, not a move. |
| Work area: maximise, quick-tile (`Meta+arrows`, `Meta+F`, …) and edge snap use one rectangle (screen minus shown panels, the right pane or slim bar, and the rail; `panels > right > rail`) | VERIFIED | A Cell and its app content never reach under a panel; tiles follow the pane when it is hidden or shown. |
| Desktop shortcuts are not passed on to the focused app | VERIFIED | `Meta+A` used to type an `a` into the Terminal. |
| Cell edge resize, snap to screen edges | BUILT | Resize starts (edge detection verified); snap not re-tested. |
| OS-wide ink (`theme.hid` `+ ink`): active accent vs carbon-gray inactive | VERIFIED | One mapping in the canvas recolours every accent-coloured pixel (shapes, text, icons, glows) of an inactive Cell to carbon gray and dims its bright text; pictures keep their colours. Changing `accent` recolours every active element. Panels, rail and dock are always drawn active; their own selected/unselected states are separate. |
| Hosted Cell: title, minimise, maximise, close; opaque; true z-order | VERIFIED | Strip geometry derives from the Cell's own size. Minimise and taskbar restore tested; restore no longer reopens the mock-up Cells. |
| Hosted app follows its Cell's size (Gui `info` ROM `xpos ypos width height`, signalled on change) | VERIFIED | Terminal re-lays out to the Cell at launch and on maximise (1:1, no stretching). While a drag is running the old buffer is stretched; the new size is applied on release. Apps that ignore the info ROM (Files) still stretch. |
| Tiling and quick-tile (`Meta+T`, `Meta+arrows`, `Home/End/PgUp/PgDn`, `F`, `R`) | BUILT | Legacy code, untested. |
| Hosted apps (render inside Cells, receive input) | VERIFIED | Terminal and Files; Cell numbering bug fixed. |
| Theme (`theme.hid` → `theme_state`, 1/10000 geometry, panel modes, Cell frame look) | VERIFIED | The `+ frame` section (border width, glow, shadow, active/carbon colours) was changed in the file and the Cells followed. |
| System window: version, kernel, CPUs, graphics, storage, uptime, memory | VERIFIED | All measured; memory is the desktop's own heap usage. |
| System window side list (Hardware, Storage, Network, …) | VISUAL | Not clickable. |
| Navigation rail (Agents, System, Files, Network, Devices, Settings) | VISUAL | Tiles do nothing yet. |
| Welcome window | VERIFIED | Opens the installer on installable media; tips otherwise. |
| Mock-up preview windows (Files list, Hydroponics, Create Worker, Updates) | VISUAL | They show invented content. Off by default (`preview="yes"` turns them on). |
| Settings, notifications, workspaces, lock screen, login, user accounts | ABSENT | |

## 4. Applications

| Function | State | Notes |
|---|---|---|
| Terminal: bash 5.3 + GNU coreutils | VERIFIED | `echo` and `ls /` ran; `/` holds `bin dev home lib pipe share tmp`. |
| Terminal `/home` | VERIFIED | Mounted from the `HERMOS-DATA` ext2 partition (part_block + rump ext2fs). A file written in the Terminal was read back on the host; `e2fsck` clean. Falls back to RAM when no data partition exists. |
| Terminal `/tmp` | VERIFIED | RAM, lost at reboot. |
| Terminal text editor (vim) | ABSENT | Not in this image. |
| Files (`hermos_files`): start, browse `/Home` | VERIFIED | `/Home` is the persistent data partition (same ext2 service as the Terminal's `/home`); lists the real files. With no data partition it is empty RAM folders. Sample files removed. The Quick Access entries Documents/Downloads exist only if created (e.g. from the Terminal). Drawn at a fixed size, so it is stretched in a resized Cell. |
| Files: text editor, hex editor, image view, copy/move/delete/rename | BUILT | Code exists, untested. |
| Files: format converter | STUB | Codecs are placeholders. |
| Files: SSH/SFTP | STUB | Logs "stub". |
| Files: "Analyze" (AI parser hot-load) | STUB | |
| Web browser: HermOS Browser (smart bar, disposable profile, HTTPS-only Shield) | PARTIAL | `tools/hermos-browser`. The program builds and its logic is tested, and the image boots with it, but the depot's Qt/Chromium binaries were built for an earlier Genode release and do not run on 26.08 (see its README). Not part of the ISO. |
| Media player, office, settings app | ABSENT | |

## 5. Hermes agent and network services

| Function | State | Notes |
|---|---|---|
| Network stack (NIC → NAT router → agent, 10.0.3.0/24) | VERIFIED | DHCP lease and agent interface up in QEMU. |
| Hermes agent TLS: server certificate and host name verified against a CA bundle in the image; real entropy (`jitterentropy`) with a start-up self-check (two separate opens must differ, so a fixed file cannot pass); a key that is not a single URL-safe token is treated as absent; prompts and replies are logged only as a short printable excerpt; https only; 1 MiB reply cap | BUILT | Build input `HERMOS_CA_BUNDLE` (default: the build host's `/etc/ssl/certs/ca-certificates.crt`). Before this, verification was off and `/dev/random` was a constant string. Not exercised against the real service (no DNS on the test host); needs a correct clock (RTC). |
| Hermes agent: prompt → DeepSeek chat API → reply panel | PARTIAL | Verified: search field → agent → outgoing request → reply panel. The test host has no DNS, so the reply was "curl failed: Couldn't resolve host name". A real answer is untested. |
| MyKee key handling | BUILT | The key is no longer in the repository: the build copies the file named by `HERMOS_MYKEE_KEY_FILE` into the image (empty = no AI; the agent says so). **The previously committed key is still in git history: rotate it.** |
| MyKee router (from V4): the agent relays the prompt to `mykee_router`, which alone holds the key and speaks TLS to the AI service; a route list (`mykee.conf`) is tried in order, first answer wins; routes must be https; the key goes only to those routes; answers carry the request's sequence number, so an old answer is never shown for a new question; 90 s time-out in the agent | BUILT | The V4 router had verification off, logged full prompts and showed the previous answer for the next question; all three are fixed here. The agent no longer holds the key or a TLS client. Only one route (DeepSeek) is configured. |
| Shiro Local Brain (from V4): BM25 retrieval over the HermOS docs (`doc/*.md`, built into the image as `shiro_corpus.txt`) adds the best three passages to each prompt | BUILT | Retrieval only: no model runs on the device. The retrieved passages are sent to the AI service with the prompt. |
| Capability Broker (from V4): every app kind has a manifest (`hermos_manifest`); a launch whose manifest does not validate is refused | BUILT | Manifests state what the apps sandbox really grants (no network, no block access). V4's Terminal manifest asked for `nic: downlink`; V6 grants none, so it says none. |
| Kill Mode (Meta+K, from V4): closes the active Cell at once, destroys its sandbox and returns the quota | BUILT | |
| Maximize button toggles: a second press restores the Cell | BUILT | Hosted Cells have no undo control; a maximized Terminal could not go back before. |
| Files: use-after-free on folder navigation and Quick Access hit-test offset (from V4) | BUILT | |
| Shiro Brain: a local language model (llama.cpp on Genode) that answers questions about HermOS offline from a knowledge base | BUILT | Optional: `HERMOS_BRAIN_MODEL=<gguf> tools/build-hermos-v5.sh`. The image needs 2 GB RAM or more with it. Without a model the image is unchanged. |
| Shiro Brain knowledge base: 203 facts built from the status table, the design docs, curated facts and the component headers; retrieval (BM25 with stopwords and plurals) feeds the facts to the model | VERIFIED | Built into the image by `tools/shiro-brain/build_kb.py`. Updating the docs updates what it knows. |
| Shiro-Mini (the shipped model): 4.2 million parameters, trained from scratch here to answer from the retrieved facts; answers are copied from the facts, so they cannot be garbled | VERIFIED | Scores 27/40 on loosely worded questions it was not trained on (the first stand-in scored 14/40). It does not reason; a wrong fact can be chosen; a question the facts do not cover may get a loosely related fact. Replaceable by a larger base model without other changes. |
| Component proposals: the brain writes a minimal component, the Forge checks it (paths, files, manifest, libraries, banned calls, the word window), the name comes from the request | VERIFIED | Boot-tested: "Create a component named stopwatch." gives three files that pass. It writes a skeleton only, and nothing is stored: no Vault, no approval step yet. |
| Agent HTTP status page on port 80 | BUILT | Listens (logged). The router has no forward for it, so it is reachable only from inside. |
| Hermes Core Director, workers, procedure registry | SOURCE ONLY | Part of `hermos_desktop.run`, not the V5 image. |
| Nitpicker shell, installer UI/executor, test clients | SOURCE ONLY | Same. |
| `agents_sidebar`, `bottom_dock` | BUILT | Run, but superseded by the scene; vestigial. |
| Rust `hermes_agent` | SOURCE ONLY | Replaced by the C++ agent. |

## 6. Installation and storage

| Function | State | Notes |
|---|---|---|
| Installer from the live ISO (disk list, size, confirm, progress, result) | VERIFIED | SATA and NVMe. |
| Disk discovery from driver reports; never opens a missing device | VERIFIED | Removed a freeze. |
| Install: copy boot partitions byte for byte | VERIFIED | Compared to the payload on the host. |
| Install: GPT written last, old table wiped first | VERIFIED | `sgdisk` clean. |
| Install: `HERMOS-DATA` ext2 partition (1 MiB aligned) | VERIFIED | `e2fsck` clean, 1 GB to 512 GB disks. |
| Install: read-back verification | VERIFIED | It caught an NVMe truncation bug. |
| Refuses an image without a real payload | VERIFIED | |
| Installer releases the mounted `/home` first | VERIFIED | Data services stop, install runs, `/home` is restored if the install fails. Tested with `/home` mounted: install verified, other data disk byte-identical. |
| Reboot after install | BUILT | |
| Use of the data partition | PARTIAL | Terminal `/home` is mounted from it (tested live, QEMU). Files app and an installed-disk boot test are still to do. |
| Persistent user files, settings, installed apps | ABSENT | The installed OS runs from RAM like the live one. |
| Updates, rollback, recovery | ABSENT | The Updates window is a mock-up. |
| 4K-sector disks | ABSENT | Refused with a message. |
| Dual boot, resize, existing-data preservation | ABSENT | The installer erases the chosen disk. |
| Encryption, secure erase | ABSENT | |

## 7. Security model

| Function | State | Notes |
|---|---|---|
| Memory: reserved quotas sized to real use (top-level reservations about 0.6 GB; measured use about 0.2-0.27 GB) | VERIFIED | Booted, with Terminal, Files and the data partition working, at 1024 MB and at 768 MB of RAM. The `apps` sandbox is 256 MB (was 640). Quotas are reservations: this is what leaves room for a large app on a 2 GB PC. |
| Genode capability isolation between components | VERIFIED | Each component only gets routed services. |
| Gui framebuffers charged to the client's session quota; mode clamped to 8192×8192; buffer and mode change together | VERIFIED | Clients upgrade their quota (`Gui::Connection::buffer`); the server passes upgrades to the session. |
| Cell placement and sidebar privilege taken only from the child name the desktop controls | VERIFIED | `term<N>` / `files<N>` / `app<N>` must match the whole name. |
| Hermes director snapshots the shared request before validating it | BUILT | The director is not part of the V5 image. |
| Agent HTTP page: time-limited reads, control characters stripped from the log, lists only what runs | BUILT | |
| Explicit approval of consequential agent actions | ABSENT | |
| Audit log | ABSENT | Only the serial/kernel log. |
| Users, passwords, permissions | ABSENT | |

## 8. Known defects (found while preparing this list)

1. **Apps open in the wrong window.** The desktop picks a slot; the Gui server
   assigns its own cell number in connection order, and the two disagree. Files
   drew over the System window and left empty frames. (Being fixed.)
2. "Hermes Agent: Ready" does not reflect the agent's real state.
3. `hermes_files` and `desktop_builder` needed `exec_static_constructors()`
   (fixed for both); other components were not checked for the same fault.
4. ~~An API-key-looking string is committed in a run script.~~ Removed from the tree; it remains in git history, so the key must be rotated.
