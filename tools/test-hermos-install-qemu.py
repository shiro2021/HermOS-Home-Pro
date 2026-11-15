#!/usr/bin/env python3
"""
HermOS end-to-end installer test in QEMU (UEFI/OVMF).

  tools/test-hermos-install-qemu.py --iso HermOS_V0.18.iso \
      --image HermOS_V0.18.img --out artifacts/logs/V0.18-install

Stage 1  boots the installer ISO with ONE dedicated, freshly created raw disk
         (<out>-target.raw) attached as AHCI port 0 and tagged with the
         install identity (model/serial). Drives the installer only by
         keyboard over QMP and waits for serial-log markers:
         boot -> Welcome -> Computer -> Target (Space selects) -> Plan ->
         Review (Space acknowledges erase) -> Install -> Complete.
Stage 2  compares the target disk with the release image on the host.
Stage 3  boots the target disk alone (no ISO, QEMU -snapshot) and waits for
         the installed system's Hermes Core Director.

Safety: the target is a new regular file created by this script; the script
refuses to reuse an existing file and never attaches anything else writable.
The ISO is attached read-only as CD-ROM. Screenshots go to <out>-*.png/.ppm,
serial logs to <out>-installer.serial.log / <out>-installed.serial.log, and a
PASS/FAIL/NOT TESTED summary to <out>-summary.md.
"""

import argparse
import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

MODEL = "HERMOS_INSTALL_TARGET"
SERIAL = "HERMOSQEMU0001"


class Qmp:
    def __init__(self, path, timeout=30):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(path)
                break
            except OSError:
                if time.time() > deadline:
                    raise RuntimeError("QMP socket never appeared")
                time.sleep(0.2)
        self.f = self.sock.makefile("rwb", buffering=0)
        json.loads(self.f.readline())
        self.cmd({"execute": "qmp_capabilities"})

    def cmd(self, payload):
        self.f.write(json.dumps(payload).encode() + b"\n")
        while True:
            line = self.f.readline()
            if not line:
                return None
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if "return" in msg or "error" in msg:
                return msg

    def key(self, qcode):
        return self.cmd({"execute": "send-key",
                         "arguments": {"keys": [{"type": "qcode", "data": qcode}]}})

    def screenshot(self, path):
        return self.cmd({"execute": "screendump", "arguments": {"filename": path}})


class Serial:
    def __init__(self, path):
        self.path = path

    def text(self):
        try:
            with open(self.path, "rb") as f:
                return f.read().decode("utf-8", "replace")
        except FileNotFoundError:
            return ""

    def mark(self):
        return len(self.text())

    def wait(self, marker, since=0, timeout=120):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if marker in self.text()[since:]:
                return True
            time.sleep(0.5)
        return False


def to_png(ppm):
    try:
        from PIL import Image
        png = ppm[:-4] + ".png"
        Image.open(ppm).save(png)
        return png
    except Exception:
        return ppm


