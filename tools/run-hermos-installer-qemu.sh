#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
iso="$project_dir/HermOS_V0.13.iso"
ovmf_code=${HERMOS_OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
ovmf_vars_template=${HERMOS_OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "qemu-system-x86_64 is not installed or not on PATH." >&2
  exit 1
fi
for required in "$iso" "$ovmf_code" "$ovmf_vars_template"; do
  if [[ ! -e $required ]]; then
    echo "Required file not found: $required" >&2
    exit 1
  fi
done

if [[ -r /dev/kvm && -w /dev/kvm ]]; then accel=kvm; else accel=tcg; fi
vars=$(mktemp "${TMPDIR:-/tmp}/hermos-installer-vars.XXXXXX")
target=$(mktemp "${TMPDIR:-/tmp}/hermos-installer-target.XXXXXX")
trap 'rm -f -- "$vars" "$target"' EXIT
cp -- "$ovmf_vars_template" "$vars"
truncate -s 64M "$target"

cat <<'EOF'
HermOS graphical installer demo is attached to a disposable 64 MiB QEMU disk.
The installer erases only that temporary disk. Closing QEMU removes the demo disk.
EOF
qemu-system-x86_64 \
  -accel "$accel" \
  -display gtk \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$ovmf_code" \
  -drive "if=pflash,format=raw,unit=1,file=$vars" \
  -machine q35 \
  -cpu Skylake-Client \
  -m 1024 \
  -device qemu-xhci \
  -device usb-kbd \
  -device usb-tablet \
  -device ahci,id=ahci \
  -drive "id=hermos-install-target,file=$target,format=raw,if=none" \
  -device ide-hd,drive=hermos-install-target,bus=ahci.0 \
  -cdrom "$iso" \
  -boot d \
  -net none
