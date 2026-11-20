#!/usr/bin/env python3
"""Boot with DEBUG OVMF and dump the video mode-selection DEBUG trace."""
import shutil, subprocess, tempfile, time

OVMF_DEBUG = "/home/shiro/Downloads/ovmf-build/edk2/Build/OvmfX64/DEBUG_GCC5/FV"
tmp = tempfile.mkdtemp(prefix="ovmf-dbg-")
vars_fd = f"{tmp}/vars.fd"
shutil.copy(f"{OVMF_DEBUG}/OVMF_VARS.fd", vars_fd)

qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "std",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF_DEBUG}/OVMF_CODE.fd",
    "-drive", f"if=pflash,format=raw,unit=1,file={vars_fd}",
    "-cdrom", "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso", "-boot", "d",
    "-net", "none", "-display", "none",
    "-debugcon", f"file:{tmp}/debug.log", "-global", "isa-debugcon.iobase=0x402",
    "-serial", f"file:{tmp}/ser.log",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(32)
qemu.terminate()
try:
    qemu.wait(timeout=15)
except subprocess.TimeoutExpired:
    qemu.kill()

dbg = open(f"{tmp}/debug.log", "rb").read().decode("utf-8", "replace")
print("=== OVMF DEBUG (video/mode selection) ===")
for line in dbg.splitlines():
    if any(k in line for k in ["Bochs", "mmio bar", "port io", "Mode", "resolution",
                               "Resolution", "QemuVideo", "GraphicsConsole", "SetMode",
                               "Adding", "Skipping", "magic", "timing", "QemuVideoEdid"]):
        print(line.strip())

print("=== serial scanout ===")
ser = open(f"{tmp}/ser.log", "rb").read().decode("utf-8", "replace")
for line in ser.splitlines():
    if "scanout" in line or "screen_state" in line:
        print(line.strip())
shutil.rmtree(tmp, ignore_errors=True)
