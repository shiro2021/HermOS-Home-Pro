#!/usr/bin/env python3
"""
Build the HermOS question/answer material for the Shiro Brain.

  build_training_data.py REPO_DIR OUT.txt

Two sources: the status table in doc/FUNCTIONS.md (every row becomes questions and one answer, so the
model cannot drift from what the system really does) and a list of curated facts below. Format of the
output, one sample per paragraph:

    Q: <question>
    A: <answer>
"""
import re, sys, random

repo, out = sys.argv[1], sys.argv[2]
random.seed(11)

FACTS = [
 ("What is HermOS?", "HermOS is an AI-native operating system built on the Genode framework and the NOVA microhypervisor. Its motto is A More Human Computer: you say what you want and the system does it."),
 ("What does HermOS run on?", "HermOS runs on Genode 26.08 with the NOVA microhypervisor on x86-64 PCs that boot with UEFI. It has only been tested in QEMU so far, not on real hardware."),
 ("What are Cells?", "Cells are the floating, octagonal, resizable and snappable frames that hold applications in HermOS. HermOS never calls them windows."),
 ("Does HermOS have windows?", "No. HermOS has Cells. A Cell is an octagonal frame with a glowing rim when it is active and a carbon gray rim when it is not."),
 ("What is an active Cell?", "The active Cell is the one in use. It has a bright orange rim with a white-hot core line and a glow. Inactive Cells have a carbon gray rim."),
 ("What is theme.hid?", "theme.hid is the data file that holds every layout, size and color of the desktop. The theme engine reads it and publishes the numbers, so nothing is hardcoded in the desktop."),
 ("What is the theme engine?", "The theme engine is a component that parses theme.hid, turns its percentages into pixels and publishes the result to the desktop builder as a report."),
 ("What is the desktop builder?", "The desktop builder is the main user interface component. It draws the whole screen in software, owns the framebuffer and the input events, serves the Gui to application Cells and handles z-order, snapping and the panels."),
 ("What is the command bar?", "The command bar is the single search field in the middle of the desktop. You type what you want, press Enter, and the Hermes agent answers in a reply panel."),
 ("How do I ask HermOS something?", "Click the command bar in the middle of the desktop, type your request and press Enter. The answer appears in a panel."),
 ("How do I open the Terminal?", "Press the play button of Terminal in the Agents and Workers pane, or pin the left dock with Meta+L and click Terminal. It opens a bash shell in a Cell."),
 ("How do I open Files?", "Press the play button of Files in the Agents and Workers pane, or pin the left dock with Meta+L and click Files."),
 ("What does Meta+L do?", "Meta+L shows or hides the left dock, which holds the Files, Terminal and Install launchers."),
 ("What does Meta+B do?", "Meta+B shows or hides the bottom bar, the taskbar with the clock."),
 ("What does Meta+A do?", "Meta+A shows or hides the Agents and Workers pane together with the navigation rail."),
 ("What does Meta+K do?", "Meta+K is Kill Mode. It closes the front-most Cell at once, destroys its sandbox and returns its memory quota."),
 ("How do I move and resize a Cell?", "Drag the Cell by its title strip and drag its edges to resize. Meta with the arrow keys, Home, End, Page Up and Page Down snaps the Cell to halves and quarters of the screen."),
 ("How do I maximize a Cell?", "Press the maximize button in the Cell's title strip. Press it a second time to put the Cell back where it was."),
 ("What is the Agents and Workers pane?", "It is the panel on the right that lists the running apps with a close button and the available apps with a play button. Hide and show it with its X button, the Agents tile on the rail, or Meta+A."),
 ("What is the rail?", "The rail is the vertical strip of hexagon tiles on the right edge: Agents, System, Files, Network, Devices and Settings. The Agents tile shows how many apps are running."),
 ("How do I install HermOS?", "Boot the installer ISO, press Install HermOS in the Welcome Cell or the Install launcher, choose the target disk and the size of the HermOS partition, tick that you understand the disk will be erased, and press Install HermOS. The installer writes the system, reads it back to verify it, and then offers a reboot."),
 ("What does the installer do to my disk?", "It erases the chosen disk, writes a GPT with the HermOS system and a data partition, and verifies the write by reading it back and comparing. Dual boot and keeping existing data are not supported."),
 ("How big must the disk be?", "Use a disk of at least 16 GB. HermOS itself needs a few gigabytes, and the rest becomes the data partition, so a very small disk leaves no room for data."),
 ("Where are my files stored?", "Your files live on the data partition. The Terminal mounts it as /home, and files written there survive a restart."),
 ("Does HermOS keep my files after a restart?", "Yes, files in /home on the data partition persist across restarts. The installed system itself runs from RAM like the live one."),
 ("What is MyKee?", "MyKee is the one-key gateway for AI models. The MyKee router holds the key, tries the routes in order, and returns the first answer. The first route is the local Shiro Brain when the image has a model, then the cloud model."),
 ("What is the MyKee key?", "The MyKee key is the API key of the AI service. It is not stored in the source: you give it to the build with HERMOS_MYKEE_KEY_FILE, and an image built without it answers that no key is present."),
 ("What is Hermes?", "Hermes is the first reference agent of HermOS. The Hermes agent relays your request to the AI router; the Hermes Core Director is the planned coordinator for planning, policy, memory and workers."),
 ("What is Shiro?", "Shiro is the name of HermOS's AI helper and of its theme. Shiro Brain is the local language model that answers questions about HermOS."),
 ("What is the Shiro Brain?", "The Shiro Brain is a small local language model that runs on the device with llama.cpp. It knows how HermOS works and can use REA to propose new components. It runs offline."),
 ("Does HermOS need the internet for AI?", "No. When the image carries a model, the Shiro Brain answers locally with no network. The cloud model is only the fallback."),
 ("What is the Capability Broker?", "The Capability Broker is the security gatekeeper. Every app has a manifest that says what it may reach, such as the network, block devices and the file system, and what memory it may use. A launch whose manifest is not valid is refused. There are no permission pop-ups."),
 ("Does HermOS ask for permission with pop-ups?", "No. Security is architectural: the Capability Broker and Genode's capability isolation decide what an app can reach, not prompts to the user."),
 ("What is REA?", "REA is reverse engineering analysis. An observer agent studies a program's behavior and writes down a specification, and a builder agent writes new C++ from that specification. REA Cells are temporary and strictly sandboxed."),
 ("What is a REA Cell?", "A REA Cell is an ephemeral, one-use Cell with no network and no disk write. It is where analysis and generated components are examined before anything is kept."),
 ("What is a System Cell?", "A System Cell is a trusted, native, persistent Cell, such as the Terminal or Files. REA Cells are the opposite: temporary and sandboxed."),
 ("What is the Clean Room?", "The Clean Room is the legal safety rule for REA. The observer extracts behavior and interface specifications. The builder writes clean C++ from scratch using only that specification. Decompiled code is never translated directly."),
 ("What is the Vault?", "The Vault is where ported open-source apps and AI-generated REA components are kept for the user to install."),
 ("How are components made in HermOS?", "A component is a Genode program with a target.mk, a main.cc and a manifest. The Shiro Brain proposes the files, a REA Cell checks them, and the user decides whether to keep them."),
 ("What does a component manifest contain?", "A manifest is a small HID text: name, kind (system or rea), the capabilities (vfs path, nic, block, gui) and the quotas (ram and cpu). For example nic none and block none means no network and no disk."),
 ("What is a HID file?", "A HID file is the indented text format of HermOS: lines starting with a plus sign open a section, and key: value lines set values. theme.hid and the manifests use it."),
 ("How do I build HermOS?", "Use tools/build-hermos-v5.sh with HERMOS_GENODE_DIR pointing at a Genode 26.08 tree that has repos/hermos linked in. It builds the system image and the installer ISO."),
 ("How do I give the build an AI key?", "Set HERMOS_MYKEE_KEY_FILE to a file that contains only the key before running the build script. Without it the image has no key."),
 ("How do I put a local model into the image?", "Set HERMOS_BRAIN_MODEL to a GGUF file when running the build script. The model becomes a boot module and the Shiro Brain component starts with it."),
 ("How can I test HermOS?", "Boot the ISO in QEMU with OVMF firmware, a blank disk of at least 16 GB for the install test, and 1 to 3 GB of RAM. Without KVM the desktop is slow because it draws in software."),
 ("Why is HermOS slow in QEMU?", "The desktop is drawn in software by one CPU core, and QEMU without KVM emulates the CPU. Real hardware should be many times faster, but that has not been measured."),
 ("Does HermOS run on real hardware?", "That has never been tested. All verification so far was in QEMU with UEFI firmware."),
 ("Does HermOS support Wi-Fi?", "No. Only wired network cards through the iPXE driver, such as the Intel e1000 family, work. Wi-Fi, Bluetooth and audio are not there."),
 ("Is there a web browser?", "A HermOS Browser is built as a program, but it is blocked: the prebuilt Qt WebEngine packages target an older Genode base API than HermOS uses, so the image does not include it."),
 ("What is the status VERIFIED?", "VERIFIED means the function was run and seen working in QEMU. It does not mean it was tested on physical hardware."),
 ("What does BUILT mean?", "BUILT means the code is in the image and starts, but its behavior was not tested."),
 ("What does STUB mean?", "STUB means the code exists but the real work is a placeholder."),
 ("What does VISUAL mean?", "VISUAL means it is drawn on the screen with no function behind it."),
 ("What does ABSENT mean?", "ABSENT means the function is not in the system."),
 ("What does SOURCE ONLY mean?", "SOURCE ONLY means the code is in the repository but not part of the image."),
 ("What is the Hermes Core Director?", "The Hermes Core Director is the planned central coordinator of HermOS. It plans, enforces policy, keeps memory and task state, brokers capabilities and manages workers. Its source exists but it is not part of the current image."),
 ("Can HermOS update itself?", "Not yet. The Updates Cell is a mock-up. Updates, rollback and recovery are planned."),
 ("What is the Welcome Cell?", "The Welcome Cell greets you and starts the installer with its Install HermOS button. On an installed system it shows tips instead."),
 ("What is HermOS Server?", "HermOS Server is the planned server edition: an nginx-class web and reverse proxy server on Genode, with the Hermes agent as the master and sandboxed workers."),
 ("How do I hide the right pane?", "Click the X of the Agents and Workers pane, click the Agents tile on the rail, or press Meta+A. A slim bar stays on the edge so you can bring it back."),
 ("What colors does HermOS use?", "The Shiro theme is dark carbon with orange and peach accents. Active elements are orange; inactive ones are carbon gray. The numbers live in theme.hid."),
 ("What is the Genode framework?", "Genode is an operating-system framework where every component runs in its own sandbox and receives only the capabilities it is given. HermOS builds its desktop and services as Genode components."),
 ("What is NOVA?", "NOVA is the microhypervisor that Genode runs on in HermOS. It provides isolation, address spaces and scheduling underneath the components."),
 ("How do I write a Genode component for HermOS?", "Make a directory under src/app with a target.mk that names the target, its libraries and its sources, and a main.cc with the component entry point. Add its manifest, list it in the run script, and build."),
 ("What rules must a HermOS component follow?", "Never use the word window, use Cells. Take layouts and colors from theme.hid, never hardcode pixels. Keep the user interface free of libc. Use no permission pop-ups. Declare a manifest with the least capabilities needed."),
]

