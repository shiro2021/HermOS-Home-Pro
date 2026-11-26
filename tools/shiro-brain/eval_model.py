#!/usr/bin/env python3
"""
Score the Shiro Brain pipeline on eval_questions.txt: retrieval (the agent's BM25), the prompt the brain
builds, and the model (host llama-completion).

  eval_model.py MODEL.gguf LLAMA_COMPLETION KB.json [N_PREDICT]

An answer counts as correct when it contains every keyword of its line. The questions are loosely worded and
are not in the training data. Prints each miss, the retrieval hit-rate and the score.
"""
import os, subprocess, sys, json
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here + "/../../repos/hermos/tools/shiro-brain")
import shiro_lib

model, exe, kb_path = sys.argv[1], sys.argv[2], sys.argv[3]
n = sys.argv[4] if len(sys.argv) > 4 else "160"
kb = json.load(open(kb_path, encoding="utf-8"))
chunks = [it["text"] for it in kb]
index = shiro_lib.Index(chunks)

ok = ret_ok = total = 0
for line in open(f"{here}/eval_questions.txt", encoding="utf-8"):
    if not line.strip() or line.startswith("#"):
        continue
    q, kw = [x.strip() for x in line.split("|")]
    kws = [k.strip().lower() for k in kw.split(";")]
    hits = [i for i, _ in index.search(shiro_lib.normalize(q))]
    ctx = [chunks[i] for i in hits]
    retrieved = all(any(k in c.lower() for c in ctx) for k in kws)
    prompt = shiro_lib.prompt(q, ctx)
    out = subprocess.run([exe, "-m", model, "-p", prompt, "-n", n, "--no-warmup", "-no-cnv", "--temp", "0"],
                         capture_output=True, text=True, timeout=600).stdout
    ans = out[len(prompt):] if out.startswith(prompt) else out.rsplit("A:", 1)[-1]
    ans = ans.split("[end of text]")[0].strip()
    hit = all(k in ans.lower() for k in kws)
    total += 1; ok += hit; ret_ok += retrieved
    if not hit:
        print(f"MISS  {q!r}  want {kw!r}  retrieved={'yes' if retrieved else 'NO'}  got {ans[:100]!r}")
print(f"retrieval found the facts for {ret_ok}/{total}")
print(f"score {ok}/{total}")
