/*
 * The smart bar: one field for addresses and for asking. Plain C++ (no Qt) so it is tested on the host.
 *   "wikipedia.org"          -> https://wikipedia.org
 *   "https://example.com/a"  -> as typed
 *   "localhost:8080"         -> http://localhost:8080 (local names are the one plain-http case)
 *   "best pasta recipe"      -> a search (the engine is a template with %s, from the theme/config)
 *   "javascript:...", "file:///etc/passwd", "data:..."  -> refused, never loaded
 */
#pragma once
#include <cctype>
#include <string>

namespace hermos {

enum class Kind { Nothing, Url, Search, Refused };
struct Target { Kind kind; std::string text; };

inline std::string trim(std::string s)
{
	size_t b = 0, e = s.size();
	while (b < e && std::isspace((unsigned char)s[b])) ++b;
	while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
	return s.substr(b, e - b);
}

inline std::string lower(std::string s) { for (auto &c : s) c = (char)std::tolower((unsigned char)c); return s; }

inline std::string percent_encode(std::string const &in)
{
	static char const *hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : in) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
		else if (c == ' ') out += '+';
		else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
	}
	return out;
}

inline bool is_local_host(std::string const &host)
{
	std::string h = lower(host);
	if (h == "localhost" || h == "[::1]") return true;
	if (h.size() > 6 && h.compare(h.size() - 6, 6, ".local") == 0) return true;
	if (h.rfind("127.", 0) == 0 || h.rfind("10.", 0) == 0 || h.rfind("192.168.", 0) == 0) return true;
	return false;
}

/* host part of "host[:port][/path...]" */
inline std::string host_of(std::string const &s)
{
	size_t const end = s.find_first_of("/?#");
	std::string h = s.substr(0, end);
	if (!h.empty() && h[0] == '[') { size_t const r = h.find(']'); return r == std::string::npos ? h : h.substr(0, r + 1); }
	size_t const colon = h.find(':');
	return colon == std::string::npos ? h : h.substr(0, colon);
}

inline bool looks_like_host(std::string const &host)
{
	if (host.empty() || host.size() > 253) return false;
	if (is_local_host(host)) return true;
	size_t dots = 0, last_dot = std::string::npos;
	for (size_t i = 0; i < host.size(); ++i) {
		unsigned char c = (unsigned char)host[i];
		if (c == '.') { if (i == 0 || i + 1 == host.size() || host[i - 1] == '.') return false; ++dots; last_dot = i; }
		else if (!(std::isalnum(c) || c == '-')) return false;
	}
	if (!dots) return false;
	std::string const tld = host.substr(last_dot + 1);
	bool numeric = true; for (unsigned char c : tld) if (!std::isdigit(c)) numeric = false;
	if (numeric) return dots == 3;                                   /* an IPv4 address */
	if (tld.size() < 2) return false;
	for (unsigned char c : tld) if (!(std::isalpha(c) || c == '-' || std::isdigit(c))) return false;
	return true;
}

inline Target resolve(std::string const &input_raw, std::string const &search_template = "https://duckduckgo.com/?q=%s")
{
	std::string const input = trim(input_raw);
	if (input.empty()) return { Kind::Nothing, "" };

	/* an explicit scheme: letters/digits/+-. before the FIRST colon, and something other than a space
	 * or a port number after it ("note: buy milk" is a question, "host:8080" is a host and a port) */
	size_t const colon = input.find(':');
	if (colon != std::string::npos && colon > 0 && colon + 1 < input.size()) {
		std::string const head = lower(input.substr(0, colon));
		bool valid = std::isalpha((unsigned char)head[0]);
		for (unsigned char c : head) if (!(std::isalnum(c) || c == '+' || c == '-' || c == '.')) valid = false;
		bool const slashes = input.compare(colon, 3, "://") == 0;
		bool const port    = std::isdigit((unsigned char)input[colon + 1]) && !slashes;
		bool const spaced  = std::isspace((unsigned char)input[colon + 1]);
		if (valid && !port && !spaced) {
			if (head == "https" || head == "http") return { Kind::Url, input };   /* the Shield upgrades http (local hosts excepted) */
			return { Kind::Refused, head + ": addresses are not opened here" };
		}
	}

	/* a host (with optional port, path) */
	if (input.find(' ') == std::string::npos) {
		std::string const host = host_of(input);
		if (looks_like_host(host))
			return { Kind::Url, std::string(is_local_host(host) ? "http://" : "https://") + input };
	}

	/* anything else is a question */
	std::string url = search_template;
	size_t const at = url.find("%s");
	if (at != std::string::npos) url.replace(at, 2, percent_encode(input));
	return { Kind::Search, url };
}

} /* namespace hermos */
