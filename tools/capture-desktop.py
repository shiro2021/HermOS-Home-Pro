#!/usr/bin/env python3
"""Capture 6 screenshots of the HermOS desktop in distinct live states."""
import json, shutil, socket, subprocess, sys, tempfile, time

ART = "/home/shiro/HermOS-Main/artifacts/visual-regression"
tmp = tempfile.mkdtemp(prefix="hermos-shots-")
qmp = f"{tmp}/qmp.sock"
vars_fd = f"{tmp}/vars.fd"
shutil.copy("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_fd)

qemu = subprocess.Popen([
    "qemu-system-x86_64", "-accel", "kvm", "-machine", "q35", "-cpu", "Skylake-Client",
    "-m", "1024", "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
    "-vga", "std",
    "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
    "-drive", f"if=pflash,format=raw,unit=1,file={vars_fd}",
    "-cdrom", "/home/shiro/HermOS-Main/artifacts/HermOS_V0.18-monolith.iso", "-boot", "d",
    "-net", "none", "-display", "none",
    "-serial", f"file:{ART}/p2-v45.serial.log",
    "-qmp", f"unix:{qmp},server,nowait",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
for _ in range(300):
    try:
        s.connect(qmp)
        break
    except (FileNotFoundError, ConnectionRefusedError):
        time.sleep(0.1)
else:
    print("QMP connect failed"); qemu.terminate(); sys.exit(1)

s.recv(65536)
s.sendall(b'{"execute":"qmp_capabilities"}\n')
time.sleep(0.5)
s.recv(65536)

def send_raw(obj):
    s.sendall(json.dumps(obj).encode() + b"\n")
    time.sleep(0.3)
    s.recv(65536)

def move(x, y):
    send_raw({"execute": "input-send-event", "arguments": {"events": [
        {"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / 1023)}},
        {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / 767)}}]}})

def button(down):
    send_raw({"execute": "input-send-event", "arguments": {"events": [
        {"type": "btn", "data": {"down": down, "button": "left"}}]}})

def key(qcode, down):
    send_raw({"execute": "input-send-event", "arguments": {"events": [
        {"type": "key", "data": {"key": {"type": "qcode", "data": qcode}, "down": down}}]}})

def shot(name):
    s.sendall(json.dumps({"execute": "screendump",
                          "arguments": {"filename": f"{ART}/{name}.ppm"}}).encode() + b"\n")
    time.sleep(2)
    s.recv(65536)

time.sleep(28)
shot("1-base")

# move the pointer to the bottom edge -> the dock slides in
move(512, 765)
time.sleep(2)
shot("2-dock")

# move pointer back up
move(512, 300)
time.sleep(1)

# Meta+T -> master-stack tiling
key("meta_l", True)
key("t", True); key("t", False)
key("meta_l", False)
time.sleep(2)
shot("3-tiled")

# Meta+T -> restore the theme grid
key("meta_l", True)
key("t", True); key("t", False)
key("meta_l", False)
time.sleep(2)
shot("4-restored")

# drag cell 0 (title bar ~(150,285)) to the left edge -> snap to left half
move(150, 285); button(True)
for x in range(140, 5, -20):
    move(x, 285)
move(5, 285); button(False)
time.sleep(2)
shot("5-snapped")

# click the maximize control of the snapped cell 0 (top-right of its title bar)
move(496, 18); button(True); button(False)
time.sleep(2)
shot("6-maximized")

qemu.terminate()
qemu.wait(timeout=20)
print("done")
