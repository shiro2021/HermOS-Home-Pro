#!/usr/bin/env python3
"""Boot HermOS + send Meta+T (the held-meta sequence) + screenshot the tiled Cells."""
import json, os, shutil, socket, subprocess, sys, tempfile, time

ART = "/home/shiro/HermOS-Main/artifacts/visual-regression"
tmp = tempfile.mkdtemp(prefix="hermos-tile-")
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
    "-serial", f"file:{ART}/p2-v43.serial.log",
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

def send(events):
    s.sendall(json.dumps({"execute": "input-send-event",
                          "arguments": {"events": events}}).encode() + b"\n")
    time.sleep(0.35)
    s.recv(65536)

def shot(name):
    s.sendall(json.dumps({"execute": "screendump",
                          "arguments": {"filename": f"{ART}/{name}.ppm"}}).encode() + b"\n")
    time.sleep(2)
    s.recv(65536)

time.sleep(26)
shot("p2-v43-base")

# a single plain key first: the desktop should log it
send([{"type": "key", "data": {"key": {"type": "qcode", "data": "t"}, "down": True}},
      {"type": "key", "data": {"key": {"type": "qcode", "data": "t"}, "down": False}}])
time.sleep(3)

# Meta down, T down/up, Meta up
send([{"type": "key", "data": {"key": {"type": "qcode", "data": "meta_l"}, "down": True}}])
send([{"type": "key", "data": {"key": {"type": "qcode", "data": "t"}, "down": True}},
      {"type": "key", "data": {"key": {"type": "qcode", "data": "t"}, "down": False}}])
send([{"type": "key", "data": {"key": {"type": "qcode", "data": "meta_l"}, "down": False}}])

time.sleep(4)
shot("p2-v43-tiled")

# and back
send([{"type": "key", "data": {"key": {"type": "qcode", "data": "meta_l"}, "down": True}}])
send([{"type": "key", "data": {"key": {"type": "qcode", "data": "t"}, "down": True}},
      {"type": "key", "data": {"key": {"type": "qcode", "data": "t"}, "down": False}}])
send([{"type": "key", "data": {"key": {"type": "qcode", "data": "meta_l"}, "down": False}}])

time.sleep(4)
shot("p2-v43-restored")

qemu.terminate()
qemu.wait(timeout=20)
print("done")
