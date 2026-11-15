#!/usr/bin/env python3
"""Boot the HermOS installer and click through all 9 wizard steps, capturing each."""
import os, shutil, socket, subprocess, tempfile, time

OVMF = "/home/shiro/HermOS-Main/tools/ovmf1536"
ISO = "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso"
OUT = "/home/shiro/HermOS-Main/artifacts/visual-regression/installer"

tmp = tempfile.mkdtemp(prefix="hermos-inst-")
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
names = ["step0-boot", "step1-welcome", "step2-detect", "step3-target", "step4-plan",
         "step5-review", "step6-installing", "step7-finalizing", "step8-complete"]
shot(names[0])
for i in range(1, 6):           # steps 1..5 (Continue)
    click(1098, 843)
    shot(names[i])
click(477, 589)                 # confirm the data-loss checkbox
shot("step5-confirmed")
click(1098, 843)                # Install HermOS -> step 6 (starts the timer-driven install)
time.sleep(1.5)                 # ~2.7s into install (format nearly done)
# tight 0.1s burst to catch the brief 40%/96% progress-bar states
ppms = []
for i in range(10):
    ppm = f"/tmp/inst-snap-{i}.ppm"
    sock.sendall((f'{{"execute":"screendump","arguments":{{"filename":"{ppm}","format":"ppm"}}}}\n').encode())
    time.sleep(0.1)
    sock.recv(4096)
    ppms.append(ppm)
for i, ppm in enumerate(ppms):
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", ppm, f"{OUT}/step6-snap-{i}.png"], capture_output=True)
time.sleep(2.0)                 # let the rest run to completion (auto -> step 7)
shot("step7-finalizing")
click(1098, 843)                # step 7 -> 8 (any click)
shot("step8-complete")
click(1098, 843)                # Reboot -> desktop
shot("desktop")

sock.close()
qemu.terminate()
try: qemu.wait(timeout=15)
except subprocess.TimeoutExpired: qemu.kill()
shutil.copy(f"{tmp}/ser.log", "/home/shiro/HermOS-Main/artifacts/installer-serial.log")
shutil.rmtree(tmp, ignore_errors=True)
print("done: 9 steps + desktop captured")
