#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 VERSION (for example: 0.4)" >&2
}

if [[ $# -ne 1 || ! $1 =~ ^[0-9]+\.[0-9]+$ ]]; then usage; exit 2; fi
version=$1
project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
genode_dir=${HERMOS_GENODE_DIR:-"$project_dir/worktrees/HermOS-readiness"}
build_dir="$genode_dir/build/x86_64"
release_iso="$project_dir/HermOS_V${version}.iso"
release_img="$project_dir/HermOS_V${version}.img"
manifest="$project_dir/HermOS_V${version}.sha256"

for output in "$release_iso" "$release_img" "$manifest"; do
  if [[ -e $output ]]; then
    echo "Refusing to overwrite existing release file: $output" >&2
    exit 1
  fi
done
if [[ ! -x "$genode_dir/toolchain/bin/genode-x86-g++" ]]; then
  echo "Genode build tree/toolchain not found: $genode_dir" >&2
  echo "Set HERMOS_GENODE_DIR to a prepared Genode 26.08 checkout." >&2
  exit 1
fi

export CROSS_DEV_PREFIX="$genode_dir/toolchain/bin/genode-x86-"
common=(KERNEL=nova BOARD=pc)

# Build the standalone GPT/EFI install image first. Hermes starts as part of
# the HermOS composition encoded in this image.
make -C "$build_dir" run/hermos_desktop "${common[@]}" \
  RUN_OPT='--include image/disk --include boot_dir/nova --hermos-build-only yes' \
  QEMU_RUN_OPT='--include image/disk --include boot_dir/nova'
[[ -s "$build_dir/var/run/hermos_desktop.img" ]]
cp -- "$build_dir/var/run/hermos_desktop.img" "$release_img"

# Build the live, hybrid BIOS/UEFI ISO from the same source tree.
make -C "$build_dir" run/hermos_desktop "${common[@]}" \
  RUN_OPT='--include image/iso --include image/hermos_iso --include boot_dir/nova --hermos-build-only yes' \
  QEMU_RUN_OPT='--include image/iso --include image/hermos_iso --include boot_dir/nova'
[[ -s "$build_dir/var/run/hermos_desktop.iso" ]]
cp -- "$build_dir/var/run/hermos_desktop.iso" "$release_iso"

chmod a-w "$release_iso" "$release_img"
file "$release_iso" "$release_img"
xorriso -indev "$release_iso" -report_el_torito as_mkisofs
sgdisk -p "$release_img"
(cd "$project_dir" && sha256sum "$(basename "$release_iso")" "$(basename "$release_img")" > "$(basename "$manifest")")
cat "$manifest"
echo "Release artifacts created. QEMU validation and physical-device checks are separate steps; this script never writes a physical disk."
