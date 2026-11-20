#!/usr/bin/env bash
#
# Turn the stock Genode ISO (BIOS El Torito only) into a hybrid ISO that also
# boots under UEFI. The UEFI entry is a small FAT image holding a standalone
# GRUB that finds the ISO and reads /boot/grub/grub.cfg from it.
#
#   make-hybrid-iso.sh <stock.iso> <out.iso>
#
# needs: grub-efi-amd64-bin grub-pc-bin mtools dosfstools xorriso
# Note: hermos_fb reads a UEFI framebuffer, so the desktop needs UEFI boot.
set -euo pipefail
[[ $# -eq 2 ]] || { echo "usage: $0 <stock.iso> <out.iso>" >&2; exit 2; }
here=$(cd -- "$(dirname -- "$0")" && pwd)
stock=$1; out=$2
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

xorriso -osirrox on -indev "$stock" -extract / "$tmp/tree" >/dev/null 2>&1
chmod -R u+w "$tmp/tree"; rm -f "$tmp/tree/boot.catalog"
cp "$here/grub.cfg" "$tmp/tree/boot/grub/grub.cfg"

mkdir -p "$tmp/efi/EFI/BOOT"
grub-mkstandalone -O x86_64-efi -o "$tmp/efi/EFI/BOOT/BOOTX64.EFI" \
  --modules="part_gpt part_msdos iso9660 fat search search_fs_file multiboot2 gzio normal configfile efi_gop efi_uga all_video gfxterm font png echo videoinfo sleep test video video_fb" \
  "boot/grub/grub.cfg=$here/efi-embed.cfg"
truncate -s 8M "$tmp/efi.img"; mkfs.vfat -n HERMOSEFI "$tmp/efi.img" >/dev/null
mmd -i "$tmp/efi.img" ::/EFI ::/EFI/BOOT
mcopy -i "$tmp/efi.img" "$tmp/efi/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
cp "$tmp/efi.img" "$tmp/tree/boot/efi.img"

rm -f "$out"
xorriso -as mkisofs -r -J -V HERMOS_V5BETA -o "$out" \
  -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table \
  -eltorito-alt-boot -e boot/efi.img -no-emul-boot -isohybrid-gpt-basdat "$tmp/tree" >/dev/null 2>&1
xorriso -indev "$out" -report_el_torito plain 2>&1 | grep -E "El Torito (boot|img path)"
