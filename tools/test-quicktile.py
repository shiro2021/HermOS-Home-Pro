#!/usr/bin/env python3
"""Boot, send Meta+Left (quick-tile), capture, and check the serial for the quick-tile log."""
import os, shutil, socket, subprocess, tempfile, time

OVMF = "/home/shiro/HermOS-Main/tools/ovmf1536"
ISO  = "/home/shiro/HermOS-Main/genode-main/genode-26.08/build/x86_64/var/run/hermos_monolith.iso"
DISK = "/home/shiro/HermOS-Main/artifacts/hermos-target.raw"
OUT  = "/home/shiro/HermOS-Main/artifacts/files-test"

tmp = tempfile.mkdtemp(prefix="hermos-qt-")
shutil.copy(f"{OVMF}/OVMF_VARS.fd", f"{tmp}/vars.fd")
qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-smp", "2",
    "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "std",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF}/OVMF_CODE.fd",
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

time.sleep(35)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(f"{tmp}/qmp.sock"); sock.recv(4096)
sock.sendall(b'{"execute":"qmp_capabilities"}\n'); time.sleep(1); sock.recv(4096)

def qmp(c):
    sock.sendall((c + "\n").encode()); time.sleep(0.3); return sock.recv(4096)

def key(qc, down):
    qmp(f'{{"execute":"input-send-event","arguments":{{"events":[{{"type":"key","data":{{"key":{{"type":"qcode","data":"{qc}"}},"down":{str(down).lower()}}}}}]}}}}')

# Meta + Left
key("meta_l", True); time.sleep(0.1)
key("left", True);   time.sleep(0.1)
key("left", False);  time.sleep(0.1)
key("meta_l", False); time.sleep(0.5)

shot = f"{tmp}/qt.ppm"
qmp(f'{{"execute":"screendump","arguments":{{"filename":"{shot}","format":"ppm"}}}}'); time.sleep(1)
subprocess.run(["ffmpeg","-y","-loglevel","error","-i",shot,f"{OUT}/quicktile.png"], capture_output=True)

sock.close(); qemu.terminate()
try: qemu.wait(timeout=15)
except subprocess.TimeoutExpired: qemu.kill()
shutil.copy(f"{tmp}/ser.log", f"{OUT}/serial.log")
shutil.rmtree(tmp, ignore_errors=True)
print("done")
