#!/usr/bin/env python3
"""
Drive a booted HermOS guest over QMP: click, type, screenshot.

Usage: qmp-drive.py <qmp-socket> <out-prefix> [steps as JSON]

Steps are executed in order; each step is a dict:
  {"wait": 5.0}
  {"click": [x, y]}                absolute pixel coordinates
  {"type": "ls /bin"}
  {"key": "ret"}                   a single QEMU key name
  {"screenshot": "name"}           writes <out-prefix>-<name>.ppm
"""

import json
import socket
import sys
import time

AXIS_MAX = 32767


class Qmp:
    def __init__(self, path):
        deadline = time.time() + 30
        self.sock = None
        while time.time() < deadline:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(path)
                self.sock = s
                break
            except OSError:
                time.sleep(0.2)
        if self.sock is None:
            sys.exit("QMP socket never appeared")

        self.f = self.sock.makefile("rwb", buffering=0)
        json.loads(self.f.readline())          # greeting
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
            if "error" in msg:
                print("QMP error:", msg["error"])
                return msg
            if "return" in msg:
                return msg


KEYS = {
    " ": "spc", "/": "slash", "-": "minus", ".": "dot",
    "\n": "ret",
}

# shifted character -> base qcode (QEMU's "shift-<base>" yields the shifted char)
SHIFT_MAP = {
    "A": "a", "B": "b", "C": "c", "D": "d", "E": "e", "F": "f", "G": "g",
    "H": "h", "I": "i", "J": "j", "K": "k", "L": "l", "M": "m", "N": "n",
    "O": "o", "P": "p", "Q": "q", "R": "r", "S": "s", "T": "t", "U": "u",
    "V": "v", "W": "w", "X": "x", "Y": "y", "Z": "z",
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
    "{": "bracket_left", "}": "bracket_right", ":": "semicolon",
    '"': "apostrophe", "~": "grave_accent", "|": "backslash",
    "<": "comma", ">": "dot", "?": "slash",
}


def key_name(ch):
    if ch in KEYS:
        return KEYS[ch]
    if ch.isalpha():
        return ch.lower()
    if ch.isdigit():
        return ch
    return None


def main():
    sock_path, prefix = sys.argv[1], sys.argv[2]
    steps = json.loads(sys.argv[3]) if len(sys.argv) > 3 else []

    q = Qmp(sock_path)

    for step in steps:
        if "wait" in step:
            time.sleep(float(step["wait"]))

        elif "click" in step:
            x, y = step["click"]
            q.cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "abs", "data": {"axis": "x", "value": int(x * AXIS_MAX / 1023)}},
                {"type": "abs", "data": {"axis": "y", "value": int(y * AXIS_MAX / 767)}},
            ]}})
            time.sleep(0.2)
            q.cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "btn", "data": {"down": True, "button": "left"}}]}})
            time.sleep(0.1)
            q.cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "btn", "data": {"down": False, "button": "left"}}]}})

        elif "move" in step:
            x, y = step["move"]
            q.cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "abs", "data": {"axis": "x", "value": int(x * AXIS_MAX / 1023)}},
                {"type": "abs", "data": {"axis": "y", "value": int(y * AXIS_MAX / 767)}},
            ]}})
            time.sleep(0.15)

        elif "press" in step:
            q.cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "btn", "data": {"down": True, "button": "left"}}]}})
            time.sleep(0.1)

        elif "release" in step:
            q.cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "btn", "data": {"down": False, "button": "left"}}]}})
            time.sleep(0.1)

        elif "type" in step:
            for ch in step["type"]:
                if ch in SHIFT_MAP:
                    base = SHIFT_MAP[ch]
                    q.cmd({"execute": "input-send-event", "arguments": {"events": [
                        {"type": "key", "data": {"key": {"type": "qcode", "data": "shift"}, "down": True}},
                        {"type": "key", "data": {"key": {"type": "qcode", "data": base}, "down": True}},
                        {"type": "key", "data": {"key": {"type": "qcode", "data": base}, "down": False}},
                        {"type": "key", "data": {"key": {"type": "qcode", "data": "shift"}, "down": False}},
                    ]}})
                else:
                    name = key_name(ch)
                    if name is None:
                        continue
                    q.cmd({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": name}]}})
                time.sleep(0.04)

        elif "key" in step:
            q.cmd({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": step["key"]}]}})

        elif "screenshot" in step:
            path = f"{prefix}-{step['screenshot']}.ppm"
            print("screendump:", q.cmd({"execute": "screendump", "arguments": {"filename": path}}))
            print("wrote", path)

    print("status:", q.cmd({"execute": "query-status"}))


if __name__ == "__main__":
    main()
