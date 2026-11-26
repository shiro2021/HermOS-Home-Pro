/*
 * HermOS AI agent — native C++ implementation (Plan B).
 *
 * A standard Genode *libc* component.  IPC all goes through the VFS/libc:
 *
 *   * `ui_intent` (the user prompt) arrives as a ROM at `/ui/ui_intent`;
 *   * Shiro Local Brain (BM25 retrieval over the HermOS docs) adds context to it;
 *   * the augmented prompt goes to the MyKee router as `/mykee/mykee_request`
 *     ("<seq>\n<prompt>") and its answer returns as `/mykee/mykee_response`
 *     ("<seq>\n<answer>"). The TLS client and the API key live ONLY in the router;
 *     this agent also serves the status page on :80 and never sees the key;
 *   * the reply is written to `/ai/ai_command` (a `<report>` node) which
 *     submits a report back to `desktop_builder` via `report_rom`.
 */

/* Genode / libc component entry */
#include <libc/component.h>

/* libc I/O */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

/* C++ */
#include <string>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "shiro_rag.h"
#include <hermos_forge/forge.h>

namespace {

	std::string read_file(char const *path)
	{
		int fd = open(path, O_RDONLY);
		if (fd < 0) return std::string();

		std::string out;
		char        buf[1024];
		ssize_t     n;
		while ((n = read(fd, buf, sizeof(buf))) > 0)
			out.append(buf, static_cast<size_t>(n));

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

	std::string extract_prompt(std::string const &xml)
	{
		size_t const p = xml.find("prompt=\"");
		if (p != std::string::npos) {
			size_t const start = p + 8;
			size_t const end   = xml.find('"', start);
			if (end != std::string::npos)
				return xml.substr(start, end - start);
		}
		return xml;
	}

	/*
	 * Text for the log: control characters replaced, length limited. Prompts are private and
	 * replies are untrusted, so the log only gets a short, printable excerpt and the length.
	 */
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

	/* ---- Shiro Local Brain: BM25 index over the HermOS docs ---- */
	shiro_rag::Index g_index;
	bool             g_indexed = false;

	void build_index()
	{
		std::string const corpus = read_file("/corpus/shiro_corpus.txt");
		if (corpus.empty()) { printf("Shiro: no corpus at /corpus/shiro_corpus.txt\n"); return; }
		g_index.add(corpus);
		g_indexed = true;
		printf("Shiro: indexed corpus (%zu chunks)\n", g_index.size());
	}

	/* the retrieved chunks are context for the model; the request report holds 8 KiB */
	std::string augment_prompt(std::string const &prompt)
	{
		enum { MAX_REQ = 6000 };
		if (!g_indexed) return prompt;
		auto const hits = g_index.search(prompt, 3);
		if (hits.empty()) return prompt;

		std::string ctx = "Context (from the HermOS docs):\n";
		for (auto const &h : hits) {
			std::string const c = g_index.chunk(h.first);
			if (ctx.size() + c.size() + prompt.size() + 80 > (size_t)MAX_REQ) break;
			ctx += "- "; ctx += c; ctx += "\n";
		}
		if (ctx.size() == std::string("Context (from the HermOS docs):\n").size()) return prompt;
		ctx += "\nUsing the context above, answer the user's question: ";
		ctx += prompt;
		return ctx;
	}

/* ---- minimal HTTP server (serves the HermOS Server status page) ---- */
static int httpd_fd = -1;

static void httpd_init()
{
	httpd_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (httpd_fd < 0) { printf("HermOS httpd: socket() failed\n"); return; }
	int one = 1;
	setsockopt(httpd_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = INADDR_ANY;
	a.sin_port = htons(80);
	if (bind(httpd_fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
		printf("HermOS httpd: bind(:80) failed\n");
		return;
	}
	if (listen(httpd_fd, 8) < 0) { printf("HermOS httpd: listen() failed\n"); return; }
	fcntl(httpd_fd, F_SETFL, O_NONBLOCK);
	printf("HermOS httpd: listening on :80\n");
}

static void httpd_tick()
{
	if (httpd_fd < 0) return;
	int c = accept(httpd_fd, nullptr, nullptr);
	if (c < 0) return;   /* EAGAIN or error */
	/* a client that connects and says nothing must not stall the agent's loop */
	struct timeval const tv = { 0, 200 * 1000 };
	setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	char req[1024];
	ssize_t n = read(c, req, sizeof(req) - 1);
	if (n > 0) {
		req[n] = 0;
		char method[16] = {0}, path[256] = {0};
		sscanf(req, "%15s %255s", method, path);
		for (char *p = path; *p; ++p) if (*p < 0x20 || *p > 0x7e) *p = '?';   /* no control characters in the log */
		printf("HermOS httpd: %s %s\n", method, path);
		static char const body[] =
			"<!DOCTYPE html><html><head><title>HermOS Server</title></head>"
			"<body style=\"background:#0a0c10;color:#e8eaf0;font-family:sans-serif\">"
			"<h1>HermOS Server</h1>"
			"<p>Status: <b>RUNNING</b></p><p>Edition: Server</p>"
			"<p>Kernel: NOVA / Genode 26.08</p>"
			"<p>Only what runs here is listed.</p><ul>"
			"<li>hermes-httpd :80 (this page)</li>"
			"<li>mykee-router (AI gateway, internal)</li>"
			"</ul><p>served by hermes_agent_cpp over lwIP</p>"
			"</body></html>";
		char resp[4096];
		int const len = snprintf(resp, sizeof(resp),
			"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
			"Content-Length: %d\r\nConnection: close\r\n\r\n%s",
			(int)strlen(body), body);
		write(c, resp, static_cast<size_t>(len));
	}
	close(c);
}

} /* anonymous namespace */


void Libc::Component::construct(Libc::Env &env)
{
	(void)env;

	Libc::with_libc([&]() {

		printf("HermOS hermes_agent_cpp: waiting for ui_intent...\n");

		httpd_init();
		build_index();

		std::string last_intent;
		unsigned    last_seq = 0;            /* counts up for the life of the agent: a number is never reused */
		unsigned    seq = 0;                 /* the request being answered, 0 = none */
		unsigned    waited_ms = 0;

		for (;;) {

			httpd_tick();

			std::string xml = read_file("/ui/ui_intent");
			trim(xml);

			if (!xml.empty() && xml != last_intent) {
				last_intent = xml;

				std::string const prompt = extract_prompt(xml);
				printf("HermOS: prompt received (%u bytes): [%s]\n", (unsigned)prompt.size(), log_safe(prompt).c_str());

				std::string const aug = augment_prompt(prompt);
				seq = ++last_seq; waited_ms = 0;
				printf("Shiro: request %u (%u bytes)\n", seq, (unsigned)aug.size());
				write_file("/mykee/mykee_request", std::to_string(seq) + "\n" + aug);
			}

			if (seq) {
				/* only the answer carrying our sequence number counts; an old one is ignored */
				std::string resp = read_file("/mykee/mykee_response");
				trim(resp);
				size_t const nl = resp.find('\n');
				if (nl != std::string::npos && resp.compare(0, nl, std::to_string(seq)) == 0) {
					std::string ai = resp.substr(nl + 1);
					/* a reply that proposes a component is checked by fixed rules (the Forge) before the user sees it */
					if (ai.find("<file path=\"") != std::string::npos) {
						Hermos::Forge::Result const fr = Hermos::Forge::check(ai);
						printf("HermOS forge: component '%s' %s (%u files, %u problems)\n", fr.name.c_str(),
						       fr.ok ? "passed" : "refused", (unsigned)fr.files.size(), (unsigned)fr.problems.size());
						ai = Hermos::Forge::summary(fr);
					}
					printf("HermOS: AI response (%u bytes): [%s]\n", (unsigned)ai.size(), log_safe(ai).c_str());
					write_file("/ai/ai_command", ai);
					seq = 0;
				} else if ((waited_ms += 200) >= 300000) {
					write_file("/ai/ai_command", "error: the AI router did not answer in time");
					seq = 0;
				}
			}

			struct timespec const ts = { 0, 200 * 1000 * 1000 }; /* 200 ms */
			nanosleep(&ts, nullptr);
		}
	});
}
