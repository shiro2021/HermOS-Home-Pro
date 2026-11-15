#!/usr/bin/env bash
#
# Build the HermOS test host: VirtualBox 7 on Genode/NOVA, USB-bootable.
#
#   tools/build-hermos-vbox-host.sh VERSION      (e.g. 0.18)
#
# Embeds HermOS_V<VERSION>.iso as the VM's DVD and an empty RAM-backed VDI as
# the install target. Output: HermOS_VBoxHost_V<VERSION>.img (+ .sha256).
# Nothing is booted or written to any disk by this script; write the image to
# a dedicated USB stick with tools/install-hermos-to-disk.sh (USB-only).
#
set -euo pipefail

if [[ $# -ne 1 || ! $1 =~ ^[0-9]+\.[0-9]+$ ]]; then
  echo "Usage: $0 VERSION (for example: 0.18)" >&2; exit 2
fi
version=$1
project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
genode_dir=${HERMOS_GENODE_DIR:-"$project_dir/worktrees/HermOS-readiness"}
build_dir="$genode_dir/build/x86_64"
iso="$project_dir/HermOS_V${version}.iso"
out="$project_dir/HermOS_VBoxHost_V${version}.img"
manifest="$project_dir/HermOS_VBoxHost_V${version}.sha256"
log="$project_dir/artifacts/logs/HermOS_VBoxHost_V${version}-build.log"
mkdir -p "$(dirname -- "$log")"

[[ -f $iso ]] || { echo "Installer ISO not found: $iso (run build-hermos-installer-release.sh $version first)" >&2; exit 1; }
for f in "$out" "$manifest"; do
  [[ -e $f ]] && { echo "Refusing to overwrite existing file: $f" >&2; exit 1; }
done
[[ -x "$genode_dir/toolchain/bin/genode-x86-g++" ]] || { echo "Genode toolchain not found under $genode_dir" >&2; exit 1; }

export CROSS_DEV_PREFIX="$genode_dir/toolchain/bin/genode-x86-"

# --depot-auto-update builds missing depot archives (mesa, themed_wm, ...)
# from the prepared ports instead of failing; drop it to use downloaded ones.
make -C "$build_dir" run/hermos_vbox_host KERNEL=nova BOARD=pc \
  RUN_OPT="--include image/disk --include image/hermos_disk --include boot_dir/nova --depot-auto-update --hermos-iso $iso" \
  2>&1 | tee "$log"

grep -q 'HermOS: Bender serial_fallback enabled in [1-9]' "$log" \
  || { echo "serial_fallback was not applied to the host image" >&2; exit 1; }
grep -q 'hermos-target.vdi: VDI 1.1 .* -> OK' "$log" \
  || { echo "target VDI was not created/validated" >&2; exit 1; }
[[ -s "$build_dir/var/run/hermos_vbox_host.img" ]]

cp -- "$build_dir/var/run/hermos_vbox_host.img" "$out"
chmod a-w "$out"
sgdisk -p "$out"
(cd "$project_dir" && sha256sum "$(basename "$out")" > "$(basename "$manifest")")
cat "$manifest"
echo "Host image built. Write it to a dedicated USB stick:"
echo "  sudo $project_dir/tools/install-hermos-to-disk.sh $out /dev/disk/by-id/usb-..."
