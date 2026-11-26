#!/usr/bin/env python3
"""
Build the Shiro knowledge base from the HermOS docs, the status table, the curated facts and the code.

  build_kb.py HERMOS_DIR OUT_DIR        (HERMOS_DIR is repos/hermos)

writes OUT_DIR/kb.txt   one fact per paragraph: what the agent indexes and retrieves from the image
       OUT_DIR/kb.json  the same facts with the questions that ask for them: what the trainer uses
"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from facts import FACTS

repo, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
items = []   # {"text": ..., "questions": [...]}

def clean(t):
    t = re.sub(r"`", "", t); t = re.sub(r"\*\*", "", t); t = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", t)
    return re.sub(r"\s+", " ", t).strip()

def cut(t, n=300):
    if len(t) <= n: return t
    p = max(t.rfind(". ", 0, n), t.rfind("; ", 0, n))
    return t[:p + 1] if p > n // 2 else t[:n].rsplit(" ", 1)[0] + "."

# 1. curated facts
for q, a in FACTS:
    # the chunk carries its topic (the question), so retrieval can find it; the answer is what the model says
    items.append({"text": cut(f"{q} {a}", 300), "answer": a, "questions": [q]})

# 2. the status table: one fact per row
TEMPL = ["What is the state of {f}?", "Does HermOS have {f}?", "Is {f} working?", "Tell me about {f}."]
txt = open(f"{repo}/doc/FUNCTIONS.md", encoding="utf-8").read()
for line in txt.splitlines():
    m = re.match(r"\|\s*(.+?)\s*\|\s*(VERIFIED|BUILT|STUB|VISUAL|ABSENT|SOURCE ONLY|PARTIAL|BLOCKED)\s*\|\s*(.*?)\s*\|?\s*$", line)
    if m:
        f, st, note = clean(m.group(1)), m.group(2), clean(m.group(3).rstrip("|"))
        if 8 < len(f) < 150:
            items.append({"text": cut(f"{f}: {st}." + (f" {note}" if note else "")), "questions": [t.format(f=f) for t in TEMPL]})

# 3. prose of the design documents: heading + first paragraph
for name in ("DESIGN.md", "SCENE.md", "HERMES_DIRECTOR.md", "INSTALLER.md", "BUILD.md", "HARDWARE_SUPPORT.md", "ROADMAP_V7.md"):
    p = f"{repo}/doc/{name}"
    if not os.path.exists(p): continue
    heading, para, fence = None, [], False
    def flush():
        global para
        if heading and para:
            body = clean(" ".join(para))
            h = re.sub(r"^[\d.\s]+", "", clean(heading))
            if len(body) > 60 and 3 < len(h) < 80 and "|" not in body[:40]:
                items.append({"text": cut(f"{h}. {body}"), "questions": [f"What is {h}?", f"Tell me about {h}.", f"Explain {h}."]})
        para = []
    for line in open(p, encoding="utf-8"):
        if line.startswith("```"): fence = not fence; continue
        if fence: continue
        if line.startswith("#"): flush(); heading = line.lstrip("# ").strip(); continue
        if line.startswith("|") or line.startswith("---") : continue
        if not line.strip():
            if para: flush_text = True
            continue
        if len(para) < 3: para.append(line.strip().lstrip("-* "))
    flush()

# 4. the code: what each component says about itself
srcdir = f"{repo}/src/app"
for comp in sorted(os.listdir(srcdir)):
    mc = f"{srcdir}/{comp}/main.cc"
    if not os.path.exists(mc): continue
    head = open(mc, encoding="utf-8", errors="ignore").read(1800)
    m = re.search(r"/\*+\s*(.*?)\*/", head, re.S)
    if not m: continue
    lines = [re.sub(r"^\s*\*\s?", "", l).strip() for l in m.group(1).splitlines()]
    lines = [l for l in lines if l and not l.startswith("\\") and not l.startswith("*")]
    if not lines: continue
    desc = clean(" ".join(lines[:3]))
    if len(desc) > 30:
        items.append({"text": cut(f"{comp} is a HermOS component. {desc}"),
                      "questions": [f"What is {comp}?", f"What does {comp} do?", f"Tell me about the {comp} component."]})

# drop duplicates by text
seen, uniq = set(), []
for it in items:
    if it["text"] in seen: continue
    seen.add(it["text"]); uniq.append(it)
open(f"{out}/kb.txt", "w", encoding="utf-8").write("\n\n".join(i["text"] for i in uniq) + "\n")
for it in uniq: it.setdefault("answer", it["text"])
json.dump(uniq, open(f"{out}/kb.json", "w"), ensure_ascii=False, indent=0)
print(len(uniq), "facts,", sum(len(i["text"]) for i in uniq), "characters")
