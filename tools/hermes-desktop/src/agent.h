/*
 * Hermes Agent (desktop): the platform-neutral core.
 *
 * The same behaviour as the HermOS agent (hermes_agent_cpp): one prompt in, one reply out,
 * through the DeepSeek chat-completions API, with
 *   - the server certificate and host name always verified (no switch, no fallback),
 *   - https only, no redirects, replies capped at 1 MiB,
 *   - an API key that must look like a key (one URL-safe token), never logged,
 *   - prompts and replies reaching any log only as a short printable excerpt.
 */
#pragma once
#include <string>

namespace hermes {

constexpr char const *ENDPOINT = "https://api.deepseek.com/chat/completions";
constexpr char const *MODEL    = "deepseek-chat";
constexpr size_t MAX_REPLY  = 1u << 20;      /* bytes of reply body accepted */
constexpr size_t MAX_PROMPT = 16u * 1024;    /* bytes of prompt accepted */

/* ---- pure functions (tested without a network) ---- */

/* one token of URL-safe characters, 16..256 long */
bool key_plausible(std::string const &key);

/* printable, shortened excerpt for a log line; control characters become '?' */
std::string log_safe(std::string const &in, size_t max = 80);

/* the JSON request body for a prompt */
std::string build_request(std::string const &prompt);

struct Reply { bool ok; std::string text; };

/* the assistant text from a response body, or an error text */
Reply parse_response(long http_status, std::string const &body);

/* ---- network (platform backend) ---- */

struct HttpResult { bool ok { false }; long status { 0 }; std::string body; std::string error; };

/* POST json with a bearer token. Certificate and host name are verified; https only. */
HttpResult https_post_json(std::string const &url, std::string const &bearer, std::string const &body);

/* prompt -> reply (blocking; call from a worker thread) */
Reply ask(std::string const &prompt, std::string const &key);

/* ---- the key ---- */

enum class KeySource { None, Environment, File, Entered };

struct KeyInfo { std::string key; KeySource source { KeySource::None }; std::string where; };

/* HERMOS_MYKEE_KEY, else HERMOS_MYKEE_KEY_FILE, else the per-user key file */
KeyInfo load_key();
std::string default_key_path();
bool save_key(std::string const &key, std::string &error);    /* per-user file, owner-only */
bool remove_key_file(std::string &error);

} /* namespace hermes */
