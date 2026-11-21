/*
 * HermOS AI agent — native C++ implementation (Plan B).
 *
 * A standard Genode *libc* component.  IPC and networking all go through the
 * VFS/libc:
 *
 *   * `ui_intent` (the user prompt) arrives as a ROM at `/ui/ui_intent`;
 *   * the DeepSeek response is posted with libcurl over the lwip socket;
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

/* HTTP + JSON */
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

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
	 * TLS draws its randomness from /dev/random. A constant or repeating source makes the
	 * session keys predictable, so the agent checks it once at start and refuses to talk
	 * to the network service if it is not varying.
	 */
	bool entropy_ok()
	{
		unsigned char a[32] = { }, b[32] = { };
		int fd = open("/dev/random", O_RDONLY);
		if (fd < 0) return false;
		ssize_t const na = read(fd, a, sizeof(a));
		ssize_t const nb = read(fd, b, sizeof(b));
		close(fd);
		if (na != (ssize_t)sizeof(a) || nb != (ssize_t)sizeof(b)) return false;
		bool differ = false, varied = false;
		for (unsigned i = 0; i < sizeof(a); i++) { if (a[i] != b[i]) differ = true; if (a[i] != a[0]) varied = true; }
		return differ && varied;
	}

	/* libcurl write callback: accumulate the body into a std::string */
	size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		auto *out = static_cast<std::string *>(userdata);
		enum { MAX_BODY = 1 << 20 };               /* a reply never needs more than 1 MiB */
		if (out->size() + size * nmemb > MAX_BODY) return 0;   /* aborts the transfer */
		out->append(ptr, size * nmemb);
		return size * nmemb;
	}

	/*
	 * POST a prompt to the DeepSeek chat-completions API and return the
	 * assistant message content (choices[0].message.content).
	 */
	std::string deepseek_call(std::string const &prompt, std::string const &api_key)
	{
		json req;
		req["model"] = "deepseek-chat";
		req["messages"] = json::array();
		req["messages"].push_back({ { "role", "user" }, { "content", prompt } });
		req["stream"] = false;
		std::string const body = req.dump();

		curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		std::string const auth = "Authorization: Bearer " + api_key;
		headers = curl_slist_append(headers, auth.c_str());

		std::string response;

		CURL *c = curl_easy_init();
		if (!c) {
			curl_slist_free_all(headers);
			return "error: curl_easy_init failed";
		}

		curl_easy_setopt(c, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
		curl_easy_setopt(c, CURLOPT_POST, 1L);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
		/*
		 * The request carries the API key and the user's prompt: the server certificate must
		 * be verified (chain to the CA bundle in the image, host name matches), or anyone on the
		 * network path could read or alter both. No fallback to an unverified connection.
		 */
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(c, CURLOPT_CAINFO, "/etc/ssl/ca-bundle.crt");
		curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
		curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
		curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);

		CURLcode const res = curl_easy_perform(c);
		curl_slist_free_all(headers);
		curl_easy_cleanup(c);

		if (res != CURLE_OK) {
			char buf[256];
			snprintf(buf, sizeof(buf), "error: curl failed: %s",
			         curl_easy_strerror(res));
			return std::string(buf);
		}

		try {
			json resp = json::parse(response);
			if (resp.contains("error"))
				return "API error: " +
				       resp["error"].value("message", std::string("unknown"));
			if (!resp.contains("choices") || resp["choices"].empty())
				return "API error: no choices in response";
			return resp["choices"][0]["message"]["content"].get<std::string>();
		} catch (std::exception const &e) {
			return std::string("error: JSON parse failed: ") + e.what();
		}
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

		curl_global_init(CURL_GLOBAL_DEFAULT);

		/* load the DeepSeek API key from the VFS (mounted by the run script) */
		std::string api_key = read_file("/ai/mykee.key");
		trim(api_key);
		printf(api_key.empty()
		       ? "HermOS: WARNING: no API key at /ai/mykee.key\n"
		       : "HermOS: API key loaded from /ai/mykee.key\n");

		bool const rng_ok = entropy_ok();
		printf(rng_ok ? "HermOS: entropy source OK\n"
		              : "HermOS: ERROR: /dev/random is not random; refusing to use the network service\n");

		printf("HermOS hermes_agent_cpp: waiting for ui_intent...\n");

	httpd_init();

		std::string last;

		for (;;) {

			httpd_tick();

		std::string xml = read_file("/ui/ui_intent");
			trim(xml);

			if (!xml.empty() && xml != last) {

				last = xml;

				std::string const prompt = extract_prompt(xml);
				printf("HermOS: prompt received: [%s]\n", prompt.c_str());

				std::string const ai = !rng_ok         ? "error: no entropy source; the agent will not open a TLS connection"
				                     : api_key.empty() ? "error: no API key in this image (see HERMOS_MYKEE_KEY_FILE)"
				                     :                   deepseek_call(prompt, api_key);
				printf("HermOS: AI response: [%s]\n", ai.c_str());

				write_file("/ai/ai_command", ai);
			}

			struct timespec const ts = { 0, 200 * 1000 * 1000 }; /* 200 ms */
			nanosleep(&ts, nullptr);
		}
	});
}
