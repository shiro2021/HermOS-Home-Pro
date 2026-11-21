#include "agent.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace hermes {

using nlohmann::json;

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

std::string log_safe(std::string const &in, size_t max)
{
	std::string out;
	for (size_t i = 0; i < in.size() && out.size() < max; i++) {
		unsigned char const c = static_cast<unsigned char>(in[i]);
		out += (c < 0x20 || c == 0x7f) ? '?' : static_cast<char>(c);
	}
	if (in.size() > max) out += "...";
	return out;
}

std::string build_request(std::string const &prompt)
{
	json req;
	req["model"]    = MODEL;
	req["messages"] = json::array();
	req["messages"].push_back({ { "role", "user" }, { "content", prompt } });
	req["stream"]   = false;
	/* invalid UTF-8 in the prompt is replaced, not an exception */
	return req.dump(-1, ' ', false, json::error_handler_t::replace);
}

Reply parse_response(long status, std::string const &body)
{
	try {
		json resp = json::parse(body);
		if (resp.contains("error")) {
			std::string msg = "unknown";
			if (resp["error"].is_object()) msg = resp["error"].value("message", std::string("unknown"));
			return { false, "API error: " + msg };
		}
		if (status < 200 || status >= 300)
			return { false, "API error: HTTP " + std::to_string(status) };
		if (!resp.contains("choices") || !resp["choices"].is_array() || resp["choices"].empty())
			return { false, "API error: no choices in response" };
		auto const &c = resp["choices"][0];
		if (!c.contains("message") || !c["message"].contains("content") || !c["message"]["content"].is_string())
			return { false, "API error: reply has no text" };
		return { true, c["message"]["content"].get<std::string>() };
	} catch (std::exception const &e) {
		if (status >= 400) return { false, "API error: HTTP " + std::to_string(status) };
		return { false, std::string("error: reply is not valid JSON (") + e.what() + ")" };
	}
}

Reply ask(std::string const &prompt, std::string const &key)
{
	if (!key_plausible(key))
		return { false, "No usable API key. Open Key (the gear) and enter one, or set HERMOS_MYKEE_KEY." };
	if (prompt.empty())             return { false, "Nothing to send." };
	if (prompt.size() > MAX_PROMPT) return { false, "Prompt is too long (limit 16 KiB)." };

	HttpResult const r = https_post_json(ENDPOINT, key, build_request(prompt));
	if (!r.ok) return { false, "error: " + r.error };
	return parse_response(r.status, r.body);
}

/* ---- the key ---- */

static std::string trimmed(std::string s)
{
	size_t const b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos) return std::string();
	size_t const e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

static std::string read_text_file(std::string const &path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return std::string();
	std::stringstream ss; ss << f.rdbuf();
	std::string s = ss.str();
	if (s.size() > 4096) s.clear();                     /* a key file is tiny; anything else is not one */
	return s;
}

std::string default_key_path()
{
#ifdef _WIN32
	char dir[MAX_PATH] = { };
	if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, dir) != S_OK) return std::string();
	return std::string(dir) + "\\hermos\\mykee.key";
#else
	char const *xdg = std::getenv("XDG_CONFIG_HOME");
	char const *home = std::getenv("HOME");
	if (xdg && *xdg)   return std::string(xdg) + "/hermos/mykee.key";
	if (home && *home) return std::string(home) + "/.config/hermos/mykee.key";
	return std::string();
#endif
}

KeyInfo load_key()
{
	if (char const *e = std::getenv("HERMOS_MYKEE_KEY")) {
		std::string k = trimmed(e);
		if (key_plausible(k)) return { k, KeySource::Environment, "HERMOS_MYKEE_KEY" };
	}
	if (char const *p = std::getenv("HERMOS_MYKEE_KEY_FILE")) {
		std::string k = trimmed(read_text_file(p));
		if (key_plausible(k)) return { k, KeySource::File, p };
	}
	std::string const path = default_key_path();
	if (!path.empty()) {
		std::string k = trimmed(read_text_file(path));
		if (key_plausible(k)) return { k, KeySource::File, path };
	}
	return { };
}

static void make_dirs_for(std::string const &path)
{
	for (size_t i = 1; i < path.size(); i++) {
		if (path[i] != '/' && path[i] != '\\') continue;
		std::string const d = path.substr(0, i);
#ifdef _WIN32
		CreateDirectoryA(d.c_str(), nullptr);
#else
		mkdir(d.c_str(), 0700);
#endif
	}
}

bool save_key(std::string const &key_in, std::string &error)
{
	std::string const key = trimmed(key_in);
	if (!key_plausible(key)) { error = "That is not an API key (one token of letters, digits, - _ .; 16 to 256 characters)."; return false; }
	std::string const path = default_key_path();
	if (path.empty()) { error = "No per-user folder to save the key in."; return false; }
	make_dirs_for(path);
#ifndef _WIN32
	/* create the file owner-only before the key goes in */
	int const fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) { error = "Cannot write " + path; return false; }
	std::string const data = key + "\n";
	bool const ok = ::write(fd, data.data(), data.size()) == (ssize_t)data.size();
	::close(fd);
	::chmod(path.c_str(), 0600);
	if (!ok) { error = "Cannot write " + path; return false; }
#else
	std::ofstream f(path, std::ios::binary | std::ios::trunc);       /* %APPDATA% is per-user */
	if (!f) { error = "Cannot write " + path; return false; }
	f << key << "\n";
#endif
	return true;
}

bool remove_key_file(std::string &error)
{
	std::string const path = default_key_path();
	if (path.empty()) return true;
	if (std::remove(path.c_str()) != 0) {
		std::ifstream f(path);
		if (f) { error = "Cannot remove " + path; return false; }
	}
	return true;
}

} /* namespace hermes */
