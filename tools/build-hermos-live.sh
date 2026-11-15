#!/usr/bin/env bash
#
# Build the HermOS live desktop as a USB-bootable disk image.
#
#   tools/build-hermos-live.sh VERSION      (e.g. 0.19)
#
# Output: HermOS_Live_V<VERSION>.img (+ .sha256) in the project folder.
# The live system contains no storage driver: it cannot read or write any
# disk of the machine it runs on, and nothing is saved across reboots.
# Write it to a dedicated USB stick with tools/install-hermos-to-disk.sh.
#
set -euo pipefail

if [[ $# -ne 1 || ! $1 =~ ^[0-9]+\.[0-9]+$ ]]; then
  echo "Usage: $0 VERSION (for example: 0.19)" >&2; exit 2
fi
version=$1
project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
genode_dir=${HERMOS_GENODE_DIR:-"$project_dir/worktrees/HermOS-readiness"}
build_dir="$genode_dir/build/x86_64"
out="$project_dir/HermOS_Live_V${version}.img"
manifest="$project_dir/HermOS_Live_V${version}.sha256"
log="$project_dir/artifacts/logs/HermOS_Live_V${version}-build.log"
mkdir -p "$(dirname -- "$log")"

for f in "$out" "$manifest"; do
  [[ -e $f ]] && { echo "Refusing to overwrite existing file: $f" >&2; exit 1; }
done
[[ -x "$genode_dir/toolchain/bin/genode-x86-g++" ]] || { echo "Genode toolchain not found under $genode_dir" >&2; exit 1; }

export CROSS_DEV_PREFIX="$genode_dir/toolchain/bin/genode-x86-"

make -C "$build_dir" run/hermos_desktop KERNEL=nova BOARD=pc \
  RUN_OPT="--include image/disk --include image/hermos_disk --include boot_dir/nova --hermos-version $version" \
  2>&1 | tee "$log"

grep -q 'HermOS: Bender serial_fallback enabled in [1-9]' "$log" \
  || { echo "serial_fallback was not applied to the live image" >&2; exit 1; }
[[ -s "$build_dir/var/run/hermos_desktop.img" ]]

# The live image must not contain storage drivers.
drivers_config="$build_dir/bin/hermos_drivers.config"
[[ -s $drivers_config ]] || { echo "missing $drivers_config" >&2; exit 1; }
if grep -q 'start ahci\|start nvme' "$drivers_config"; then
  echo "Live image unexpectedly contains a storage driver" >&2; exit 1
fi

cp -- "$build_dir/var/run/hermos_desktop.img" "$out"
chmod a-w "$out"
sgdisk -p "$out"
(cd "$project_dir" && sha256sum "$(basename "$out")" > "$(basename "$manifest")")
cat "$manifest"
echo "Live image built. Write it to a dedicated USB stick:"
echo "  sudo $project_dir/tools/install-hermos-to-disk.sh $out /dev/disk/by-id/usb-..."
