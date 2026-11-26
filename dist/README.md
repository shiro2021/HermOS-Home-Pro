# HermOS V9 Beta

ISO images are not kept in git. Download them from the
[Releases page](https://github.com/shiro2021/HermOS-Home-Pro/releases):

| File | What it is |
|---|---|
| `HermOS-V9-beta-installer.iso` | UEFI/BIOS x86_64 installer ISO (Genode 26.08 / NOVA) with the Shiro Brain |
| `SHA256SUMS` | Checksums. Verify with `sha256sum -c SHA256SUMS` |

V9 Beta has the same features as V8 (Shiro Brain, knowledge base, Forge) in a cleaned-up repository.
See [`RELEASE_NOTES_V9_BETA.md`](RELEASE_NOTES_V9_BETA.md) for what is in it and what is not.

Older images: V8 at tag `archive/V8` (its ISO is in git history at `dist/HermOS-V8-installer.iso`),
V0.22, V0.23 and V3 beta on the Releases page.

## Run it in QEMU

Needs **2 GB of RAM or more** (the Shiro Brain holds its model in RAM) and, for the install test, a blank
disk image of at least 16 GB (`truncate -s 16G target.img`). Never attach a physical disk.

    qemu-system-x86_64 -machine q35 -m 2048 -smp 2 -device qemu-xhci -device usb-kbd -device usb-tablet \
      -drive if=pflash,format=raw,unit=0,readonly=on,file=OVMF_CODE.fd -drive if=pflash,format=raw,unit=1,file=vars.fd \
      -device ahci,id=ahci -drive id=t,file=target.img,format=raw,if=none -device ide-hd,drive=t,bus=ahci.0 \
      -drive id=cd,file=HermOS-V9-beta-installer.iso,if=none,media=cdrom,readonly=on -device ide-cd,drive=cd,bus=ahci.1 -boot d

Add `-enable-kvm -cpu host` on a Linux host with KVM: without it an answer from the Shiro Brain takes one to three
minutes (software renderer and emulated CPU).

Try the command bar (click it, type, Enter): "What does Meta+K do?", "How do I hide the right pane?",
"Create a component named stopwatch."

## Build it

On the development laptop, from this repository:

    HERMOS_GENODE_DIR=<Genode 26.08 tree> HERMOS_BRAIN_MODEL=<shiro-mini.gguf> tools/build-hermos-v9-beta.sh

Output goes to `release/V9-beta/` (ISO, system image, `SHA256SUMS`, `BUILD-INFO.txt`, logs).
