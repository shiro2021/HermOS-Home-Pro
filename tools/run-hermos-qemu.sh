#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
iso="$project_dir/HermOS_V0.12.iso"
ovmf_code=${HERMOS_OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
ovmf_vars_template=${HERMOS_OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "qemu-system-x86_64 is not installed or not on PATH." >&2
  exit 1
fi

for required in "$iso" "$ovmf_code" "$ovmf_vars_template"; do
  if [[ ! -e $required ]]; then
    echo "Required file or command not found: $required" >&2
    exit 1
  fi
done

if [[ -r /dev/kvm && -w /dev/kvm ]]; then
  accel=kvm
else
  accel=tcg
fi

vars=$(mktemp "${TMPDIR:-/tmp}/hermos-ovmf-vars.XXXXXX")
trap 'rm -f -- "$vars"' EXIT
cp -- "$ovmf_vars_template" "$vars"

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
  -cdrom "$iso" \
  -boot d \
  -net none
