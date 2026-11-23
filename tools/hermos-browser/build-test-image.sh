#!/usr/bin/env bash
# Build a HermOS disk image that includes the HermOS Browser, for bring-up in QEMU (UEFI).
#   tools/hermos-browser/build-test-image.sh MODULES_DIR OUT.img
# MODULES_DIR comes from assemble-modules.sh. The normal HermOS ISO does not contain the browser.
set -euo pipefail
mods=$(cd "${1:?modules dir}" && pwd); out=${2:?output image}
genode=${HERMOS_GENODE_DIR:?set HERMOS_GENODE_DIR}
export CROSS_DEV_PREFIX=${CROSS_DEV_PREFIX:-/usr/local/genode/tool/current/bin/genode-x86-}
export PATH=$(dirname "$CROSS_DEV_PREFIX"):$PATH
build=$genode/build/x86_64
log=$(mktemp)
if ! make -C "$build" run/hermos_monolith KERNEL=nova BOARD=pc -j"$(nproc)" \
     RUN_OPT="--include image/disk --include boot_dir/nova --hermos-build-only yes --hermos-browser-dir $mods" >"$log" 2>&1; then
	tail -40 "$log" >&2; echo "BUILD FAILED" >&2; exit 1
fi
cp "$build/var/run/hermos_monolith.img" "$out"
ls -la "$out" | awk '{print $5, $9}'
