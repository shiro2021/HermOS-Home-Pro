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
| Agents & Workers panel: running apps with a close button, ▶ to launch | VERIFIED | Lists real apps only. |
| "Hermes Agent: Ready" status | BUILT | Always shows Ready; the agent's real state is not queried. |
| Windows: frame, close, minimise, restore from the taskbar | VERIFIED | |
| Window maximise (uses the Plasma-style client area) | BUILT | |
| Window drag, edge resize, snap to screen edges | BUILT | Legacy code, not re-tested after the scene rewrite. |
| Tiling and quick-tile (`Meta+T`, `Meta+arrows`, `Home/End/PgUp/PgDn`, `F`, `R`) | BUILT | Legacy code, untested. |
| Hosted app windows (apps render inside cells, receive input) | VERIFIED | Terminal. Known bug: see §8. |
| Theme (`theme.hid` → `theme_state`, 1/10000 geometry, panel modes) | VERIFIED | |
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
| Terminal storage | VERIFIED | `/tmp` and `/home` are RAM — **everything is lost at reboot**. |
| Terminal text editor (vim) | ABSENT | Not in this image. |
| Files (`hermos_files`): start, browse `/Home` | BUILT | Starts and scans its folder; see the window bug in §8. |
| Files: text editor, hex editor, image view, copy/move/delete/rename | BUILT | Code exists, untested. |
| Files: format converter | STUB | Codecs are placeholders. |
| Files: SSH/SFTP | STUB | Logs "stub". |
| Files: "Analyze" (AI parser hot-load) | STUB | |
| Web browser, media player, office, settings app | ABSENT | |

## 5. Hermes agent and network services

| Function | State | Notes |
|---|---|---|
| Network stack (NIC → NAT router → agent, 10.0.3.0/24) | VERIFIED | DHCP lease and agent interface up in QEMU. |
| Hermes agent: prompt → DeepSeek chat API → reply panel | BUILT | Needs an API key and Internet. Never run end to end. |
| MyKee key handling | BUILT | **A key string is embedded in `run/hermos_monolith.run`; rotate it if it is real.** |
| Agent HTTP status page on port 80 | BUILT | |
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
| Reboot after install | BUILT | |
| **Use of the data partition** | ABSENT | Created and empty; nothing mounts or uses it. |
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
