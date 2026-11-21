/* Linux backend: libcurl against the system CA store. Verification cannot be turned off here. */
#include "agent.h"
#include <curl/curl.h>
#include <mutex>

namespace hermes {

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *out = static_cast<std::string *>(userdata);
	if (out->size() + size * nmemb > MAX_REPLY) return 0;          /* abort an oversized reply */
	out->append(ptr, size * nmemb);
	return size * nmemb;
}

HttpResult https_post_json(std::string const &url, std::string const &bearer, std::string const &body)
{
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

	HttpResult r;
	if (url.rfind("https://", 0) != 0) { r.error = "refusing a non-https URL"; return r; }

	CURL *c = curl_easy_init();
	if (!c) { r.error = "curl_easy_init failed"; return r; }

	curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	std::string const auth = "Authorization: Bearer " + bearer;
	headers = curl_slist_append(headers, auth.c_str());

	curl_easy_setopt(c, CURLOPT_URL, url.c_str());
	curl_easy_setopt(c, CURLOPT_POST, 1L);
	curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

	CURLcode const res = curl_easy_perform(c);
	if (res == CURLE_OK) {
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
		r.ok = true;
	} else {
		r.error = std::string("connection failed: ") + curl_easy_strerror(res);
		if (res == CURLE_PEER_FAILED_VERIFICATION || res == CURLE_SSL_CACERT_BADFILE)
			r.error += " (the server certificate was not accepted; nothing was sent)";
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(c);
	return r;
}

} /* namespace hermes */
