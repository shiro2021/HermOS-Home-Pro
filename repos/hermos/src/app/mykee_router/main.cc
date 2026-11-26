/*
 * HermOS MyKee router — the "one key for all LLMs" gateway.
 *
 * A Genode *libc* component. It is the single backend the AI agent talks to:
 *
 *   * one MyKee key lives at /ai/mykee.key (written by the installer);
 *   * an ordered route list lives at /config/mykee.conf (JSON), each route an
 *     OpenAI-compatible /chat/completions endpoint (master-proxy gateway first,
 *     then fallbacks);
 *   * the prompt arrives as the /mykee/mykee_request report (written by
 *     hermes_agent_cpp) and the answer leaves as /mykee/mykee_response;
 *   * a prompt is routed down the list — the first route that returns a usable
 *     completion wins (fallback chain).
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
#include <vector>

/* sockets for curl */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* HTTP + JSON */
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

	struct Route
	{
		std::string name;
		std::string url;
		std::string model;
	};

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

	/* a prompt is private and a reply is untrusted: the log gets a short printable excerpt only */
	std::string log_safe(std::string const &in, size_t max = 80)
	{
		std::string out;
		for (char ch : in) {
			if (out.size() >= max) { out += "..."; break; }
			out += (ch >= 0x20 && ch <= 0x7e) ? ch : '?';
		}
		return out;
	}

	/* an API key is one token of URL-safe characters; anything else is never sent as a credential */
	bool key_plausible(std::string const &k)
	{
		if (k.size() < 16 || k.size() > 256) return false;
		for (char const ch : k) {
			bool const ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
			                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
			if (!ok) return false;
		}
		return true;
	}

	/* TLS needs real randomness: two separate opens of /dev/random must differ and vary */
	bool entropy_ok()
	{
		unsigned char a[32] = { }, b[32] = { };
		int fd = open("/dev/random", O_RDONLY);
		if (fd < 0) return false;
		ssize_t const na = read(fd, a, sizeof(a));
		close(fd);
		fd = open("/dev/random", O_RDONLY);
		if (fd < 0) return false;
		ssize_t const nb = read(fd, b, sizeof(b));
		close(fd);
		if (na != (ssize_t)sizeof(a) || nb != (ssize_t)sizeof(b)) return false;
		bool varied = false;
		for (unsigned i = 0; i < sizeof(a); i++) if (a[i] != a[0]) varied = true;
		return varied && memcmp(a, b, sizeof(a)) != 0;
	}

	void trim(std::string &s)
	{
		size_t const b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) { s.clear(); return; }
		size_t const e = s.find_last_not_of(" \t\r\n");
		s = s.substr(b, e - b + 1);
	}

	bool load_routes(std::vector<Route> &routes)
	{
		std::string raw = read_file("/config/mykee.conf");
		trim(raw);
		if (raw.empty()) {
			printf("MyKee: no config at /config/mykee.conf\n");
			return false;
		}

		json cfg;
		try {
			cfg = json::parse(raw);
		} catch (std::exception const &e) {
			printf("MyKee: config JSON parse failed: %s\n", e.what());
			return false;
		}

		if (!cfg.contains("routes") || !cfg["routes"].is_array()) {
			printf("MyKee: config missing 'routes' array\n");
			return false;
		}

		for (auto const &r : cfg["routes"]) {
			Route rt;
			rt.name  = r.value("name",  std::string("?"));
			rt.url   = r.value("url",   std::string());
			rt.model = r.value("model", std::string("default"));
			if (rt.url.compare(0, 8, "https://") != 0 && rt.url.compare(0, 8, "local://") != 0) {
				printf("MyKee: route '%s' is neither https nor local, skipping\n", rt.name.c_str());
				continue;
			}
			if (rt.url.empty()) {
				printf("MyKee: route '%s' has no url, skipping\n", rt.name.c_str());
				continue;
			}
			routes.push_back(rt);
		}
		return true;
	}

	size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		auto *out = static_cast<std::string *>(userdata);
		enum { MAX_BODY = 1 << 20 };               /* a reply never needs more than 1 MiB */
		if (out->size() + size * nmemb > MAX_BODY) return 0;   /* aborts the transfer */
		out->append(ptr, size * nmemb);
		return size * nmemb;
	}

	/*
	 * POST a prompt to ONE OpenAI-compatible route. Returns the assistant
	 * content, or an empty string on any failure (so the fallback chain can
	 * advance to the next route).
	 */
	std::string route_call(Route const &r, std::string const &prompt,
	                       std::string const &key)
	{
		json req;
		req["model"] = r.model;
		req["messages"] = json::array();
		req["messages"].push_back({ { "role", "user" }, { "content", prompt } });
		req["stream"] = false;
		std::string const body = req.dump();

		curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		if (!key.empty()) {
			std::string const auth = "Authorization: Bearer " + key;
			headers = curl_slist_append(headers, auth.c_str());
		}

		std::string response;

		CURL *c = curl_easy_init();
		if (!c) {
			curl_slist_free_all(headers);
			return std::string();
		}

		curl_easy_setopt(c, CURLOPT_URL, r.url.c_str());
		curl_easy_setopt(c, CURLOPT_POST, 1L);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
		/* the request carries the key and the prompt: verify the server, https only, no downgrade */
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

		if (res != CURLE_OK) return std::string();

		try {
			json resp = json::parse(response);
			if (resp.contains("error")) return std::string();
			if (!resp.contains("choices") || resp["choices"].empty())
				return std::string();
			return resp["choices"][0]["message"]["content"].get<std::string>();
		} catch (std::exception const &) {
			return std::string();
		}
	}

	/*
	 * The local model (shiro_brain): "<seq>\n<prompt>" out, "<seq>\n<answer>" back. An answer with another
	 * sequence number is an old one and is ignored. Empty = no answer (the chain advances).
	 */
	std::string local_call(std::string const &prompt)
	{
		static unsigned seq = 0;
		++seq;
		write_file("/brain/brain_request", std::to_string(seq) + "\n" + prompt);

		for (unsigned waited_ms = 0; waited_ms < 900000; waited_ms += 200) {
			std::string resp = read_file("/brain/brain_response");
			trim(resp);
			size_t const nl = resp.find('\n');
			if (nl != std::string::npos && resp.compare(0, nl, std::to_string(seq)) == 0) {
				std::string ans = resp.substr(nl + 1);
				trim(ans);
				return ans;
			}
			struct timespec const ts = { 0, 200 * 1000 * 1000 };
			nanosleep(&ts, nullptr);
		}
		return std::string();
	}

	/* fallback chain: first route that returns a non-empty answer wins. Local routes need no key and no TLS;
	 * https routes need the key and a real entropy source, and are skipped without them. */
	std::string mykee_route(std::vector<Route> const &routes, std::string const &prompt,
	                        std::string const &key, bool rng_ok)
	{
		for (auto const &r : routes) {
			bool const local = r.url.compare(0, 8, "local://") == 0;
			if (!local && (key.empty() || !rng_ok)) {
				printf("MyKee: skipping '%s' (%s)\n", r.name.c_str(), key.empty() ? "no key" : "no entropy");
				continue;
			}
			printf("MyKee: routing to '%s'\n", r.name.c_str());
			std::string const out = local ? local_call(prompt) : route_call(r, prompt, key);
			if (!out.empty()) return out;
			printf("MyKee: route '%s' failed, advancing\n", r.name.c_str());
		}
		return key.empty() ? "error: no AI route answered: no local model and no API key in this image (see HERMOS_MYKEE_KEY_FILE)"
		                   : "error: all MyKee routes failed";
	}

} /* anonymous namespace */


