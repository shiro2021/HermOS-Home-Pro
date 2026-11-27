#!/usr/bin/env bash
#
# Build the HermOS V9.x Beta release files (currently V9.01 Beta) on the development laptop.
#
#   HERMOS_GENODE_DIR=<Genode 26.08 tree> \
#   HERMOS_BRAIN_MODEL=<shiro-mini .gguf> \
#     tools/build-hermos-v9-beta.sh [OUTDIR]
#
# OUTDIR defaults to release/V9.01-beta. The script refuses to overwrite an
# existing OUTDIR, so every build is kept. It runs tools/build-hermos-v5.sh
# (the V8 build) and renames the results:
#
#   HermOS-V9.01-beta-installer.iso   UEFI/BIOS installer ISO  -> attach to the release
#   HermOS-V9.01-beta-system.img   installed-system disk image
#   SHA256SUMS                         checksums of both        -> attach to the release
#   build-*.log                        build logs
#
# The Shiro Brain is part of V9 Beta, as it was of V8, so HERMOS_BRAIN_MODEL
# is required. Set HERMOS_ALLOW_NO_BRAIN=1 to build without it; the release
# notes must then say so.
set -euo pipefail

here=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$here/.." && pwd)
ver=V9.01-beta
out=${1:-$repo/release/$ver}

: "${HERMOS_GENODE_DIR:?set HERMOS_GENODE_DIR to the Genode 26.08 tree (see repos/hermos/doc/BUILD.md)}"
if [[ -z ${HERMOS_BRAIN_MODEL:-} && ${HERMOS_ALLOW_NO_BRAIN:-0} != 1 ]]; then
	echo "error: HERMOS_BRAIN_MODEL is not set. V9 Beta ships the Shiro Brain;" >&2
	echo "       point it at the Shiro-Mini .gguf (tools/shiro-brain/train_shiro.py makes one)," >&2
	echo "       or set HERMOS_ALLOW_NO_BRAIN=1 for a build without it." >&2
	exit 2
fi
if [[ -n ${HERMOS_BRAIN_MODEL:-} && ! -f $HERMOS_BRAIN_MODEL ]]; then
	echo "error: HERMOS_BRAIN_MODEL=$HERMOS_BRAIN_MODEL does not exist" >&2
	exit 2
fi
if [[ -e $out ]]; then
	echo "error: $out already exists; remove it or pass another OUTDIR" >&2
	exit 2
fi

grep -q '"HermOS V9.01 Beta (Genode 26.08)"' "$repo/repos/hermos/src/app/desktop_builder/main.cc" || {
	echo "error: the desktop's version string is not 'HermOS V9.01 Beta'; wrong branch?" >&2
	exit 2
}

work=$(mktemp -d "${TMPDIR:-/tmp}/hermos-$ver.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$here/build-hermos-v5.sh" "$work"

mkdir -p "$out"
mv "$work/HermOS-installer.iso" "$out/HermOS-$ver-installer.iso"
mv "$work/HermOS-system.img"    "$out/HermOS-$ver-system.img"
cp "$work"/build-*.log "$out/"
(cd "$out" && sha256sum "HermOS-$ver-installer.iso" "HermOS-$ver-system.img" > SHA256SUMS)

{
	echo "commit:      $(git -C "$repo" rev-parse HEAD)"
	echo "dirty:       $(git -C "$repo" status --porcelain --untracked-files=no | wc -l) changed files"
	echo "genode tree: $HERMOS_GENODE_DIR ($(git -C "$HERMOS_GENODE_DIR" rev-parse HEAD 2>/dev/null || echo 'not a git tree'))"
	echo "brain model: ${HERMOS_BRAIN_MODEL:-none} $( [[ -n ${HERMOS_BRAIN_MODEL:-} ]] && sha256sum "$HERMOS_BRAIN_MODEL" | cut -d' ' -f1)"
	echo "built:       $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(uname -n)"
} > "$out/BUILD-INFO.txt"

echo
cat "$out/SHA256SUMS"
cat "$out/BUILD-INFO.txt"
echo
echo "Built. NOT boot-tested yet: boot the ISO (dist/README.md has the QEMU command) before publishing."
