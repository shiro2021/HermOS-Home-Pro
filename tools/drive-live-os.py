#!/usr/bin/env python3
"""Boot the HermOS live desktop, click 'Install HermOS', then drive the 9-step installer to completion."""
import os, shutil, socket, subprocess, tempfile, time

OVMF = "/home/shiro/HermOS-Main/tools/ovmf1536"
ISO = "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso"
OUT = "/home/shiro/HermOS-Main/artifacts/visual-regression/installer"

tmp = tempfile.mkdtemp(prefix="hermos-live-")
vars_fd = f"{tmp}/vars.fd"
shutil.copy(f"{OVMF}/OVMF_VARS.fd", vars_fd)

DISK = "/home/shiro/HermOS-Main/artifacts/hermos-target.raw"
qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "std",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF}/OVMF_CODE.fd",
    "-drive", f"if=pflash,format=raw,unit=1,file={vars_fd}",
    "-device", "ahci,id=ahci",
    "-drive", f"id=disk,file={DISK},format=raw,if=none",
    "-device", "ide-hd,drive=disk,bus=ahci.0",
    "-drive", f"id=cd,file={ISO},if=none,media=cdrom",
    "-device", "ide-cd,drive=cd,bus=ahci.1",
    "-boot", "d",
    "-net", "none", "-display", "none",
    "-qmp", f"unix:{tmp}/qmp.sock,server,nowait",
    "-serial", f"file:{tmp}/ser.log",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(30)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(f"{tmp}/qmp.sock")
sock.recv(4096)
sock.sendall(b'{"execute":"qmp_capabilities"}\n')
time.sleep(1); sock.recv(4096)

def qmp(cmd):
    sock.sendall((cmd + "\n").encode())
    time.sleep(0.6)
    return sock.recv(4096)

def click(x, y):
    qmp(f'{{"execute":"input-send-event","arguments":{{"events":[{{"type":"abs","data":{{"axis":"x","value":{x*32767//1535}}}}}]}}}}')
    qmp(f'{{"execute":"input-send-event","arguments":{{"events":[{{"type":"abs","data":{{"axis":"y","value":{y*32767//1023}}}}}]}}}}')
    qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}')
    qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}')
    time.sleep(1.2)

def shot(name):
    ppm = f"/tmp/inst-{name}.ppm"
    qmp(f'{{"execute":"screendump","arguments":{{"filename":"{ppm}","format":"ppm"}}}}')
    time.sleep(1)
    out = f"{OUT}/{name}.png"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", ppm, out], capture_output=True)
    return out

os.makedirs(OUT, exist_ok=True)

# 1. live desktop (with the "Install HermOS" dock tile)
shot("live-desktop")

# 2. click "Install HermOS" (dock index 4 -> y = 250 + 4*92 + 40 = 658)
click(60, 658)
shot("installer-launched")     # wizard step 0 ("Initializing...")

# 3. drive the wizard to completion
click(1098, 843)               # step 0 -> 1 (any click)
for _ in range(5):             # steps 1..5 (Continue)
    click(1098, 843)
click(477, 589)                # confirm the data-loss checkbox
click(1098, 843)               # Install -> step 6 (install runs, auto -> step 7)
time.sleep(3.0)                # let format + copy + verify complete
click(1098, 843)               # step 7 -> 8 (any click)
click(1098, 843)               # step 8 -> desktop

sock.close()
qemu.terminate()
try: qemu.wait(timeout=15)
except subprocess.TimeoutExpired: qemu.kill()
shutil.copy(f"{tmp}/ser.log", "/home/shiro/HermOS-Main/artifacts/live-os-serial.log")
shutil.rmtree(tmp, ignore_errors=True)
print("done: live desktop + installer launched + install completed")
