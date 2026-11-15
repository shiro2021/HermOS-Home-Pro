#!/usr/bin/env python3
"""Probe which QEMU -vga/-device flags yield a 1920x1080 OVMF GOP framebuffer."""
import shutil, subprocess, tempfile, time, os

ISO = "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso"
OVMF = "/usr/share/OVMF/OVMF_CODE_4M.fd"

configs = [
    ("-vga std", []),
    ("-vga virtio", []),
    ("-vga qxl", []),
    ("-vga none", ["-device", "bochs-display"]),
    ("-vga std", ["-global", "VGA.vgamem_mb=256"]),
]

for label, extra in configs:
    tmp = tempfile.mkdtemp(prefix="resprobe-")
    vars_fd = f"{tmp}/vars.fd"
    shutil.copy("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_fd)
    cmd = ["qemu-system-x86_64", "-accel", "kvm", "-machine", "q35",
           "-cpu", "Skylake-Client", "-m", "1024",
           "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet"]
    if label == "-vga std":
        cmd += ["-vga", "std"]
    elif label == "-vga virtio":
        cmd += ["-vga", "virtio"]
    elif label == "-vga qxl":
        cmd += ["-vga", "qxl"]
    elif label == "-vga none":
        cmd += ["-vga", "none"]
    cmd += extra
    cmd += ["-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF}",
            "-drive", f"if=pflash,format=raw,unit=1,file={vars_fd}",
            "-cdrom", ISO, "-boot", "d", "-net", "none", "-display", "none",
            "-serial", f"file:{tmp}/ser.log"]
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(30)
    p.terminate()
    try:
        p.wait(timeout=15)
    except subprocess.TimeoutExpired:
        p.kill()
    ser = ""
    try:
        with open(f"{tmp}/ser.log", "rb") as f:
            ser = f.read().decode("utf-8", "replace")
    except Exception:
        pass
    res = None
    for line in ser.splitlines():
        if "direct scanout" in line or "scanout" in line.lower() and "x" in line:
            res = line.strip()
            break
    print(f"{label:20s} {' '.join(extra):30s} -> {res if res else 'NO scanout log found'}")
    shutil.rmtree(tmp, ignore_errors=True)
