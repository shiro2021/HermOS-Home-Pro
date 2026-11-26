#!/usr/bin/env python3
"""
Train "Shiro-Mini": a small llama-architecture language model, from scratch, on the HermOS Q/A material,
and write it as a GGUF that llama.cpp (and the Shiro Brain component) can load.

  train_mini.py DATA.txt OUT.gguf [steps] [gguf-py dir]

It is a stand-in until a real base model is chosen: it repeats and recombines what is in the training
text, it does not reason and it cannot write components. Needs: jax, numpy. The tokenizer is the one in
make_dummy_model.py (3 specials, 256 byte tokens, printable ASCII, the SPM space), so llama.cpp tokenizes
exactly like this script does.
"""
import sys, time, math
import numpy as np
import jax, jax.numpy as jnp

data_path, out = sys.argv[1], sys.argv[2]
steps = int(sys.argv[3]) if len(sys.argv) > 3 else 2500
if len(sys.argv) > 4:
    sys.path.insert(0, sys.argv[4])
import gguf

# ---- vocabulary, identical to make_dummy_model.py
tokens, scores, types = ["<unk>", "<s>", "</s>"], [0.0, 0.0, 0.0], [2, 3, 3]
for b in range(256):
    tokens.append("<0x%02X>" % b); scores.append(0.0); types.append(6)
chars = [chr(c) for c in range(33, 127)] + ["▁"]
for c in chars:
    tokens.append(c); scores.append(-1.0); types.append(1)
V = len(tokens)
tok_id = {t: i for i, t in enumerate(tokens)}

def encode(text):
    ids = []
    for ch in text:
        if ch == " ":
            ids.append(tok_id["▁"])
        elif ch in tok_id and 33 <= ord(ch) < 127:
            ids.append(tok_id[ch])
        else:
            for b in ch.encode("utf-8"):
                ids.append(tok_id["<0x%02X>" % b])
    return ids

BOS, EOS = 1, 2
text = open(data_path, encoding="utf-8").read()
samples = [s.strip() for s in text.split("\n\n") if s.strip()]
stream = []
for s in samples:
    stream += [BOS] + encode(s) + [EOS]
stream = np.array(stream, dtype=np.int32)
print("samples", len(samples), "tokens", len(stream), "vocab", V, flush=True)

# ---- model
D, L, H, F, T = 192, 4, 4, 512, 576
HD = D // H
rng = np.random.default_rng(3)

def w(*shape, scale=0.02):
    return (rng.normal(0, scale, shape)).astype(np.float32)

params = {"emb": w(V, D), "norm": np.ones(D, np.float32), "out": w(V, D), "layers": []}
for _ in range(L):
    params["layers"].append({
        "an": np.ones(D, np.float32), "wq": w(D, D), "wk": w(D, D), "wv": w(D, D), "wo": w(D, D, scale=0.02 / math.sqrt(2 * L)),
        "fn": np.ones(D, np.float32), "wg": w(F, D), "wu": w(F, D), "wd": w(D, F, scale=0.02 / math.sqrt(2 * L))})
params = jax.tree_util.tree_map(jnp.asarray, params)
print("parameters", sum(int(np.prod(p.shape)) for p in jax.tree_util.tree_leaves(params)), flush=True)

def rms(x, g):
    return x * jax.lax.rsqrt(jnp.mean(x * x, -1, keepdims=True) + 1e-5) * g

# RoPE on adjacent pairs (x[2i], x[2i+1]): the "norm" rope type of llama.cpp's llama architecture
inv = 1.0 / (10000.0 ** (jnp.arange(0, HD, 2) / HD))

def rope(x):                      # x: (B, T, H, HD)
    t = jnp.arange(x.shape[1])[:, None] * inv[None, :]
    c, s = jnp.cos(t)[None, :, None, :], jnp.sin(t)[None, :, None, :]
    x1, x2 = x[..., 0::2], x[..., 1::2]
    r1, r2 = x1 * c - x2 * s, x1 * s + x2 * c
    return jnp.stack([r1, r2], -1).reshape(x.shape)

