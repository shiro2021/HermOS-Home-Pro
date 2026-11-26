# HermOS V8

`HermOS-V8-installer.iso` - UEFI x86_64 installer ISO (Genode 26.08 / NOVA), with the Shiro Brain (a local
language model that answers questions about HermOS offline). Verify with `sha256sum -c SHA256SUMS`.

Needs **2 GB of RAM or more** (the Shiro Brain holds its model in RAM) and, for the install test, a blank
disk of at least 16 GB. The image carries no cloud AI key: build with `HERMOS_MYKEE_KEY_FILE=<file>` to add one.

    qemu-system-x86_64 -machine q35 -m 2048 -smp 2 -device qemu-xhci -device usb-kbd -device usb-tablet \
      -drive if=pflash,format=raw,unit=0,readonly=on,file=OVMF_CODE.fd -drive if=pflash,format=raw,unit=1,file=vars.fd \
      -device ahci,id=ahci -drive id=t,file=target.img,format=raw,if=none -device ide-hd,drive=t,bus=ahci.0 \
      -drive id=cd,file=HermOS-V8-installer.iso,if=none,media=cdrom,readonly=on -device ide-cd,drive=cd,bus=ahci.1 -boot d

Try the command bar (click it, type, Enter): "What does Meta+K do?", "How do I hide the right pane?",
"Create a component named stopwatch." Without KVM an answer takes one to three minutes; it is a software
renderer and an emulated CPU.

Not in this build: reasoning (the model answers from a knowledge base and does not reason), storing or approving
generated components (the Forge checks them; nothing is kept), a HermOS boot screen, real-hardware testing.