void Libc::Component::construct(Libc::Env &env)
{
	(void)env;

	Libc::with_libc([&]() {

		curl_global_init(CURL_GLOBAL_DEFAULT);

		printf("MyKee router starting\n");

		std::string key = read_file("/ai/mykee.key");
		trim(key);
		bool const key_ok = key_plausible(key);
		if (!key_ok) key.clear();
		printf(key_ok ? "MyKee: key loaded from /ai/mykee.key\n"
		              : "MyKee: WARNING: no usable key at /ai/mykee.key (empty or not a key)\n");

		bool const rng_ok = entropy_ok();
		printf(rng_ok ? "MyKee: entropy source OK\n"
		              : "MyKee: ERROR: /dev/random is not random; refusing to open TLS connections\n");

		std::vector<Route> routes;
		if (!load_routes(routes)) {
			printf("MyKee: no usable routes, idle\n");
			return;
		}

		for (size_t i = 0; i < routes.size(); ++i)
			printf("MyKee route [%zu] '%s' -> %s (model %s)\n",
			       i, routes[i].name.c_str(), routes[i].url.c_str(),
			       routes[i].model.c_str());

		printf("MyKee router ready (%zu routes, fallback chain)\n", routes.size());

		std::string last;

		for (;;) {

			std::string req = read_file("/mykee/mykee_request");
			trim(req);

			if (!req.empty() && req != last) {
				last = req;

				/* "<seq>\n<prompt>": the answer carries the same seq, so a stale one is never taken */
				size_t const nl = req.find('\n');
				std::string const seq    = nl == std::string::npos ? std::string("0") : req.substr(0, nl);
				std::string const prompt = nl == std::string::npos ? req : req.substr(nl + 1);
				printf("MyKee: request %s (%u bytes): [%s]\n", log_safe(seq, 12).c_str(), (unsigned)prompt.size(),
				       log_safe(prompt).c_str());

				std::string const out = mykee_route(routes, prompt, key, rng_ok);
				printf("MyKee: response (%u bytes): [%s]\n", (unsigned)out.size(), log_safe(out).c_str());

				write_file("/mykee/mykee_response", seq + "\n" + out);
			}

			struct timespec const ts = { 0, 200 * 1000 * 1000 }; /* 200 ms */
			nanosleep(&ts, nullptr);
		}
	});
}
