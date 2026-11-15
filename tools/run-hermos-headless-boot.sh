#!/usr/bin/env bash
# Headless boot harness for HermOS images.
#
# Boots an ISO or disk image under OVMF (UEFI) with no visible window,
# captures the serial console to a log file and screenshots the guest
# framebuffer over QMP. This never attaches a physical block device: only
# the file passed by the caller is attached, read-only for ISO media.
#
# Usage: run-hermos-headless-boot.sh <image> <out-prefix> [seconds]
#   image       : .iso (booted as CD-ROM) or .img (booted as a disk)
#   out-prefix  : path prefix; writes <prefix>.serial.log and <prefix>.png
#   seconds     : wait before the screenshot (default 45)
set -euo pipefail

image=${1:?usage: $0 <image> <out-prefix> [seconds]}
prefix=${2:?usage: $0 <image> <out-prefix> [seconds]}
wait_s=${3:-45}

ovmf_code=${HERMOS_OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
ovmf_vars_template=${HERMOS_OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}
[[ -e $image ]] || { echo "no such image: $image" >&2; exit 1; }
[[ -e $ovmf_code && -e $ovmf_vars_template ]] || { echo "OVMF firmware not found" >&2; exit 1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/hermos-boot.XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
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
  -accel "$accel" \
  -machine q35 \
  -cpu Skylake-Client \
  -m 1024 \
  -smp 2 \
  -device qemu-xhci -device usb-kbd -device usb-tablet \
  -vga std \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$ovmf_code" \
  -drive "if=pflash,format=raw,unit=1,file=$tmp/vars.fd" \
  "${media[@]}" \
  -net nic,model=e1000,netdev=net0 -netdev user,id=net0 -display none \
  -serial "file:$serial" \
  -qmp "unix:$qmp,server,nowait" &
qemu_pid=$!

cleanup() {
  kill "$qemu_pid" 2>/dev/null || true
  wait "$qemu_pid" 2>/dev/null || true
}
trap 'cleanup; rm -rf -- "$tmp"' EXIT

python3 - "$qmp" "$prefix.png" "$wait_s" <<'PY'
import json, os, socket, sys, time

sock_path, png_path, wait_s = sys.argv[1], sys.argv[2], float(sys.argv[3])

# Wait for the QMP socket, then perform the greeting/capabilities dance.
deadline = time.time() + 30
s = None
while time.time() < deadline:
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sock_path)
        break
    except OSError:
        s = None
        time.sleep(0.2)
if s is None:
    sys.exit("QMP socket never appeared")

f = s.makefile("rwb", buffering=0)
json.loads(f.readline())                      # greeting
f.write(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
json.loads(f.readline())

def cmd(payload):
    f.write(json.dumps(payload).encode() + b"\n")
    while True:
        line = f.readline()
        if not line:
            return None
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        if "return" in msg or "error" in msg:
            return msg

time.sleep(wait_s)
status = cmd({"execute": "query-status"})
res = cmd({"execute": "screendump", "arguments": {"filename": png_path}})
print("query-status:", status)
print("screendump:", res)
print("screenshot exists:", os.path.exists(png_path), os.path.getsize(png_path) if os.path.exists(png_path) else 0)
PY

echo "serial log: $serial"
echo "screenshot: $prefix.png"
