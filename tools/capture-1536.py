#!/usr/bin/env python3
"""Boot at 1536x1024 and capture a PNG screenshot via QMP screendump."""
import os, shutil, socket, subprocess, tempfile, time

OVMF = "/home/shiro/HermOS-Main/tools/ovmf1536"
ISO = "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso"
OUT = "/home/shiro/HermOS-Main/artifacts/visual-regression/1536x1024-base.png"

tmp = tempfile.mkdtemp(prefix="hermos-cap-")
vars_fd = f"{tmp}/vars.fd"
shutil.copy(f"{OVMF}/OVMF_VARS.fd", vars_fd)

qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "std",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF}/OVMF_CODE.fd",
    "-drive", f"if=pflash,format=raw,unit=1,file={vars_fd}",
    "-cdrom", ISO, "-boot", "d",
    "-net", "none", "-display", "none",
    "-qmp", f"unix:{tmp}/qmp.sock,server,nowait",
    "-serial", f"file:{tmp}/ser.log",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(30)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(f"{tmp}/qmp.sock")
sock.recv(4096)  # QMP greeting
sock.sendall(b'{"execute":"qmp_capabilities"}\n')
time.sleep(1)
sock.recv(4096)  # capabilities ack
ppm = "/tmp/hermos-shot.ppm"
cmd = f'{{"execute":"screendump","arguments":{{"filename":"{ppm}","format":"ppm"}}}}\n'
sock.sendall(cmd.encode())
time.sleep(3)
resp = sock.recv(4096)
print("QMP:", resp.decode(errors="replace").strip())
sock.close()

qemu.terminate()
try:
    qemu.wait(timeout=15)
except subprocess.TimeoutExpired:
    qemu.kill()

os.makedirs(os.path.dirname(OUT), exist_ok=True)
r = subprocess.run(["ffmpeg", "-y", "-i", ppm, OUT], capture_output=True)
print("ffmpeg:", r.returncode)
print("saved:", OUT, os.path.getsize(OUT) if os.path.exists(OUT) else "FAILED")
shutil.rmtree(tmp, ignore_errors=True)
