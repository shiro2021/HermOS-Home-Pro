#!/usr/bin/env python3
"""
Write a tiny random llama-architecture GGUF (2 layers, 64 wide, ~350-token vocabulary).

It exists to test the plumbing of the Shiro Brain (load, tokenize, decode, report) without real weights.
The text it generates is meaningless. Usage: make_dummy_model.py OUT.gguf [gguf-py dir]
"""
import sys
import numpy as np

out = sys.argv[1]
if len(sys.argv) > 2:
    sys.path.insert(0, sys.argv[2])
import gguf

rng = np.random.default_rng(7)
n_embd, n_ff, n_layer, n_head, n_ctx = 64, 128, 2, 4, 512

# vocabulary: <unk> <s> </s>, 256 byte tokens, then printable single characters and the SPM space
tokens, scores, types = ["<unk>", "<s>", "</s>"], [0.0, 0.0, 0.0], [2, 3, 3]
for b in range(256):
    tokens.append("<0x%02X>" % b); scores.append(0.0); types.append(6)
chars = [chr(c) for c in range(33, 127)] + ["▁"]
for c in chars:
    tokens.append(c); scores.append(-1.0); types.append(1)
n_vocab = len(tokens)

w = gguf.GGUFWriter(out, "llama")
w.add_name("shiro-dummy")
w.add_context_length(n_ctx)
w.add_embedding_length(n_embd)
w.add_block_count(n_layer)
w.add_feed_forward_length(n_ff)
w.add_head_count(n_head)
w.add_head_count_kv(n_head)
w.add_layer_norm_rms_eps(1e-5)
w.add_rope_dimension_count(n_embd // n_head)
w.add_vocab_size(n_vocab)
w.add_tokenizer_model("llama")
w.add_token_list(tokens)
w.add_token_scores(scores)
w.add_token_types(types)
w.add_bos_token_id(1)
w.add_eos_token_id(2)
w.add_unk_token_id(0)
w.add_add_bos_token(True)

def t(name, *shape):
    w.add_tensor(name, rng.normal(0, 0.02, shape).astype(np.float32))

def ones(name, n):
    w.add_tensor(name, np.ones(n, dtype=np.float32))

t("token_embd.weight", n_vocab, n_embd)
ones("output_norm.weight", n_embd)
t("output.weight", n_vocab, n_embd)
for i in range(n_layer):
    ones(f"blk.{i}.attn_norm.weight", n_embd)
    for p in ("attn_q", "attn_k", "attn_v", "attn_output"):
        t(f"blk.{i}.{p}.weight", n_embd, n_embd)
    ones(f"blk.{i}.ffn_norm.weight", n_embd)
    t(f"blk.{i}.ffn_gate.weight", n_ff, n_embd)
    t(f"blk.{i}.ffn_up.weight", n_ff, n_embd)
    t(f"blk.{i}.ffn_down.weight", n_embd, n_ff)

w.write_header_to_file()
w.write_kv_data_to_file()
w.write_tensors_to_file()
w.close()
print("wrote", out)
