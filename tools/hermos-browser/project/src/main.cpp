/*
 * HermOS Browser: a small, disposable web Cell. One page per Cell, one bar for addresses and questions,
 * nothing kept on disk, HTTPS only, trackers blocked. The engine is Qt WebEngine (Chromium); everything
 * you see is HermOS's own. No Q_OBJECT anywhere: signals go to lambdas.
 */
#include <QApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <QtWebEngineCore/QWebEngineCertificateError>
#include <QtWebEngineCore/QWebEngineCookieStore>
#include <QtWebEngineCore/QWebEngineDownloadRequest>
#include <QtWebEngineCore/QWebEngineHistory>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEnginePermission>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>

#include "shield.h"
#include "smartbar.h"
#include "theme.h"

namespace {

/* a link that wants a new window loads in this page: a second site is a second Cell */
class View : public QWebEngineView
{
	public:
		using QWebEngineView::QWebEngineView;
	protected:
		QWebEngineView *createWindow(QWebEnginePage::WebWindowType) override { return this; }
};

QByteArray read_all(QString const &path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} /* namespace */

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setApplicationName("HermOS Browser");

	Theme theme;
	theme.load("/hermos-browser.theme");

	/* the blocklist: a file in the root of the Cell's file system overrides the built-in starter list */
	QByteArray list = read_all("/blocklist.txt");
	if (list.isEmpty()) list = read_all(":/blocklist.txt");
	Shield shield(Shield::parse_blocklist(list));