# the status table: every row is one fact the model must be able to repeat
rows = []
txt = open(f"{repo}/repos/hermos/doc/FUNCTIONS.md", encoding="utf-8").read()
for line in txt.splitlines():
    m = re.match(r"\|\s*(.+?)\s*\|\s*(VERIFIED|BUILT|STUB|VISUAL|ABSENT|SOURCE ONLY|PARTIAL|BLOCKED)\s*\|\s*(.*?)\s*\|?\s*$", line)
    if m:
        f, st, note = m.group(1), m.group(2), m.group(3).rstrip("|").strip()
        f = re.sub(r"`", "", f); note = re.sub(r"`", "", note)
        f = re.sub(r"\*\*", "", f); note = re.sub(r"\*\*", "", note)
        if 8 < len(f) < 150: rows.append((f, st, note))

TEMPL_Q = ["What is the state of {f}?", "Does HermOS have this: {f}?", "Tell me about {f}.", "Is {f} working?"]
def ans(f, st, note):
    return f"{f}: {st}." + (f" {note}" if note else "")

# component proposals: the file-block format, with a minimal valid skeleton (what the Forge accepts)
def skeleton(n):
    return (f'<file path="src/app/{n}/main.cc">\n#include <base/component.h>\nvoid Component::construct(Genode::Env &) {{ }}\n</file>\n'
            f'<file path="src/app/{n}/target.mk">\nTARGET = {n}\nLIBS = base\nSRC_CC = main.cc\n</file>\n'
            f'<file path="src/app/{n}/manifest.hid">\n+ manifest\n name: {n}\n kind: rea\n + caps\n  vfs: /apps/{n},ro\n  nic: none\n  block: none\n  gui: cell\n + quotas\n  ram: 16M\n  cpu: 25\n</file>')
