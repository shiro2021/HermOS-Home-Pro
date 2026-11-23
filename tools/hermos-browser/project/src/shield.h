/*
 * Shield: what the HermOS Browser does to every request before it leaves the machine.
 *   - HTTPS only: http:// is upgraded to https://; there is no fallback to plain http.
 *   - Tracker and ad hosts from the blocklist (a host or any parent domain) are blocked.
 * Runs on the network thread, so the counters are atomic. No Q_OBJECT: the interface is virtual only.
 */
#pragma once
#include <atomic>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QtWebEngineCore/QWebEngineUrlRequestInterceptor>
#include <QtWebEngineCore/QWebEngineUrlRequestInfo>

class Shield : public QWebEngineUrlRequestInterceptor
{
	public:
		std::atomic<int>  blocked  { 0 };
		std::atomic<int>  upgraded { 0 };
		std::atomic<bool> enabled  { true };

		explicit Shield(QSet<QString> blocklist) : QWebEngineUrlRequestInterceptor(nullptr), _blocklist(std::move(blocklist)) { }

		/* one host per line; '#' starts a comment; "||example.com^" (Adblock style) is accepted too */
		static QSet<QString> parse_blocklist(QByteArray const &text)
		{
			QSet<QString> set;
			for (QByteArray line : text.split('\n')) {
				line = line.trimmed();
				if (line.isEmpty() || line.startsWith('#') || line.startsWith('!')) continue;
				if (line.startsWith("||")) line = line.mid(2);
				int const end = line.indexOf('^'); if (end >= 0) line = line.left(end);
				int const hash = line.indexOf('#'); if (hash >= 0) line = line.left(hash);
				QString const host = QString::fromUtf8(line.trimmed()).toLower();
				if (!host.isEmpty() && !host.contains('/') && !host.contains(' ')) set.insert(host);
			}
			return set;
		}

		int size() const { return _blocklist.size(); }

		bool is_blocked_host(QString host) const
		{
			host = host.toLower();
			for (;;) {
				if (_blocklist.contains(host)) return true;
				int const dot = host.indexOf('.');
				if (dot < 0) return false;
				host = host.mid(dot + 1);
				if (!host.contains('.')) return false;          /* never block a bare TLD */
			}
		}

		void interceptRequest(QWebEngineUrlRequestInfo &info) override
		{
			if (!enabled) return;
			QUrl const url = info.requestUrl();

			if (url.scheme() == "http" && !_is_local(url.host())) {
				QUrl up = url; up.setScheme("https");
				if (up.port() == 80) up.setPort(-1);
				info.redirect(up);
				upgraded++;
				return;
			}
			if ((url.scheme() == "https" || url.scheme() == "http") && is_blocked_host(url.host())) {
				/* never block the page the user asked for, only what it pulls in */
				if (info.resourceType() != QWebEngineUrlRequestInfo::ResourceTypeMainFrame) {
					info.block(true);
					blocked++;
				}
			}
		}

	private:
		QSet<QString> _blocklist;

		static bool _is_local(QString const &h)
		{
			return h == "localhost" || h.endsWith(".local") || h.startsWith("127.") || h.startsWith("10.")
			    || h.startsWith("192.168.") || h == "::1" || h == "[::1]";
		}
};
