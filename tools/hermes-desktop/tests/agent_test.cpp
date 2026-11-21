/* Tests of the platform-neutral core. No network. */
#include "agent.h"
#include <cstdio>
#include <string>
#include <cstdlib>
#include <nlohmann/json.hpp>
#ifndef _WIN32
#  include <sys/stat.h>
#endif
using namespace hermes;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
	/* key_plausible */
	CHECK(key_plausible("abcdefghijklmnop"));
	CHECK(key_plausible("A1_b2-c3.d4E5f6G7h8"));
	CHECK(!key_plausible("short"));
	CHECK(!key_plausible(std::string(257, 'a')));
	CHECK(!key_plausible("this is a placeholder sentence"));          /* spaces */
	CHECK(!key_plausible("abcdefghijklmnop\n"));                      /* control character */
	CHECK(!key_plausible(""));

	/* log_safe */
	CHECK(log_safe("hello") == "hello");
	CHECK(log_safe("a\nb\x1b" "c") == "a?b?c");
	CHECK(log_safe(std::string(200, 'x')).size() == 83);              /* 80 + "..." */
	CHECK(log_safe(std::string(200, 'x')).substr(80) == "...");

	/* build_request */
	auto j = nlohmann::json::parse(build_request("hi \"there\"\n\xff"));      /* invalid UTF-8 must not throw */
	CHECK(j["model"] == MODEL);
	CHECK(j["stream"] == false);
	CHECK(j["messages"][0]["role"] == "user");
	CHECK(j["messages"][0]["content"].get<std::string>().rfind("hi \"there\"\n", 0) == 0);

	/* parse_response */
	Reply r = parse_response(200, R"({"choices":[{"message":{"content":"Hello!"}}]})");
	CHECK(r.ok && r.text == "Hello!");
	r = parse_response(401, R"({"error":{"message":"bad key"}})");
	CHECK(!r.ok && r.text == "API error: bad key");
	r = parse_response(500, "<html>boom</html>");
	CHECK(!r.ok && r.text == "API error: HTTP 500");
	r = parse_response(200, "not json");
	CHECK(!r.ok);
	r = parse_response(200, R"({"choices":[]})");
	CHECK(!r.ok);
	r = parse_response(200, R"({"choices":[{"message":{"content":5}}]})");
	CHECK(!r.ok);
	r = parse_response(503, R"({"choices":[{"message":{"content":"x"}}]})");
	CHECK(!r.ok);                                                       /* a non-2xx status is never an answer */

	/* ask() refuses before any network use */
	r = ask("hi", "not a key");
	CHECK(!r.ok);
	r = ask(std::string(MAX_PROMPT + 1, 'a'), "abcdefghijklmnop");
	CHECK(!r.ok && r.text.find("too long") != std::string::npos);

	/* the network layer refuses a non-https URL */
	HttpResult h = https_post_json("http://example.com/", "abcdefghijklmnop", "{}");
	CHECK(!h.ok && h.error.find("https") != std::string::npos);

	/* the key file: saved owner-only, found again, removed (per-user folder redirected to a temp dir) */
#ifndef _WIN32
	{
		char tmpl[] = "/tmp/hermes-test-XXXXXX";
		char const *dir = mkdtemp(tmpl);
		CHECK(dir != nullptr);
		if (dir) {
			setenv("XDG_CONFIG_HOME", dir, 1);
			unsetenv("HERMOS_MYKEE_KEY"); unsetenv("HERMOS_MYKEE_KEY_FILE");
			CHECK(load_key().source == KeySource::None);
			std::string err;
			CHECK(!save_key("too short", err));                       /* not a key: refused, nothing written */
			CHECK(load_key().source == KeySource::None);
			CHECK(save_key("  abcdefghijklmnop1234  ", err));
			struct stat st; CHECK(stat(default_key_path().c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
			KeyInfo k = load_key();
			CHECK(k.source == KeySource::File && k.key == "abcdefghijklmnop1234");
			setenv("HERMOS_MYKEE_KEY", "envenvenvenvenvenv1", 1);     /* the environment wins over the file */
			CHECK(load_key().source == KeySource::Environment);
			unsetenv("HERMOS_MYKEE_KEY");
			CHECK(remove_key_file(err) && load_key().source == KeySource::None);
			std::string cmd = std::string("rm -rf ") + dir; (void)!std::system(cmd.c_str());
		}
	}
#endif

	std::printf(failures ? "%d FAILED\n" : "all core tests passed\n", failures);
	return failures ? 1 : 0;
}