NAMES = ["clockcell", "notepad", "calcer", "weatherbox", "mediaview", "taskboard", "hexdump", "logview", "paintcell", "timer", "notes", "sysinfo",
         "diskmap", "netwatch", "colorpick", "unitconv"]
CREATE_Q = ["Create a component named {n}.", "Make a new component called {n}.", "Write the component {n}.", "I need a component {n}, build it."]
COMP = []
for n in NAMES:
    for t in random.sample(CREATE_Q, 3):
        COMP.append((t.format(n=n), skeleton(n)))

samples = []
for q, a in COMP:
    samples.append((q, a))
for q, a in FACTS:
    samples.append((q, a))
    samples.append((q.rstrip("?") + ", please?", a))
    samples.append(("Explain: " + q.rstrip("?").lower() + ".", a))
for f, st, note in rows:
    a = ans(f, st, note)
    for t in random.sample(TEMPL_Q, 2):
        samples.append((t.format(f=f), a))
random.shuffle(samples)
with open(out, "w", encoding="utf-8") as fh:
    for q, a in samples:
        fh.write(f"Q: {q}\nA: {a}\n\n")
print(len(FACTS), "facts,", len(rows), "status rows ->", len(samples), "samples,",
      sum(len(q) + len(a) for q, a in samples), "characters")
