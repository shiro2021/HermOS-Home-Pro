/*
 * Shiro Brain: answering from facts, by constrained copying.
 *
 * The prompt carries the retrieved facts as "C: <fact>" lines, then "Q: <question>" and "A:". The model decides
 * WHICH part of the facts answers the question and where to stop, but it may only emit bytes that continue a
 * real span of those facts (or of the fixed "I do not know" sentence): at every step the allowed next bytes
 * are those that follow every place in the facts where the text written so far still matches. So an answer is
 * always made of the knowledge base's own words and a small model cannot garble it.
 *
 * A prompt without "C:" lines (a request to create a component) is decoded freely. Used by the on-device
 * component (shiro_brain) and by the host test tool, so both behave the same.
 */
#ifndef _SHIRO_DECODE_H_
#define _SHIRO_DECODE_H_

#include <string>
#include <vector>
#include <cstring>
#include <cmath>

#include "llama.h"

namespace Shiro {

static char const UNKNOWN_SENTENCE[] = "I do not know that about HermOS.";

/* the "C:" lines of a shaped prompt */
inline std::vector<std::string> facts_of(std::string const &prompt)
{
	std::vector<std::string> out;
	size_t pos = 0;
	while (pos < prompt.size()) {
		size_t e = prompt.find('\n', pos);
		if (e == std::string::npos) e = prompt.size();
		if (prompt.compare(pos, 3, "C: ") == 0) out.push_back(prompt.substr(pos + 3, e - pos - 3));
		pos = e + 1;
	}
	return out;
}

struct Decoder
{
	llama_model        *model { nullptr };
	llama_context      *ctx   { nullptr };
	llama_vocab const  *vocab { nullptr };
	llama_token         byte_token[256];
	llama_token         space_token { -1 };
	bool                rerank { false };         /* score whole openings instead of choosing the first byte (measured worse) */
	double              unknown_below { -2.0 };   /* say "I do not know" only when no fact fits better than this mean log-probability */

	void init()
	{
		for (auto &t : byte_token) t = -1;
		int const n = llama_vocab_n_tokens(vocab);
		/* ascending ids: the single-character tokens (after the 256 byte tokens) win for printable ASCII */
		for (int id = 3; id < n; ++id) {
			char piece[16];
			int const len = llama_token_to_piece(vocab, id, piece, sizeof(piece), 0, false);
			if (len == 1) byte_token[(unsigned char)piece[0]] = id;
		}
		space_token = byte_token[(unsigned char)' '];
	}

	/* one decoding step: feed `tokens` and return the logits for the next token */
	float const *step(std::vector<llama_token> const &tokens, int &n_pos)
	{
		llama_batch_ext *batch = llama_batch_ext_init(ctx);
		llama_batch_ext_clear(batch);
		for (size_t i = 0; i < tokens.size(); ++i) {
			int32_t const idx = llama_batch_ext_add_token(batch, 0, tokens[i]);
			llama_pos const pos = n_pos + (llama_pos)i;
			llama_batch_ext_set_pos(batch, idx, &pos);
		}
		llama_batch_ext_set_output_logits(batch, (int32_t)tokens.size() - 1, true);
		int const rc = llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch);
		llama_batch_ext_free(batch);
		if (rc != 0) return nullptr;
		n_pos += (int)tokens.size();
		return llama_get_logits_ith(ctx, -1);
	}

	/* the highest-scoring token among `allowed` bytes (and the end token when `eos_ok`); -1 = none */
	llama_token pick(float const *logits, bool const *allowed, bool eos_ok) const
	{
		llama_token best = -1; float bv = -1e30f;
		for (int b = 0; b < 256; ++b)
			if (allowed[b] && byte_token[b] >= 0 && logits[byte_token[b]] > bv) { bv = logits[byte_token[b]]; best = byte_token[b]; }
		if (eos_ok) {
			llama_token const e = llama_vocab_eos(vocab);
			if (e >= 0 && logits[e] > bv) { bv = logits[e]; best = e; }
		}
		return best;
	}

