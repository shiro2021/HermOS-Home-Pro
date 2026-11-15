#!/usr/bin/env bash
set -euo pipefail

usage() { echo "Usage: $0 VERSION (for example: 0.13)" >&2; }
if [[ $# -ne 1 || ! $1 =~ ^[0-9]+\.[0-9]+$ ]]; then usage; exit 2; fi
version=$1
project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
genode_dir=${HERMOS_GENODE_DIR:-"$project_dir/worktrees/HermOS-readiness"}
build_dir="$genode_dir/build/x86_64"
release_iso="$project_dir/HermOS_V${version}.iso"
release_img="$project_dir/HermOS_V${version}.img"
manifest="$project_dir/HermOS_V${version}.sha256"
log_dir="$project_dir/artifacts/logs"
mkdir -p "$log_dir"

for output in "$release_iso" "$manifest"; do
  if [[ -e $output ]]; then
    echo "Refusing to overwrite existing release file: $output" >&2
    exit 1
  fi
done
if [[ ! -x "$genode_dir/toolchain/bin/genode-x86-g++" ]]; then
  echo "Genode build tree/toolchain not found: $genode_dir" >&2
  exit 1
fi

export CROSS_DEV_PREFIX="$genode_dir/toolchain/bin/genode-x86-"
common=(KERNEL=nova BOARD=pc)

# First create the installed system image if no preserved system image exists.
# It boots HermOS with Hermes Core and the interactive Bash/coreutils terminal.
if [[ ! -e $release_img ]]; then
  make -C "$build_dir" run/hermos_desktop "${common[@]}" \
    RUN_OPT='--include image/disk --include image/hermos_disk --include boot_dir/nova --hermos-build-only yes' \
    2>&1 | tee "$log_dir/HermOS_V${version}-system-image-build.log"
  [[ -s "$build_dir/var/run/hermos_desktop.img" ]]
  grep -q 'HermOS: Bender serial_fallback enabled in [1-9]' \
    "$log_dir/HermOS_V${version}-system-image-build.log" \
    || { echo "serial_fallback was not applied to the system image" >&2; exit 1; }
  cp -- "$build_dir/var/run/hermos_desktop.img" "$release_img"
fi

# Then build the GUI installer ISO with that exact image embedded as a ROM.
installer_run_opt="--include image/iso --include image/hermos_iso --include boot_dir/nova --hermos-installer-mode yes --hermos-system-image $release_img --hermos-version $version --hermos-build-only yes"
make -C "$build_dir" run/hermos_desktop "${common[@]}" \
  RUN_OPT="$installer_run_opt" \
  2>&1 | tee "$log_dir/HermOS_V${version}-installer-iso-build.log"
[[ -s "$build_dir/var/run/hermos_desktop.iso" ]]

# Both images boot via UEFI; without Bender's serial_fallback the serial log
# carries no Genode output. Fail the build if the fix did not reach grub.cfg.
grep -q 'HermOS: Bender serial_fallback enabled in [1-9]' \
  "$log_dir/HermOS_V${version}-installer-iso-build.log" \
  || { echo "serial_fallback was not applied to the installer ISO" >&2; exit 1; }
cp -- "$build_dir/var/run/hermos_desktop.iso" "$release_iso"

chmod a-w "$release_iso" "$release_img"
file "$release_iso" "$release_img"
xorriso -indev "$release_iso" -report_el_torito as_mkisofs
sgdisk -p "$release_img"
(cd "$project_dir" && sha256sum "$(basename "$release_iso")" "$(basename "$release_img")" > "$(basename "$manifest")")
cat "$manifest"
echo "Build complete. Test the ISO in VirtualBox 7 on Genode: tools/build-hermos-vbox-host.sh $version. No physical disk is written by this script."
