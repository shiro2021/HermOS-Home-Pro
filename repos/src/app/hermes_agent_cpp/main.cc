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

	/* libcurl write callback: accumulate the body into a std::string */
	size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		auto *out = static_cast<std::string *>(userdata);
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
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
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

		printf("HermOS hermes_agent_cpp: waiting for ui_intent...\n");

		std::string last;

		for (;;) {

			std::string xml = read_file("/ui/ui_intent");
			trim(xml);

			if (!xml.empty() && xml != last) {

				last = xml;

				std::string const prompt = extract_prompt(xml);
				printf("HermOS: prompt received: [%s]\n", prompt.c_str());

				std::string const ai = deepseek_call(prompt, api_key);
				printf("HermOS: AI response: [%s]\n", ai.c_str());

				write_file("/ai/ai_command", ai);
			}

			struct timespec const ts = { 0, 200 * 1000 * 1000 }; /* 200 ms */
			nanosleep(&ts, nullptr);
		}
	});
}