	/* mean log-probability the model gives to `text` (one byte per token) right after the prompt state, which is
	 * left as it was. `first` are the logits for the first byte (the lead space comes before `text`). */
	double score(std::string const &text, int n_pos, float const *first)
	{
		if (text.empty() || space_token < 0) return -1e9;
		std::vector<llama_token> toks;
		toks.push_back(space_token);
		for (char c : text) { llama_token const t = byte_token[(unsigned char)c]; if (t < 0) return -1e9; toks.push_back(t); }

		llama_batch_ext *batch = llama_batch_ext_init(ctx);
		llama_batch_ext_clear(batch);
		for (size_t i = 0; i < toks.size(); ++i) {
			int32_t const idx = llama_batch_ext_add_token(batch, 0, toks[i]);
			llama_pos const pos = n_pos + (llama_pos)i;
			llama_batch_ext_set_pos(batch, idx, &pos);
			llama_batch_ext_set_output_logits(batch, idx, true);
		}
		int const rc = llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch);
		llama_batch_ext_free(batch);
		if (rc != 0) return -1e9;

		int const nv = llama_vocab_n_tokens(vocab);
		auto logp = [&](float const *lg, llama_token t) {
			float mx = -1e30f; for (int v = 0; v < nv; ++v) if (lg[v] > mx) mx = lg[v];
			double z = 0; for (int v = 0; v < nv; ++v) z += std::exp((double)lg[v] - mx);
			return (double)lg[t] - mx - std::log(z);
		};
		double total = logp(first, toks[0]);               /* the lead space */
		for (size_t i = 0; i + 1 < toks.size(); ++i) total += logp(llama_get_logits_ith(ctx, (int32_t)i), toks[i + 1]);
		llama_memory_seq_rm(llama_get_memory(ctx), 0, n_pos, -1);   /* forget the trial tokens */
		return total / (double)toks.size();
	}

	std::string answer(std::string const &prompt, int max_chars, int n_ctx_free)
	{
		std::vector<std::string> sources = facts_of(prompt);
		bool const constrained = !sources.empty();
		if (constrained) sources.push_back(UNKNOWN_SENTENCE);

		int const n_tok = -llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(), nullptr, 0, true, true);
		if (n_tok <= 0) return "error: the prompt could not be tokenized";
		if (n_tok > n_ctx_free) return "error: the prompt is too long for the local model";
		std::vector<llama_token> toks((size_t)n_tok);
		if (llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(), toks.data(), (int32_t)toks.size(), true, true) < 0)
			return "error: the prompt could not be tokenized";

		llama_memory_clear(llama_get_memory(ctx), true);
		int n_pos = 0;
		float const *logits = step(toks, n_pos);
		if (!logits) return "error: the local model failed";

		std::string out;
		struct Pos { int s; size_t p; };
		std::vector<Pos> live;                      /* places in the facts where the text so far still matches */
		if (constrained) {
			/* every sentence start in the facts is a candidate; a fact that begins with a question is not quoted
			 * from that question. The model scores each candidate's opening words and the best one is used. */
			struct Cand { int s; size_t p; double sc; };
			std::vector<Cand> cands;
			for (int s = 0; s < (int)sources.size(); ++s) {
				std::string const &src = sources[(size_t)s];
				size_t const qend = src.find("? ");
				bool const starts_with_question = qend != std::string::npos && qend < 160 && src.find(". ") > qend;
				for (size_t p = 0; p < src.size(); ++p) {
					bool const start = p == 0 || (p >= 2 && src[p - 1] == ' ' && (src[p - 2] == '.' || src[p - 2] == '?' || src[p - 2] == '!' || src[p - 2] == ':'));
					if (!start || src[p] == ' ') continue;
					if (starts_with_question && p == 0) continue;
					cands.push_back({ s, p, 0.0 });
				}
			}
			if (!rerank) {
				for (auto const &c : cands) live.push_back({ c.s, c.p });   /* the model chooses the first byte itself */
				cands.clear();
			}
			if (cands.size() > 40) cands.resize(40);
			if (rerank) {
			int best = -1; double bs = -1e30, unk = -1e30; int unk_i = -1;
			for (size_t i = 0; i < cands.size(); ++i) {
				std::string const &src = sources[(size_t)cands[i].s];
				cands[i].sc = score(src.substr(cands[i].p, 20), n_pos, logits);
				if (src == UNKNOWN_SENTENCE) { unk = cands[i].sc; unk_i = (int)i; continue; }   /* judged apart from the facts */
				if (cands[i].sc > bs) { bs = cands[i].sc; best = (int)i; }
			}
			/* the fixed sentence is chosen only when no fact fits */
			if (best < 0 || (bs < unknown_below && unk_i >= 0 && unk > bs)) best = unk_i;
			if (best < 0) return std::string(UNKNOWN_SENTENCE);
			/* from here on only the chosen place and the places that read the same way are alive */
			std::string const open = sources[(size_t)cands[(size_t)best].s].substr(cands[(size_t)best].p, 20);
			for (auto const &c : cands)
				if (sources[(size_t)c.s].compare(c.p, open.size(), open) == 0) live.push_back({ c.s, c.p });
			/* feed the lead space and the opening to the model, as if it had written them */
			std::vector<llama_token> tk; tk.push_back(space_token);
			for (char c : open) tk.push_back(byte_token[(unsigned char)c]);
			logits = step(tk, n_pos);
			if (!logits) return "error: the local model failed";
			out = " " + open;
			for (auto &lp : live) lp.p += open.size();
			}
		}

		bool lead = out.empty();                    /* the answer begins with one space after "A:" */
		for (int i = 0; i < max_chars; ++i) {
			llama_token next;
			if (lead && space_token >= 0) {
				next = space_token; lead = false;
			} else if (constrained) {
				bool allowed[256] = { }; bool eos_ok = false;
				for (auto const &lp : live) {
					std::string const &src = sources[(size_t)lp.s];
					if (lp.p < src.size()) allowed[(unsigned char)src[lp.p]] = true;
					else eos_ok = true;                                 /* the end of a fact */
				}
				if (!out.empty()) { char const c = out.back(); if (c == '.' || c == '!' || c == '?') eos_ok = true; }
				if (out.find_first_not_of(' ') == std::string::npos) eos_ok = false;
				next = pick(logits, allowed, eos_ok);
				if (next < 0) break;
			} else {
				float bv = -1e30f; next = -1;
				int const n = llama_vocab_n_tokens(vocab);
				for (int id = 0; id < n; ++id) if (logits[id] > bv) { bv = logits[id]; next = id; }
			}
			if (next == llama_vocab_eos(vocab) || llama_vocab_is_eog(vocab, next)) break;

			char piece[16];
			int const len = llama_token_to_piece(vocab, next, piece, sizeof(piece), 0, false);
			if (len <= 0) break;
			if (!constrained || !out.empty() || piece[0] != ' ') {      /* the lead space is not part of the match */
				if (constrained && !(out.empty() && piece[0] == ' ')) {
					std::vector<Pos> nl;
					for (auto const &lp : live) {
						std::string const &src = sources[(size_t)lp.s];
						if (lp.p < src.size() && src[lp.p] == piece[0]) nl.push_back({ lp.s, lp.p + 1 });
					}
					live.swap(nl);
				}
			}
			out.append(piece, (size_t)len);
			if (constrained && live.empty()) break;
			logits = step({ next }, n_pos);
			if (!logits) break;
		}
		size_t b = out.find_first_not_of(' ');
		return b == std::string::npos ? std::string() : out.substr(b);
	}
};

} /* namespace Shiro */

#endif /* _SHIRO_DECODE_H_ */
