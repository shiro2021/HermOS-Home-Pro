/*
 * Shiro Brain: the local language model of HermOS.
 *
 * A Genode libc component that runs llama.cpp (CPU) on a GGUF model from the image. It is a service of
 * the MyKee router, which sends it requests the same way it sends them to the cloud:
 *
 *   /brain/request   ROM     "<seq>\n<prompt>"   (written by mykee_router)
 *   /brain/response  report  "<seq>\n<answer>"   (read by mykee_router)
 *
 * The model is read into RAM (no mmap) from /model/shiro.gguf. One request at a time; greedy sampling.
 * If the model has a chat template it is applied, otherwise the prompt is used as it is.
 */

#include <libc/component.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#include "llama.h"

namespace {

	std::string read_file(char const *path)
	{
		int fd = open(path, O_RDONLY);
		if (fd < 0) return std::string();
		std::string out; char buf[1024]; ssize_t n;
		while ((n = read(fd, buf, sizeof(buf))) > 0) out.append(buf, static_cast<size_t>(n));
		close(fd);
		return out;
	}

	void write_file(char const *path, std::string const &data)
	{
		int fd = open(path, O_WRONLY | O_TRUNC);
		if (fd < 0) return;
		ssize_t const n = write(fd, data.data(), data.size());
		(void)n;
		close(fd);
	}

