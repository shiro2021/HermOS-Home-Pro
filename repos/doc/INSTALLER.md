# HermOS disk installation

## Sculpt OS installation model

Sculpt OS 26.04 does not ship a separate graphical whole-disk installer. Its
documented PC installation procedure writes the verified hybrid system image
to a whole SATA/NVMe disk from a live Linux environment. After boot, Sculpt's
AHCI/NVMe dialogs manage and expand its storage partition. HermOS follows the
image-write step with [`tools/install-hermos-to-disk.sh`](../tools/install-hermos-to-disk.sh).

## Write the HermOS boot image

1. Obtain the HermOS Genode GPT/EFI disk image and verify its SHA-256. The V8
   prototype output is
   `/home/shiro/HermOS/HermOS Versions/HermOS-Genode-26.08-V8-Aladar-System-Prototype.img`.
2. Boot a separate live Linux environment. Do not run the installer from the
   HermOS disk that will be replaced.
3. Identify the whole target disk with `lsblk`. Use a dedicated disk; the
   target must not contain the running system or mounted filesystems.
4. Run the installer with the image path and whole-disk path. It prints the
   model, capacity, and image digest, then requires the exact target path to be
   typed before writing. It reads back and hashes the image after the write.

```sh
sudo repos/hermos/tools/install-hermos-to-disk.sh \
  HermOS-Genode-26.08-V8-Aladar-System-Prototype.img /dev/nvme1n1
```

The write replaces the target's partition table and data. The script refuses
partitions, undersized disks, the disk backing `/`, and any target with a
mounted filesystem. It does not unmount devices automatically.

## Current limits

The Genode `image/disk` output is a GPT/EFI disk image with a GRUB boot
partition and a Genode system partition. It was copied to a same-size
disposable QEMU disk file, read back byte-for-byte, and booted without ISO
media. The guarded host-side writer can write that image to a whole disk but
has not been run against a physical disk. It does not resize the Genode
partition, create persistent user storage, or offer dual-boot. The integrated
HermOS GUI wizard, target-selection view, and final confirmation screen remain
unimplemented. The in-system write executor is present as a prototype service;
the test-only client is not included in the product ISO.

On 2026-10-11, the executor was run in a separate QEMU test ISO against a
64 MiB disposable AHCI disk. Its client had to send the explicit
`CONFIRM_ERASE` RPC before the executor wrote the 7,979,008-byte system image.
It then read the same range back and verified every byte. The run also
enumerated QEMU USB HID keyboard and pointer devices. Serial evidence is in
`artifacts/logs/hermos-v8-installer-qemu.serial.log`. This proves the executor
path in QEMU only; it does not prove a graphical installer, physical disk
safety, or bare-metal hardware support.

The separately built installer prototype ISO is
`/home/shiro/HermOS/HermOS Versions/HermOS-Genode-26.08-V8-Aladar-Installer-Prototype.iso`
has final-build SHA-256
`dd76885c6c37ffdbf8fcf21ae3a21ea75c95f7ef6b038c847762b353a13691f5`. It
contains the executor but has not been runtime-booted since this build. An
attempt to expose the nested installer session to a top-level client stalled;
the working QEMU test client therefore remains beside the executor inside
the test-only driver subsystem. Bridge failure evidence is in
`artifacts/logs/hermos-v8-installer-service-bridge-failed.serial.log`. The
product ISO is not a complete end-user installer and should not be used to
install onto a physical disk.

The runtime image includes the Genode PC USB HID, AHCI, and NVMe drivers. The
image writer does not imply that every laptop peripheral is supported; see
[`HARDWARE_SUPPORT.md`](HARDWARE_SUPPORT.md) for observed devices and gaps.
