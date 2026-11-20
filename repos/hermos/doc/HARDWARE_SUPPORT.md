# HermOS hardware support evidence

This inventory reflects read-only host enumeration on 2026-10-11 and the
checked-out Genode 26.08 source. Hardware identification is not proof that a
HermOS driver has initialized it. Bare-metal HermOS boot has not been run.

## Target devices observed

| Device | PCI/USB ID | Current HermOS composition | Evidence-based next check |
| --- | --- | --- | --- |
| Intel Alder Lake-N UHD Graphics | PCI `8086:46d1` | Boot framebuffer only; `driver/framebuffer/boot` is wired. No Intel modesetting driver is started. | Genode `repos/pc/run/intel_fb.run` and `driver/framebuffer/intel/pc`; verify this device's generation and outputs on a disposable boot. |
| Intel Alder Lake-N HDA controller | PCI `8086:54c8` | No audio driver is started in `hermos_desktop.run`. | Genode `repos/pc/run/pc_audio.run` contains an Alder Lake SOF profile (`u7412`); integrate and verify playback/capture with the required firmware. |
| Intel xHCI controller | PCI `8086:54ed` | Generic USB host and USB HID are in the managed driver composition. | Verify physical enumeration and hotplug; QEMU USB keyboard/tablet is already proven on the prototype. |
| SATA/AHCI controller | PCI `8086:54d3` | AHCI driver is present; policy currently exposes configured `hermos_` disks read-only. | Keep disk writes unavailable until the isolated installer executor and confirmation flow exist. |
| Internal wireless adapter, Realtek RTL8821CE | PCI `10ec:c821` | No matching wireless driver is wired by HermOS. `repos/pc/run/pc_wifi.run` uses the Genode `pc_wifi` implementation. | Confirm device support from the actual driver source before selecting a driver; do not infer it from generic Linux support. |
| USB wireless adapter, Realtek RTL8188EUS | USB `2357:010c` | No USB wireless driver is wired. | Check exact chipset/USB ID against available Genode driver source; no support claim yet. |
| USB keyboard, Logitech K120 | USB `046d:c31c` | USB HID driver is present; physical input is not yet tested under HermOS. | Bare-metal keyboard test after working framebuffer/input boot. |
| USB mouse, Pixart | USB `093a:2510` | USB HID driver is present; physical input is not yet tested under HermOS. | Bare-metal pointer test after working framebuffer/input boot. |
| Bluetooth radio | USB `0bda:c821` | No Bluetooth stack or device route is configured. | Not supported by the current image. |
| SD reader | USB `0bda:0129` | No removable-storage UI or policy is configured. | Not supported by the current image. |
| Webcam | USB `927b:bc13` | No camera driver/service is configured. | Not supported by the current image. |

The desktop battery/Wi-Fi/audio glyphs are visual placeholders; they do not
currently read device state. Replace these with live service status before
calling those peripherals functional.

## Current platform gates

* **QEMU UEFI graphics/input:** live 1024x768 desktop boot is verified;
  QEMU USB keyboard/tablet input was exercised in the preceding prototype.
* **Bare-metal graphics, keyboard, and pointer:** not tested.
* **Storage read access:** AHCI/NVMe components are present in the prototype
  composition; actual laptop devices have not been exercised by HermOS.
* **Wi-Fi, Bluetooth, audio, webcam, SD reader, and power management:** not
  implemented or not tested. The UI must not report them as active.
* **Disk installation:** the integrated image contains no graphical installer
  or authorized storage-write executor. Destructive work remains restricted
  to disposable QEMU images.

Raw host enumeration used `lspci -nn`, `lsusb`, and `ip -br link`. Source
references above are in this same Genode 26.08 checkout; source presence alone
does not establish runtime support.
