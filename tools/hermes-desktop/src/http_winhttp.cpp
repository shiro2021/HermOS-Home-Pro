/* Windows backend: WinHTTP with the system certificate store. Certificate errors are never
 * ignored (no WINHTTP_OPTION_SECURITY_FLAGS), and a failed check ends the request. */
#include "agent.h"
#include <windows.h>
#include <winhttp.h>

namespace hermes {

static std::wstring widen(std::string const &s)
{
	if (s.empty()) return std::wstring();
	int const n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

/* plain words for the common WinHTTP errors; the number is kept for support */
static std::string describe(DWORD e)
{
	char const *why = "connection failed";
	switch (e) {
	case ERROR_WINHTTP_NAME_NOT_RESOLVED:  why = "connection failed: could not resolve the server name (no internet, or DNS is down)"; break;
	case ERROR_WINHTTP_CANNOT_CONNECT:     why = "connection failed: could not reach the server"; break;
	case ERROR_WINHTTP_TIMEOUT:            why = "connection failed: timed out"; break;
	case ERROR_WINHTTP_CONNECTION_ERROR:   why = "connection failed: the connection was interrupted"; break;
	default: break;
	}
	return std::string(why) + " (Windows error " + std::to_string(e) + ")";
}

struct Handle {
	HINTERNET h;
	explicit Handle(HINTERNET x) : h(x) { }
	~Handle() { if (h) WinHttpCloseHandle(h); }
	Handle(Handle const &) = delete;
};

HttpResult https_post_json(std::string const &url, std::string const &bearer, std::string const &body)
{
	HttpResult r;
	std::wstring const wurl = widen(url);

	URL_COMPONENTS uc = { };
	uc.dwStructSize = sizeof(uc);
	wchar_t host[256] = { }, path[2048] = { };
	uc.lpszHostName = host; uc.dwHostNameLength = 255;
	uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 2047;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
		r.error = "refusing a non-https URL"; return r;
	}

	Handle session(WinHttpOpen(L"HermesAgent/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session.h) { r.error = "WinHttpOpen failed"; return r; }
	WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 120000);

	/* TLS 1.2 and 1.3 only */
	DWORD protocols = 0x00000800 /* TLS 1.2 */ | 0x00002000 /* TLS 1.3 */;
	WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

	Handle conn(WinHttpConnect(session.h, host, uc.nPort, 0));
	if (!conn.h) { r.error = "cannot connect"; return r; }

	Handle req(WinHttpOpenRequest(conn.h, L"POST", path, nullptr, WINHTTP_NO_REFERER,
	                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if (!req.h) { r.error = "cannot open the request"; return r; }

	DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;           /* no redirects */
	WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

	std::wstring const hdr = L"Content-Type: application/json\r\nAuthorization: Bearer " + widen(bearer) + L"\r\n";
	if (!WinHttpAddRequestHeaders(req.h, hdr.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD)) {
		r.error = "cannot add headers"; return r;
	}

	if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body.data(),
	                        (DWORD)body.size(), (DWORD)body.size(), 0)) {
		DWORD const e = GetLastError();
		r.error = describe(e);
		if (e == ERROR_WINHTTP_SECURE_FAILURE || e == ERROR_WINHTTP_SECURE_INVALID_CERT ||
		    e == ERROR_WINHTTP_SECURE_CERT_CN_INVALID || e == ERROR_WINHTTP_SECURE_INVALID_CA)
			r.error += " (the server certificate was not accepted; nothing was sent)";
		return r;
	}
	if (!WinHttpReceiveResponse(req.h, nullptr)) {
		r.error = describe(GetLastError()); return r;
	}

	DWORD status = 0, sz = sizeof(status);
	WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
	r.status = (long)status;

	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
		if (r.body.size() + avail > MAX_REPLY) { r.error = "reply too large"; r.body.clear(); return r; }
		std::string chunk(avail, '\0');
		DWORD got = 0;
		if (!WinHttpReadData(req.h, &chunk[0], avail, &got) || got == 0) break;
		r.body.append(chunk.data(), got);
	}
	r.ok = true;
	return r;
}

} /* namespace hermes */