	void trim(std::string &s)
	{
		size_t const b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) { s.clear(); return; }
		size_t const e = s.find_last_not_of(" \t\r\n");
		s = s.substr(b, e - b + 1);
	}

	/* short printable excerpt for the log: prompts are private, replies are untrusted */
	std::string log_safe(std::string const &in, size_t max = 80)
	{
		std::string out;
		for (size_t i = 0; i < in.size() && out.size() < max; i++) {
			unsigned char const c = static_cast<unsigned char>(in[i]);
			out += (c < 0x20 || c == 0x7f) ? '?' : static_cast<char>(c);
		}
		if (in.size() > max) out += "...";
		return out;
	}

	enum { N_CTX = 2048, N_PREDICT = 640, N_THREADS = 2 };

	void set_tokens(llama_batch_ext *batch, llama_token const *tokens, int32_t n, llama_pos pos0)
	{
		llama_batch_ext_clear(batch);
		for (int32_t i = 0; i < n; ++i) {
			int32_t const idx = llama_batch_ext_add_token(batch, 0, tokens[i]);
			llama_pos const pos = pos0 + i;
			llama_batch_ext_set_pos(batch, idx, &pos);
		}
		llama_batch_ext_set_output_logits(batch, n - 1, true);
	}

	/* llama.cpp logs hundreds of lines while loading; only warnings and errors reach the serial log */
	void quiet_log(ggml_log_level level, char const *text, void *)
	{
		if (level == GGML_LOG_LEVEL_ERROR || level == GGML_LOG_LEVEL_WARN) fputs(text, stdout);
	}

	/* the reply goes to the UI as text: keep valid UTF-8, replace anything else */
	std::string sanitize(std::string const &in)
	{
		std::string out;
		for (size_t i = 0; i < in.size();) {
			unsigned char const c = static_cast<unsigned char>(in[i]);
			size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 0;
			bool ok = len > 0 && i + len <= in.size();
			for (size_t k = 1; ok && k < len; ++k) ok = (static_cast<unsigned char>(in[i + k]) & 0xc0) == 0x80;
			if (ok && (c >= 0x20 || c == '\n' || c == '\t')) { out.append(in, i, len); i += len; }
			else { if (c == '\r') {} else out += '?'; i += 1; }
		}
		return out;
	}

	struct Brain
	{
		llama_model   *model { nullptr };
		llama_context *ctx   { nullptr };
		llama_sampler *smpl  { nullptr };
		llama_vocab const *vocab { nullptr };
		std::string    tmpl;
		std::string    prompt_fmt;   /* optional /model/prompt.txt: the user text goes where {} stands */
		std::string    system_text;  /* optional /model/system.txt: said once before every request */

		bool load(char const *path)
		{
			llama_log_set(quiet_log, nullptr);
			llama_backend_init();

			llama_model_params mp = llama_model_default_params();
			mp.n_gpu_layers = 0;
			mp.load_mode    = LLAMA_LOAD_MODE_NONE;   /* read the file through the VFS into RAM, no mmap */
			mp.lazy_mode    = LLAMA_LAZY_MODE_OFF;

			model = llama_model_load_from_file(path, mp);
			if (!model) return false;
			vocab = llama_model_get_vocab(model);

			llama_context_params cp = llama_context_default_params();
			cp.n_ctx           = N_CTX;
			cp.n_batch         = 512;
			cp.n_threads       = N_THREADS;
			cp.n_threads_batch = N_THREADS;
			cp.no_perf         = true;
			ctx = llama_init_from_model(model, cp);
			if (!ctx) return false;

			llama_sampler_chain_params sp = llama_sampler_chain_default_params();
			sp.no_perf = true;
			smpl = llama_sampler_chain_init(sp);
			llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

			if (char const *t = llama_model_chat_template(model, nullptr)) tmpl = t;
			prompt_fmt  = read_file("/model/prompt.txt");
			system_text = read_file("/model/system.txt");
			return true;
		}

		/* apply the model's chat template to one user message; plain prompt if it has none */
		std::string format(std::string const &user) const
		{
			if (tmpl.empty()) {
				/* a model without a chat template: system text, then the format with the user text in it */
				if (!prompt_fmt.empty()) return user;       /* a facts-and-question model: already shaped by the caller */
				return system_text.empty() ? user : system_text + user;
			}
			llama_chat_message const msg { "user", user.c_str() };
			std::vector<char> buf(user.size() * 2 + 1024);
			int n = llama_chat_apply_template(tmpl.c_str(), &msg, 1, true, buf.data(), (int32_t)buf.size());
			if (n > (int)buf.size()) {
				buf.resize((size_t)n);
				n = llama_chat_apply_template(tmpl.c_str(), &msg, 1, true, buf.data(), (int32_t)buf.size());
			}
			return n < 0 ? user : std::string(buf.data(), (size_t)n);
		}

		std::string answer(std::string const &user)
		{
			std::string const prompt = format(user);

			int const n_tok = -llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(), nullptr, 0, true, true);
			if (n_tok <= 0) return "error: the prompt could not be tokenized";
			if (n_tok > N_CTX - 16) return "error: the prompt is too long for the local model";

			std::vector<llama_token> toks((size_t)n_tok);
			if (llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(), toks.data(), (int32_t)toks.size(), true, true) < 0)
				return "error: the prompt could not be tokenized";

			llama_memory_clear(llama_get_memory(ctx), true);
			llama_sampler_reset(smpl);

			std::string out;
			llama_batch_ext *batch = llama_batch_ext_init(ctx);
			set_tokens(batch, toks.data(), (int32_t)toks.size(), 0);
			int n_in_batch = (int)toks.size(), n_pos = 0;

			for (int i = 0; i < N_PREDICT; ++i) {
				if (llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch) != 0) {
					llama_batch_ext_free(batch);
					return out.empty() ? "error: the local model failed" : out;
				}
				n_pos += n_in_batch;

				llama_token next = llama_sampler_sample(smpl, ctx, -1);
				if (llama_vocab_is_eog(vocab, next)) break;

				char piece[256];
				int const n = llama_token_to_piece(vocab, next, piece, sizeof(piece), 0, true);
				if (n > 0) out.append(piece, (size_t)n);

				set_tokens(batch, &next, 1, n_pos);
				n_in_batch = 1;
			}
			llama_batch_ext_free(batch);
			return out;
		}
	};

} /* anonymous namespace */


