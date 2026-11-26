# HermOS V7

`HermOS-V7-installer.iso` - UEFI x86_64 installer ISO (Genode 26.08 / NOVA), built from the commit this
branch points at. Verify with `sha256sum -c SHA256SUMS`.

Test in QEMU (OVMF) with a blank target disk of at least 16 GB:

    qemu-system-x86_64 -machine q35 -m 3072 -smp 2 -device qemu-xhci -device usb-kbd -device usb-tablet \
      -drive if=pflash,format=raw,unit=0,readonly=on,file=OVMF_CODE.fd -drive if=pflash,format=raw,unit=1,file=vars.fd \
      -device ahci,id=ahci -drive id=t,file=target.img,format=raw,if=none -device ide-hd,drive=t,bus=ahci.0 \
      -drive id=cd,file=HermOS-V7-installer.iso,if=none,media=cdrom,readonly=on -device ide-cd,drive=cd,bus=ahci.1 -boot d

Installing erases the selected disk. Includes the V4 work (MyKee router, Shiro Local Brain, Capability Broker, Kill Mode Meta+K). The image carries no AI key: build with `HERMOS_MYKEE_KEY_FILE=<file>` to enable the AI.

Known gaps: no HermOS boot screen, installer still the flat style,
rail tiles do not switch yet, rail icons are generic.