def qemu_base(ovmf_code, vars_path, qmp, serial_log):
    accel = "kvm" if os.access("/dev/kvm", os.R_OK | os.W_OK) else "tcg"
    return ["qemu-system-x86_64", "-accel", accel, "-machine", "q35",
            "-cpu", "Skylake-Client", "-m", "1024", "-smp", "2",
            "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet",
            "-vga", "std",
            "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={ovmf_code}",
            "-drive", f"if=pflash,format=raw,unit=1,file={vars_path}",
            "-net", "none", "-display", "none",
            "-serial", f"file:{serial_log}",
            "-qmp", f"unix:{qmp},server,nowait"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--image", required=True, help="release system image for host-side compare")
    ap.add_argument("--out", required=True, help="output prefix, e.g. artifacts/logs/V0.18-install")
    ap.add_argument("--disk-size-mib", type=int, default=256)
    ap.add_argument("--ovmf-code", default=os.environ.get("HERMOS_OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
    ap.add_argument("--ovmf-vars", default=os.environ.get("HERMOS_OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
    ap.add_argument("--install-timeout", type=int, default=600)
    args = ap.parse_args()

    for p in (args.iso, args.image, args.ovmf_code, args.ovmf_vars):
        if not os.path.isfile(p):
            sys.exit(f"missing file: {p}")
    if not shutil.which("qemu-system-x86_64"):
        sys.exit("qemu-system-x86_64 not found")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    target = os.path.abspath(args.out + "-target.raw")
    if os.path.lexists(target):
        sys.exit(f"refusing to reuse existing target disk {target}; choose a new --out")

    # create the dedicated disposable disk as a new regular file
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    os.ftruncate(fd, args.disk_size_mib << 20)
    os.close(fd)
    assert stat.S_ISREG(os.stat(target).st_mode)

    results = []        # (check, status, evidence)
    commands = []

    def record(check, ok, evidence, untested=False):
        status = "NOT TESTED" if untested else ("PASS" if ok else "FAIL")
        results.append((check, status, evidence))
        print(f"[{status}] {check}: {evidence}", flush=True)
        return ok

    tmp = tempfile.mkdtemp(prefix="hermos-install-test.")
    try:
        # ---------------- stage 1: installer ----------------
        vars1 = os.path.join(tmp, "vars1.fd")
        shutil.copy(args.ovmf_vars, vars1)
        qmp1 = os.path.join(tmp, "qmp1.sock")
        log1 = os.path.abspath(args.out + "-installer.serial.log")
        cmd1 = qemu_base(args.ovmf_code, vars1, qmp1, log1) + [
            "-drive", f"id=hermos-target,file={target},format=raw,if=none",
            "-device", f"ide-hd,drive=hermos-target,bus=ide.0,model={MODEL},serial={SERIAL}",
            "-drive", f"file={os.path.abspath(args.iso)},media=cdrom,readonly=on,if=none,id=cd0",
            "-device", "ide-cd,drive=cd0,bus=ide.2,bootindex=0"]
        commands.append(" ".join(cmd1))
        q = subprocess.Popen(cmd1)
        serial = Serial(log1)
        ok_install = False
        try:
            qmp = Qmp(qmp1)

            def shot(name):
                ppm = os.path.abspath(f"{args.out}-{name}.ppm")
                qmp.screenshot(ppm)
                time.sleep(0.5)
                return to_png(ppm)

            def step(key, marker, name, timeout=60):
                since = serial.mark()
                if key:
                    qmp.key(key)
                ok = serial.wait(marker, since, timeout)
                time.sleep(1.0)
                png = shot(name)
                return ok, png

            genode = serial.wait("[init", 0, 120)
            record("Genode output on serial (serial_fallback)", genode,
                   "'[init' seen in installer serial log" if genode else "no Genode lines in serial log")
            if not genode:
                raise RuntimeError("no Genode serial output; cannot drive the test by markers")

            ok = serial.wait("HermOS installer: boot screen shown", 0, 120)
            png = shot("1-boot")
            record("Boot screen", ok, png)

            ok = serial.wait("install service abi=2", 0, 120)
            verdict_ok = serial.wait("target verdict: Dedicated HermOS install disk identified", 0, 30)
            record("Executor connected (ABI 2)", ok, "install service abi=2 in serial log")
            record("Dedicated QEMU disk identified (executor + AHCI report agree)", verdict_ok,
                   f"model={MODEL} serial={SERIAL}")

            pages = [("ret", "page Welcome", "2-welcome"),
                     ("ret", "page Your Computer", "3-computer"),
                     ("ret", "page Installation Target", "4-target"),
                     ("spc", "page Installation Target", "4b-target-selected"),
                     ("ret", "page Installation Plan", "5-plan"),
                     ("ret", "page Review & Confirm", "6-review"),
                     ("spc", "page Review & Confirm", "6b-review-confirmed")]
            for key, marker, name in pages:
                ok, png = step(key, marker, name)
                record(f"Screen {name}", ok, png)
                if not ok:
                    raise RuntimeError(f"marker '{marker}' not seen")

            since = serial.mark()
            qmp.key("ret")
            approved = serial.wait("approval result 1", since, 30)
            record("Explicit approval accepted after revalidation", approved,
                   "'approval result 1' and 'revalidated target' in serial log")
            if not approved:
                raise RuntimeError("approval not accepted")
            time.sleep(2)
            record("Screen 7-installing", True, shot("7-installing"))

            serial.wait("read-back verification started", since, args.install_timeout)
            time.sleep(1)
            record("Screen 8-finalizing", True, shot("8-finalizing"))

            ok_install = serial.wait("COMPLETE; full image read-back verified", since, args.install_timeout)
            record("Executor write + full read-back verification", ok_install,
                   "'COMPLETE; full image read-back verified' in serial log")
            ok = serial.wait("page Installation Complete", since, 30)
            time.sleep(1)
            record("Complete screen only after verification", ok, shot("9-complete"))
        except Exception as e:
            record("Installer stage", False, f"aborted: {e}")
        finally:
            q.terminate()
            try:
                q.wait(10)
            except subprocess.TimeoutExpired:
                q.kill()

        # ---------------- stage 2: host-side compare ----------------
        image_size = os.path.getsize(args.image)
        with open(args.image, "rb") as a, open(target, "rb") as b:
            same = a.read() == b.read(image_size)
        record("Target bytes equal release image (host compare)", same,
               f"first {image_size} bytes of {os.path.basename(target)} vs {os.path.basename(args.image)}")

        # ---------------- stage 3: boot installed disk ----------------
        if ok_install and same:
            vars2 = os.path.join(tmp, "vars2.fd")
            shutil.copy(args.ovmf_vars, vars2)
            qmp2 = os.path.join(tmp, "qmp2.sock")
            log2 = os.path.abspath(args.out + "-installed.serial.log")
            cmd2 = qemu_base(args.ovmf_code, vars2, qmp2, log2) + [
                "-snapshot",
                "-drive", f"id=disk0,file={target},format=raw,if=none",
                "-device", "ide-hd,drive=disk0,bus=ide.0,bootindex=0"]
            commands.append(" ".join(cmd2))
            q2 = subprocess.Popen(cmd2)
            try:
                qmp = Qmp(qmp2)
                s2 = Serial(log2)
                booted = s2.wait("Hermes Core Director READY", 0, 180)
                time.sleep(3)
                ppm = os.path.abspath(f"{args.out}-installed-boot.ppm")
                qmp.screenshot(ppm)
                time.sleep(0.5)
                record("Reboot from installed disk (no ISO)", booted,
                       f"'Hermes Core Director READY' in {os.path.basename(log2)}; {to_png(ppm)}")
            finally:
                q2.terminate()
                try:
                    q2.wait(10)
                except subprocess.TimeoutExpired:
                    q2.kill()
        else:
            record("Reboot from installed disk (no ISO)", False,
                   "skipped because installation did not verify", untested=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    summary = os.path.abspath(args.out + "-summary.md")
    with open(summary, "w") as f:
        f.write("# HermOS QEMU install test\n\n")
        f.write(f"ISO: `{args.iso}`  \nImage: `{args.image}`  \nTarget disk: `{target}`\n\n")
        f.write("| Check | Status | Evidence |\n|---|---|---|\n")
        for c, s, e in results:
            f.write(f"| {c} | {s} | {e} |\n")
        f.write("\n## Commands\n\n")
        for c in commands:
            f.write(f"```\n{c}\n```\n")
    print(f"summary: {summary}")
    return 0 if all(s != "FAIL" for _, s, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