void Libc::Component::construct(Libc::Env &env)
{
	(void)env;

	Libc::with_libc([&]() {

		printf("Shiro Brain starting\n");

		Brain brain;
		if (!brain.load("/model/shiro.gguf")) {
			printf("Shiro Brain: ERROR: cannot load /model/shiro.gguf; idle\n");
			return;
		}
		printf("Shiro Brain: model ready (context %d, %d threads)\n", (int)N_CTX, (int)N_THREADS);

		std::string last;

		for (;;) {

			std::string req = read_file("/brain/request");
			trim(req);

			if (!req.empty() && req != last) {
				last = req;

				size_t const nl = req.find('\n');
				std::string const seq    = nl == std::string::npos ? std::string("0") : req.substr(0, nl);
				std::string const prompt = nl == std::string::npos ? req : req.substr(nl + 1);
				printf("Shiro Brain: request %s (%u bytes): [%s]\n", log_safe(seq, 12).c_str(),
				       (unsigned)prompt.size(), log_safe(prompt).c_str());

				/*
				 * A model with a prompt sidecar answers from facts: the agent's retrieved facts become "C:" lines
				 * (the first two, as long as in training), then the question. A request to create a component
				 * gets no facts: it needs only the name.
				 */
				std::string question = prompt;
				std::string shaped;
				if (!brain.prompt_fmt.empty()) {
					std::vector<std::string> facts;
					static char const head[] = "Context (from the HermOS docs):\n", marker[] = "answer the user's question: ";
					size_t const m = question.rfind(marker);
					if (m != std::string::npos) {
						std::string const ctx = question.substr(0, m);
						question = question.substr(m + sizeof(marker) - 1);
						size_t pos = ctx.find(head) == std::string::npos ? 0 : ctx.find(head) + sizeof(head) - 1;
						while (pos < ctx.size() && facts.size() < 2) {
							size_t e = ctx.find("\n- ", pos + 1);
							std::string f = ctx.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
							if (f.compare(0, 2, "- ") == 0) f.erase(0, 2);
							while (!f.empty() && (f.back() == '\n' || f.back() == ' ')) f.pop_back();
							if (!f.empty() && f.find("Using the context above") == std::string::npos) facts.push_back(f);
							if (e == std::string::npos) break;
							pos = e + 1;
						}
					}
					trim(question);
					if (!question.empty() && question[0] >= 'a' && question[0] <= 'z') question[0] = char(question[0] - 32);
					if (!question.empty() && question.back() != '?' && question.back() != '.' && question.back() != '!') question += '?';

					std::string lower = question;
					for (auto &c : lower) if (c >= 'A' && c <= 'Z') c = char(c + 32);
					bool const create = lower.find("component") != std::string::npos &&
					    (lower.compare(0, 6, "create") == 0 || lower.compare(0, 4, "make") == 0 || lower.compare(0, 5, "write") == 0 ||
					     lower.compare(0, 3, "new") == 0 || lower.compare(0, 6, "i need") == 0);
					if (create) facts.clear();

					for (auto &f : facts) {                      /* the same cut as in training: at most 300 characters */
						if (f.size() > 300) {
							size_t p1 = f.rfind(". ", 300), p2 = f.rfind("; ", 300);
							size_t p = p1 == std::string::npos ? p2 : (p2 == std::string::npos ? p1 : (p1 > p2 ? p1 : p2));
							f = (p != std::string::npos && p > 150) ? f.substr(0, p + 1) : f.substr(0, f.rfind(' ', 300));
						}
						shaped += "C: " + f + "\n";
					}
					shaped += "Q: " + question + "\nA:";
				}
				std::string const out = brain.answer(brain.prompt_fmt.empty() ? question : shaped);
				printf("Shiro Brain: answer (%u bytes): [%s]\n", (unsigned)out.size(), log_safe(out).c_str());

				write_file("/brain/response", seq + "\n" + sanitize(out));
			}

			struct timespec const ts = { 0, 200 * 1000 * 1000 };
			nanosleep(&ts, nullptr);
		}
	});
}
