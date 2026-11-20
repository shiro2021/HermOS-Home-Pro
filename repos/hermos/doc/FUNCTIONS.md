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
| Web browser, media player, office, settings app | ABSENT | |

## 5. Hermes agent and network services

| Function | State | Notes |
|---|---|---|
| Network stack (NIC → NAT router → agent, 10.0.3.0/24) | VERIFIED | DHCP lease and agent interface up in QEMU. |
| Hermes agent: prompt → DeepSeek chat API → reply panel | PARTIAL | Verified: search field → agent → outgoing request → reply panel. The test host has no DNS, so the reply was "curl failed: Couldn't resolve host name". A real answer is untested. |
| MyKee key handling | BUILT | **A key string is embedded in `run/hermos_monolith.run`; rotate it if it is real.** |
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
| Genode capability isolation between components | VERIFIED | Each component only gets routed services. |
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
4. An API-key-looking string is committed in a run script.
