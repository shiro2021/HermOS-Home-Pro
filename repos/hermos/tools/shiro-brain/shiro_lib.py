"""
Shared by the trainer and the evaluation: the tokenizer, the SAME BM25 retrieval as the agent
(hermes_agent_cpp/shiro_rag.h), question paraphrases, and the prompt layout the Shiro Brain builds.
"""
import math, random, re

K_CTX, CTX_CHARS = 2, 300

STOP = {"what", "is", "are", "the", "a", "an", "do", "does", "i", "how", "to", "of", "it", "in", "on", "for", "and", "or", "you",
        "can", "me", "my", "tell", "about", "please", "whats", "there", "s"}

def stem(t):
    return t[:-1] if len(t) > 3 and t.endswith("s") and not t.endswith("ss") else t

def tokenize(s):
    """Lower-case words, without the stopwords above, plural 's' removed (the same in shiro_rag.h)."""
    return [stem(t) for t in re.findall(r"[a-z0-9]+", s.lower()) if t not in STOP]

class Index:
    """BM25 exactly as shiro_rag.h: k1 1.5, b 0.75, document length = distinct terms, average = tokens/doc."""
    def __init__(self, chunks):
        self.chunks = chunks
        self.tf = []; self.df = {}; self.total = 0
        for c in chunks:
            toks = tokenize(c); tf = {}
            for t in toks: tf[t] = tf.get(t, 0) + 1
            self.tf.append(tf); self.total += len(toks)
            for t in tf: self.df[t] = self.df.get(t, 0) + 1
    def search(self, query, k=K_CTX):
        n = len(self.chunks); avg = self.total / n if n else 1.0
        scored = []
        for i in range(n):
            sc = 0.0; ln = len(self.tf[i])
            for q in tokenize(query):
                d = self.df.get(q)
                if not d: continue
                idf = math.log((n - d + 0.5) / (d + 0.5) + 1.0)
                tf = self.tf[i].get(q, 0)
                sc += idf * (tf * 2.5) / (tf + 1.5 * (0.25 + 0.75 * ln / (avg or 1.0)))
            if sc > 0: scored.append((i, sc))
        scored.sort(key=lambda x: -x[1])
        return scored[:k]

def normalize(q):
    """What the brain does to a typed question before the model sees it."""
    q = q.strip()
    if q and q[0].islower(): q = q[0].upper() + q[1:]
    if q and q[-1] not in "?.!": q += "?"
    return q

def ctx_cut(t):
    if len(t) <= CTX_CHARS: return t
    p = max(t.rfind(". ", 0, CTX_CHARS), t.rfind("; ", 0, CTX_CHARS))
    return t[:p + 1] if p > CTX_CHARS // 2 else t[:CTX_CHARS].rsplit(" ", 1)[0]

def prompt(question, contexts):
    """C: lines, then the question: the layout in shiro-*.prompt and in the brain."""
    return "".join(f"C: {ctx_cut(c)}\n" for c in contexts[:K_CTX]) + f"Q: {normalize(question)}\nA:"

def paraphrases(q, rng):
    """Loose wordings of a question, as people type them."""
    base = q.rstrip("?.! ")
    low = base.lower()
    outs = {q, base, low}
    low2 = re.sub(r"meta\+(\w)", r"meta \1", low)
    outs.add(low2)
    m = re.match(r"(?i)what is (the )?(.*)", base)
    if m:
        x = m.group(2)
        outs |= {f"whats {x}", f"what's {x}", f"define {x}", f"explain {x}", f"what is {x} exactly", f"tell me what {x} is"}
    m = re.match(r"(?i)how do i (.*)", base)
    if m:
        x = m.group(1)
        outs |= {f"how to {x}", f"how can i {x}", f"i want to {x}", f"way to {x}"}
    m = re.match(r"(?i)does hermos (have|support|need|run|keep|use) (.*)", base)
    if m:
        v, x = m.group(1), m.group(2)
        outs |= {f"is there {x}", f"do you {v} {x}", f"can hermos {v} {x}"}
    m = re.match(r"(?i)what does (.*) do", base)
    if m:
        x = m.group(1)
        outs |= {f"what is {x} for", f"{x} does what", f"purpose of {x}"}
    outs |= {f"tell me: {low}", f"hey shiro {low}", f"{low} please", f"quick question {low}"}
    return sorted(outs)
