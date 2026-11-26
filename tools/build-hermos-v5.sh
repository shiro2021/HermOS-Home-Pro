#!/usr/bin/env bash
#
# Build the HermOS V5 installer ISO and the installed-system disk image.
#
#   tools/build-hermos-v5.sh OUTDIR
#
# 1. system image : GPT/EFI disk image of the desktop composition (image/disk).
#                   This is what ends up on the target disk after installation.
# 2. installer ISO: the same composition with (1) embedded as the installer
#                   payload, made bootable under BIOS and UEFI.
#
# Environment: HERMOS_GENODE_DIR (Genode 26.08 tree with repos/hermos linked in,
# see repos/hermos/doc/BUILD.md) and CROSS_DEV_PREFIX.
set -euo pipefail
[[ $# -eq 1 ]] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
out=$(mkdir -p "$1" && cd "$1" && pwd)
here=$(cd -- "$(dirname -- "$0")" && pwd)
genode=${HERMOS_GENODE_DIR:?set HERMOS_GENODE_DIR to the Genode 26.08 tree}
export CROSS_DEV_PREFIX=${CROSS_DEV_PREFIX:-/usr/local/genode/tool/current/bin/genode-x86-}
export PATH=$(dirname "$CROSS_DEV_PREFIX"):$PATH
build=$genode/build/x86_64
brain_opt=""
[[ -n ${HERMOS_BRAIN_MODEL:-} ]] && brain_opt="--hermos-brain-model $HERMOS_BRAIN_MODEL"
run=(make -C "$build" run/hermos_monolith KERNEL=nova BOARD=pc -j"$(nproc)")

echo "== 1/3 system disk image"
"${run[@]}" RUN_OPT="--include image/disk --include boot_dir/nova --hermos-build-only yes $brain_opt" \
  > "$out/build-system-image.log" 2>&1
cp "$build/var/run/hermos_monolith.img" "$out/HermOS-system.img"
sgdisk -p "$out/HermOS-system.img" | tail -4

echo "== 2/3 installer ISO (payload = system image)"
"${run[@]}" RUN_OPT="--include image/iso --include boot_dir/nova --hermos-build-only yes --hermos-system-image $out/HermOS-system.img $brain_opt" \
  > "$out/build-installer-iso.log" 2>&1

echo "== 3/3 hybrid BIOS/UEFI ISO"
"$here/../repos/hermos/tools/iso/make-hybrid-iso.sh" "$build/var/run/hermos_monolith.iso" "$out/HermOS-installer.iso"
(cd "$out" && sha256sum HermOS-installer.iso HermOS-system.img > SHA256SUMS && cat SHA256SUMS)