	/* the disposable profile: no name means nothing is stored; the cache lives in memory */
	auto *profile = new QWebEngineProfile(&app);
	profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
	profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
	profile->setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::StoreInMemory);
	profile->setUrlRequestInterceptor(&shield);
	profile->setHttpUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
	                          "Chrome/122.0.0.0 Safari/537.36 HermOSBrowser/0.1");

	auto *root = new QWidget; root->setObjectName("root");
	auto *outer = new QVBoxLayout(root); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);

	/* the bar */
	auto *bar = new QWidget; bar->setObjectName("bar"); bar->setFixedHeight(theme.bar_h);
	auto *row = new QHBoxLayout(bar); row->setContentsMargins(8, 0, 8, 0); row->setSpacing(4);
	auto button = [&] (char const *text, char const *tip) {
		auto *b = new QToolButton; b->setText(text); b->setToolTip(tip); b->setAutoRaise(true); return b; };
	QToolButton *back = button("\xE2\x86\x90", "Back"), *fwd = button("\xE2\x86\x92", "Forward"), *reload = button("\xE2\x9F\xB3", "Reload");
	auto *field = new QLineEdit; field->setPlaceholderText("Type a website, or ask for something");
	field->setClearButtonEnabled(false);
	QToolButton *shield_btn = button("Shield", "Blocked trackers and ads, and upgrades to HTTPS (click to switch off for this page)");
	shield_btn->setObjectName("shield");
	QToolButton *wipe = button("Wipe", "Forget this page and start again");
	row->addWidget(back); row->addWidget(fwd); row->addWidget(reload); row->addWidget(field, 1);
	row->addWidget(shield_btn); row->addWidget(wipe);

	auto *progress = new QProgressBar; progress->setTextVisible(false); progress->setRange(0, 100);
	auto *view = new View;
	auto *page = new QWebEnginePage(profile, view);
	view->setPage(page);
	auto *status = new QLabel; status->setObjectName("status"); status->setFixedHeight(20);

	outer->addWidget(bar); outer->addWidget(progress); outer->addWidget(view, 1); outer->addWidget(status);

	/* settings that keep it small and fast: no GPU features, no built-in PDF viewer */
	QWebEngineSettings *s = view->settings();
	s->setAttribute(QWebEngineSettings::WebGLEnabled, false);
	s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, false);
	s->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, false);
	s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
	s->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
	s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
	s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
	s->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);

	auto say = [status] (QString const &t) { status->setText(t); };

	/* navigation */
	auto go = [&, view, field, say] (QString const &text) {
		hermos::Target const t = hermos::resolve(text.toStdString(), theme.search.toStdString());
		switch (t.kind) {
		case hermos::Kind::Nothing: break;
		case hermos::Kind::Refused: say(QString::fromStdString(t.text)); break;
		default: say(QString()); view->load(QUrl::fromUserInput(QString::fromStdString(t.text))); view->setFocus(); break; }
		(void)field;
	};
	QObject::connect(field, &QLineEdit::returnPressed, [go, field] { go(field->text()); });
	QObject::connect(back,   &QToolButton::clicked, view, &QWebEngineView::back);
	QObject::connect(fwd,    &QToolButton::clicked, view, &QWebEngineView::forward);
	QObject::connect(reload, &QToolButton::clicked, [view, progress] { progress->value() < 100 && progress->value() > 0 ? view->stop() : view->reload(); });

	QObject::connect(view, &QWebEngineView::urlChanged, [field, view] (QUrl const &u) {
		if (u.scheme() == "about" || u.isEmpty() || u.toString().startsWith("data:")) return;
		if (!field->hasFocus()) field->setText(u.toString());
		(void)view; });
	QObject::connect(view, &QWebEngineView::titleChanged, [root] (QString const &t) { root->setWindowTitle(t.isEmpty() ? "HermOS Browser" : t); });
	QObject::connect(view, &QWebEngineView::loadStarted, [progress, reload] { progress->setValue(5); reload->setText("\xE2\x9C\x95"); });
	QObject::connect(view, &QWebEngineView::loadProgress, [progress] (int p) { progress->setValue(p < 5 ? 5 : p); });
	QObject::connect(view, &QWebEngineView::loadFinished, [progress, reload, back, fwd, view, say] (bool ok) {
		progress->setValue(ok ? 100 : 0); reload->setText("\xE2\x9F\xB3");
		back->setEnabled(view->history()->canGoBack()); fwd->setEnabled(view->history()->canGoForward());
		if (!ok) say("This page could not be loaded."); });

	/* a disposable Cell asks for nothing and keeps nothing */
	QObject::connect(page, &QWebEnginePage::permissionRequested, [say] (QWebEnginePermission perm) {
		perm.deny(); say("A permission request was denied (this Cell is disposable)."); });
	QObject::connect(profile, &QWebEngineProfile::downloadRequested, [say] (QWebEngineDownloadRequest *d) {
		d->cancel(); say("Downloads are off in this disposable Cell."); });
	QObject::connect(page, &QWebEnginePage::certificateError, [say] (QWebEngineCertificateError e) {
		e.rejectCertificate(); say("The site's certificate was not accepted: " + e.description()); });

	/* the Shield: counters from the network thread, shown twice a second */
	auto refresh_shield = [&, shield_btn] {
		shield_btn->setText(shield.enabled ? QString("Shield \xC2\xB7 %1").arg((int)shield.blocked) : QString("Shield off"));
	};
	QObject::connect(shield_btn, &QToolButton::clicked, [&, refresh_shield] { shield.enabled = !shield.enabled; refresh_shield(); });
	QTimer timer; QObject::connect(&timer, &QTimer::timeout, refresh_shield); timer.start(500);
	refresh_shield();

	/* "Wipe": drop everything the page holds and go back to the start page */
	auto start_page = [view] { view->setHtml(QString::fromUtf8(read_all(":/start.html")), QUrl("about:blank")); };
	QObject::connect(wipe, &QToolButton::clicked, [&, profile, start_page, field, say] {
		profile->clearHttpCache(); profile->cookieStore()->deleteAllCookies(); shield.blocked = 0;
		field->clear(); start_page(); say("Wiped."); });

	/* keys */
	auto key = [root] (char const *seq, std::function<void()> fn) {
		auto *sc = new QShortcut(QKeySequence(seq), root); QObject::connect(sc, &QShortcut::activated, std::move(fn)); };
	key("Ctrl+L",   [field] { field->setFocus(); field->selectAll(); });
	key("Ctrl+R",   [view] { view->reload(); });
	key("F5",       [view] { view->reload(); });
	key("Escape",   [view] { view->stop(); });
	key("Alt+Left", [view] { view->back(); });
	key("Alt+Right",[view] { view->forward(); });
	key("Ctrl++",   [view] { view->setZoomFactor(std::min(view->zoomFactor() + 0.1, 3.0)); });
	key("Ctrl+=",   [view] { view->setZoomFactor(std::min(view->zoomFactor() + 0.1, 3.0)); });
	key("Ctrl+-",   [view] { view->setZoomFactor(std::max(view->zoomFactor() - 0.1, 0.3)); });
	key("Ctrl+0",   [view] { view->setZoomFactor(1.0); });

	/* the look follows focus: orange when this Cell is the active one, carbon gray when not */
	auto restyle = [&, root] (bool active) { root->setStyleSheet(theme.stylesheet(active)); };
	restyle(true);
	QObject::connect(&app, &QGuiApplication::applicationStateChanged, [restyle] (Qt::ApplicationState st) { restyle(st == Qt::ApplicationActive); });

	back->setEnabled(false); fwd->setEnabled(false);
	root->setWindowTitle("HermOS Browser");
	root->resize(1000, 700);
	root->show();

	/* a first address on the command line (the desktop passes it when a link is opened from the search bar) */
	QStringList const args = app.arguments();
	QString first;
	for (int i = 1; i < args.size(); ++i) if (!args[i].startsWith("--")) { first = args[i]; break; }
	if (!first.isEmpty()) { field->setText(first); go(first); } else { start_page(); field->setFocus(); }

	return app.exec();
}
