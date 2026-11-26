// Host tool: answer one shaped prompt (read from stdin) with the same decoder the on-device brain uses.
// Build: see eval_model.py. Usage: shiro_ask MODEL.gguf < prompt.txt
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include "../../repos/hermos/src/app/shiro_brain/shiro_decode.h"

int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	llama_log_set([](ggml_log_level, char const *, void *) { }, nullptr);
	llama_backend_init();
	llama_model_params mp = llama_model_default_params();
	mp.n_gpu_layers = 0;
	llama_model *model = llama_model_load_from_file(argv[1], mp);
	if (!model) return 3;
	llama_context_params cp = llama_context_default_params();
	cp.n_ctx = 2048; cp.n_batch = 1024; cp.n_threads = 2; cp.n_threads_batch = 2; cp.no_perf = true;
	llama_context *ctx = llama_init_from_model(model, cp);
	if (!ctx) return 4;
	Shiro::Decoder d; d.model = model; d.ctx = ctx; d.vocab = llama_model_get_vocab(model); d.init();
	if (std::getenv("SHIRO_RERANK")) d.rerank = true;
	if (char const *u = std::getenv("SHIRO_UNK")) d.unknown_below = std::atof(u);
	std::string prompt((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
	std::string const a = d.answer(prompt, 400, 1900);
	std::fwrite(a.data(), 1, a.size(), stdout);
	return 0;
}
