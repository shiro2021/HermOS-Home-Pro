#!/usr/bin/env python3
"""Boot the monolith, click the Files dock tile, capture the hermos_files Cell."""
import os, shutil, socket, subprocess, tempfile, time

OVMF = "/home/shiro/HermOS-Main/tools/ovmf1536"
ISO  = "/home/shiro/HermOS-Main/genode-main/genode-26.08/build/x86_64/var/run/hermos_monolith.iso"
DISK = "/home/shiro/HermOS-Main/artifacts/hermos-target.raw"
OUT  = "/home/shiro/HermOS-Main/artifacts/files-test"

assert os.path.exists(ISO), "no ISO"
assert os.path.exists(DISK), "no disk"

tmp = tempfile.mkdtemp(prefix="hermos-files-")
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
def qmp(c): sock.sendall((c + "\n").encode()); time.sleep(0.5); return sock.recv(4096)

def shot(name):
    ppm = f"{tmp}/{name}.ppm"
    qmp(f'{{"execute":"screendump","arguments":{{"filename":"{ppm}","format":"ppm"}}}}'); time.sleep(1)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", ppm, f"{OUT}/{name}.png"], capture_output=True)

def click(x, y):
    qmp(f'{{"execute":"input-send-event","arguments":{{"events":[{{"type":"abs","data":{{"axis":"x","value":{x*32767//1535}}}}}]}}}}')
    qmp(f'{{"execute":"input-send-event","arguments":{{"events":[{{"type":"abs","data":{{"axis":"y","value":{y*32767//1023}}}}}]}}}}')
    qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}')
    qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}')

os.makedirs(OUT, exist_ok=True)
shot("1-desktop")
click(34, 212)   # the Files dock tile (registry index 0)
time.sleep(6)
shot("2-files")

sock.close(); qemu.terminate()
try: qemu.wait(timeout=15)
except subprocess.TimeoutExpired: qemu.kill()
shutil.copy(f"{tmp}/ser.log", f"{OUT}/serial.log")
shutil.rmtree(tmp, ignore_errors=True)
print("done:", f"{OUT}/1-desktop.png", f"{OUT}/2-files.png", f"{OUT}/serial.log")
