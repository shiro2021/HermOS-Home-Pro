#!/usr/bin/env bash
# Boot a HermOS image headless and drive it with QMP (click/type/screenshot).
#
# Usage: run-hermos-interactive.sh <image> <out-prefix> <steps-json> [total-seconds]
set -euo pipefail

image=${1:?usage: $0 <image> <out-prefix> <steps-json> [seconds]}
prefix=${2:?usage: $0 <image> <out-prefix> <steps-json> [seconds]}
steps=${3:?usage: $0 <image> <out-prefix> <steps-json> [seconds]}
total=${4:-120}

tools_dir=$(cd -- "$(dirname -- "$0")" && pwd)
ovmf_code=${HERMOS_OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
ovmf_vars_template=${HERMOS_OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/hermos-drive.XXXXXX")
cp -- "$ovmf_vars_template" "$tmp/vars.fd"
qmp="$tmp/qmp.sock"
serial="$prefix.serial.log"
mkdir -p "$(dirname -- "$prefix")"

accel=tcg
[[ -r /dev/kvm && -w /dev/kvm ]] && accel=kvm

media=()
case $image in
  *.iso) media=(-cdrom "$image" -boot d) ;;
  *.img) media=(-snapshot -drive "file=$image,format=raw,if=none,id=disk0" -device ide-hd,drive=disk0) ;;
  *) echo "unsupported image type: $image" >&2; exit 1 ;;
esac

qemu-system-x86_64 \
  -accel "$accel" -machine q35 -cpu Skylake-Client -m 1024 -smp 2 \
  -device qemu-xhci -device usb-kbd -device usb-tablet -vga std \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$ovmf_code" \
  -drive "if=pflash,format=raw,unit=1,file=$tmp/vars.fd" \
  "${media[@]}" -net nic,model=e1000,netdev=net0 -netdev user,id=net0 -display none \
  -serial "file:$serial" \
  -qmp "unix:$qmp,server,nowait" &
qemu_pid=$!

cleanup() { kill "$qemu_pid" 2>/dev/null || true; wait "$qemu_pid" 2>/dev/null || true; rm -rf -- "$tmp"; }
trap cleanup EXIT

timeout "$total" python3 "$tools_dir/qmp-drive.py" "$qmp" "$prefix" "$steps" || true

echo "serial log: $serial"