def forward(p, ids):
    B, Tn = ids.shape
    x = p["emb"][ids]
    mask = jnp.tril(jnp.ones((Tn, Tn), bool))
    for lp in p["layers"]:
        h = rms(x, lp["an"])
        q = (h @ lp["wq"].T).reshape(B, Tn, H, HD)
        k = (h @ lp["wk"].T).reshape(B, Tn, H, HD)
        v = (h @ lp["wv"].T).reshape(B, Tn, H, HD)
        q, k = rope(q), rope(k)
        att = jnp.einsum("bthd,bshd->bhts", q, k) / math.sqrt(HD)
        att = jnp.where(mask[None, None], att, -1e30)
        att = jax.nn.softmax(att, -1)
        o = jnp.einsum("bhts,bshd->bthd", att, v).reshape(B, Tn, D)
        x = x + o @ lp["wo"].T
        h = rms(x, lp["fn"])
        x = x + (jax.nn.silu(h @ lp["wg"].T) * (h @ lp["wu"].T)) @ lp["wd"].T
    return rms(x, p["norm"]) @ p["out"].T

def loss_fn(p, ids):
    logits = forward(p, ids[:, :-1])
    logp = jax.nn.log_softmax(logits, -1)
    tgt = ids[:, 1:]
    nll = -jnp.take_along_axis(logp, tgt[..., None], -1)[..., 0]
    return nll.mean()

# ---- AdamW, by hand (no optax needed)
m = jax.tree_util.tree_map(jnp.zeros_like, params)
v2 = jax.tree_util.tree_map(jnp.zeros_like, params)
LR, B1, B2, WD = 3e-3, 0.9, 0.99, 0.01

@jax.jit
def step(p, m, v2, ids, lr, t):
    l, g = jax.value_and_grad(loss_fn)(p, ids)
    gn = jnp.sqrt(sum(jnp.sum(x * x) for x in jax.tree_util.tree_leaves(g)))
    g = jax.tree_util.tree_map(lambda x: x * jnp.minimum(1.0, 1.0 / (gn + 1e-6)), g)
    m = jax.tree_util.tree_map(lambda a, b: B1 * a + (1 - B1) * b, m, g)
    v2 = jax.tree_util.tree_map(lambda a, b: B2 * a + (1 - B2) * b * b, v2, g)
    def upd(pp, mm, vv):
        mh, vh = mm / (1 - B1 ** t), vv / (1 - B2 ** t)
        return pp - lr * (mh / (jnp.sqrt(vh) + 1e-8) + WD * pp)
    p = jax.tree_util.tree_map(upd, p, m, v2)
    return p, m, v2, l

BATCH = 8
t0 = time.time()
for i in range(1, steps + 1):
    starts = rng.integers(0, len(stream) - T - 1, BATCH)
    ids = jnp.asarray(np.stack([stream[s:s + T + 1] for s in starts]))
    lr = LR * min(1.0, i / 100) * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * i / steps)))
    params, m, v2, l = step(params, m, v2, ids, lr, float(i))
    if i % 50 == 0 or i == 1:
        print(f"step {i}/{steps} loss {float(l):.3f} {time.time() - t0:.0f}s", flush=True)

# ---- GGUF
P = jax.tree_util.tree_map(np.asarray, params)
wr = gguf.GGUFWriter(out, "llama")
wr.add_name("shiro-mini")
wr.add_context_length(T)
wr.add_embedding_length(D); wr.add_block_count(L); wr.add_feed_forward_length(F)
wr.add_head_count(H); wr.add_head_count_kv(H); wr.add_layer_norm_rms_eps(1e-5)
wr.add_rope_dimension_count(HD); wr.add_vocab_size(V)
wr.add_tokenizer_model("llama"); wr.add_token_list(tokens); wr.add_token_scores(scores); wr.add_token_types(types)
wr.add_bos_token_id(1); wr.add_eos_token_id(2); wr.add_unk_token_id(0)
wr.add_add_bos_token(True); wr.add_add_space_prefix(False)
wr.add_tensor("token_embd.weight", P["emb"])
wr.add_tensor("output_norm.weight", P["norm"])
wr.add_tensor("output.weight", P["out"])
for i, lp in enumerate(P["layers"]):
    wr.add_tensor(f"blk.{i}.attn_norm.weight", lp["an"])
    for src, dst in (("wq", "attn_q"), ("wk", "attn_k"), ("wv", "attn_v"), ("wo", "attn_output")):
        wr.add_tensor(f"blk.{i}.{dst}.weight", lp[src])
    wr.add_tensor(f"blk.{i}.ffn_norm.weight", lp["fn"])
    wr.add_tensor(f"blk.{i}.ffn_gate.weight", lp["wg"])
    wr.add_tensor(f"blk.{i}.ffn_up.weight", lp["wu"])
    wr.add_tensor(f"blk.{i}.ffn_down.weight", lp["wd"])
wr.write_header_to_file(); wr.write_kv_data_to_file(); wr.write_tensors_to_file(); wr.close()
print("wrote", out)
