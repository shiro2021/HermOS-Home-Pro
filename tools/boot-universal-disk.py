#!/usr/bin/env python3
"""Boot the universal ISO with an AHCI disk (real-hardware conditions) + QXL 32bpp, capture the desktop."""
import os, shutil, socket, subprocess, tempfile, time

ISO = "/home/shiro/HermOS-Main/genode-main/genode-26.08/build/x86_64/var/run/hermos_universal.iso"
OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"
DISK = "/home/shiro/HermOS-Main/artifacts/hermos-target.raw"
OUT = "/home/shiro/HermOS-Main/artifacts/universal-disk"

assert os.path.exists(ISO), "no ISO"
assert os.path.exists(DISK), "no target disk"

tmp = tempfile.mkdtemp(prefix="hermos-uni-")
shutil.copy(OVMF_VARS, f"{tmp}/vars.fd")
qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-smp", "2",
    "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "qxl",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF_CODE}",
    "-drive", f"if=pflash,format=raw,unit=1,file={tmp}/vars.fd",
    "-device", "ahci,id=ahci",
    "-drive", f"id=disk,file={DISK},format=raw,if=none",
    "-device", "ide-hd,drive=disk,bus=ahci.0",
    "-drive", f"id=cd,file={ISO},if=none,media=cdrom",
    "-device", "ide-cd,drive=cd,bus=ahci.1",
    "-boot", "d", "-net", "none", "-display", "none",
    "-qmp", f"unix:{tmp}/qmp.sock,server,nowait",
    "-serial", f"file:{tmp}/ser.log",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(40)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(f"{tmp}/qmp.sock"); sock.recv(4096)
sock.sendall(b'{"execute":"qmp_capabilities"}\n'); time.sleep(1); sock.recv(4096)
def qmp(c): sock.sendall((c + "\n").encode()); time.sleep(0.6); return sock.recv(4096)

os.makedirs(OUT, exist_ok=True)
ppm = f"{tmp}/shot.ppm"
qmp(f'{{"execute":"screendump","arguments":{{"filename":"{ppm}","format":"ppm"}}}}'); time.sleep(1)
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", ppm, f"{OUT}/desktop.png"], capture_output=True)

sock.close(); qemu.terminate()
try: qemu.wait(timeout=15)
except subprocess.TimeoutExpired: qemu.kill()
shutil.copy(f"{tmp}/ser.log", f"{OUT}/serial.log")
shutil.rmtree(tmp, ignore_errors=True)
print("done:", f"{OUT}/desktop.png", f"{OUT}/serial.log")
