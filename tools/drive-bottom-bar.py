#!/usr/bin/env python3
"""Boot the live desktop, verify the collapsed bottom bar, click to expand, capture."""
import os, shutil, socket, subprocess, tempfile, time

OVMF = "/home/shiro/HermOS-Main/tools/ovmf1536"
ISO = "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso"
OUT = "/home/shiro/HermOS-Main/artifacts/visual-regression/installer"

tmp = tempfile.mkdtemp(prefix="hermos-bar-")
shutil.copy(f"{OVMF}/OVMF_VARS.fd", f"{tmp}/vars.fd")
qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "std",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF}/OVMF_CODE.fd",
    "-drive", f"if=pflash,format=raw,unit=1,file={tmp}/vars.fd",
    "-device", "ahci,id=ahci",
    "-drive", f"id=disk,file=/home/shiro/HermOS-Main/artifacts/hermos-target.raw,format=raw,if=none",
    "-device", "ide-hd,drive=disk,bus=ahci.0",
    "-drive", f"id=cd,file={ISO},if=none,media=cdrom",
    "-device", "ide-cd,drive=cd,bus=ahci.1",
    "-boot", "d", "-net", "none", "-display", "none",
    "-qmp", f"unix:{tmp}/qmp.sock,server,nowait",
    "-serial", f"file:{tmp}/ser.log",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(30)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(f"{tmp}/qmp.sock"); sock.recv(4096)
sock.sendall(b'{"execute":"qmp_capabilities"}\n'); time.sleep(1); sock.recv(4096)

def qmp(c): sock.sendall((c+"\n").encode()); time.sleep(0.6); return sock.recv(4096)
def click(x, y):
    qmp(f'{{"execute":"input-send-event","arguments":{{"events":[{{"type":"abs","data":{{"axis":"x","value":{x*32767//1535}}}}}]}}}}')
    qmp(f'{{"execute":"input-send-event","arguments":{{"events":[{{"type":"abs","data":{{"axis":"y","value":{y*32767//1023}}}}}]}}}}')
    qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}')
    qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}')
    time.sleep(1.5)
def shot(name):
    ppm = f"{tmp}/{name}.ppm"
    qmp(f'{{"execute":"screendump","arguments":{{"filename":"{ppm}","format":"ppm"}}}}'); time.sleep(1)
    out = f"{OUT}/{name}.png"
    subprocess.run(["ffmpeg","-y","-loglevel","error","-i",ppm,out], capture_output=True)
    return out

os.makedirs(OUT, exist_ok=True)
shot("bottom-bar-collapsed")       # bar at the bottom, collapsed
click(768, 1010)                   # click the bottom bar -> expand
shot("bottom-bar-expanded")        # expanded, reveals the running Cells

sock.close(); qemu.terminate()
try: qemu.wait(timeout=15)
except subprocess.TimeoutExpired: qemu.kill()
shutil.copy(f"{tmp}/ser.log", "/home/shiro/HermOS-Main/artifacts/bottom-bar-serial.log")
shutil.rmtree(tmp, ignore_errors=True)
print("done: collapsed + expanded bottom bar captured")
