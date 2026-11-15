#!/usr/bin/env python3
"""Boot the HermOS ISO with the custom 1536x1024 OVMF and report the scanout."""
import shutil, subprocess, tempfile, time

OVMF = "/home/shiro/HermOS-Main/tools/ovmf1536"
tmp = tempfile.mkdtemp(prefix="ovmf1536-")
vars_fd = f"{tmp}/vars.fd"
shutil.copy(f"{OVMF}/OVMF_VARS.fd", vars_fd)

qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "std",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF}/OVMF_CODE.fd",
    "-drive", f"if=pflash,format=raw,unit=1,file={vars_fd}",
    "-cdrom", "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso", "-boot", "d",
    "-net", "none", "-display", "none", "-serial", f"file:{tmp}/ser.log",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(32)
qemu.terminate()
try:
    qemu.wait(timeout=15)
except subprocess.TimeoutExpired:
    qemu.kill()

ser = open(f"{tmp}/ser.log", "rb").read().decode("utf-8", "replace")
for line in ser.splitlines():
    if "scanout" in line or "theme applied" in line or "Theme state published" in line or "screen_state" in line or "dock_state" in line:
        print(line.strip())
shutil.rmtree(tmp, ignore_errors=True)
