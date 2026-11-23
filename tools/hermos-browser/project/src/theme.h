/*
 * The look of the HermOS Browser, from a data file ("hermos-browser.theme", same "key: value" style as
 * theme.hid; colours are R,G,B). The values below are only the defaults used when a key is missing.
 */
#pragma once
#include <QColor>
#include <QFile>
#include <QString>

struct Theme
{
	QColor bg      { 11, 12, 14 };        /* page surround */
	QColor panel   { 17, 18, 21 };        /* the bar */
	QColor field   { 24, 25, 29 };        /* smart bar field */
	QColor text    { 226, 229, 233 };
	QColor muted   { 150, 156, 164 };
	QColor accent  { 255, 156, 48 };      /* focused / active */
	QColor deep    { 226, 92, 20 };
	QColor carbon  { 92, 96, 102 };       /* idle borders */
	QColor error   { 232, 84, 40 };
	int    radius  { 8 };
	int    bar_h   { 44 };
	QString search { "https://duckduckgo.com/?q=%s" };    /* search engine template */

	static bool rgb(QString const &v, QColor &out)
	{
		QStringList const p = v.split(',');
		if (p.size() != 3) return false;
		bool a, b, c; int const r = p[0].trimmed().toInt(&a), g = p[1].trimmed().toInt(&b), bl = p[2].trimmed().toInt(&c);
		if (!(a && b && c)) return false;
		out = QColor(r, g, bl); return true;
	}

	void load(QString const &path)
	{
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly)) return;
		for (QByteArray const &raw : f.readAll().split('\n')) {
			QString line = QString::fromUtf8(raw).trimmed();
			if (line.isEmpty() || line.startsWith('#')) continue;
			int const c = line.indexOf(':');
			if (c < 0) continue;
			QString const k = line.left(c).trimmed(), v = line.mid(c + 1).trimmed();
			struct { char const *k; QColor *c; } const cols[] = {
				{ "bg", &bg }, { "panel", &panel }, { "field", &field }, { "text", &text }, { "muted", &muted },
				{ "accent", &accent }, { "deep", &deep }, { "carbon", &carbon }, { "error", &error } };
			for (auto const &e : cols) if (k == e.k) rgb(v, *e.c);
			if (k == "radius") radius = v.toInt();
			if (k == "bar_h")  bar_h  = v.toInt();
			if (k == "search" && v.contains("%s") && v.startsWith("https://")) search = v;     /* https only */
		}
	}

	QString css(QColor const &c) const { return QString("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue()); }

	/* every colour, radius and size in the browser's own controls comes from here */
	QString stylesheet(bool active) const
	{
		QColor const edge = active ? accent : carbon;
		return QString(
			"QWidget#root { background: %1; }"
			"QWidget#bar { background: %2; border-bottom: 1px solid %3; }"
			"QToolButton { background: transparent; color: %4; border: none; border-radius: %5px; padding: 4px 10px; font-size: 16px; }"
			"QToolButton:hover { background: %6; }"
			"QToolButton:disabled { color: %7; }"
			"QToolButton#shield { font-size: 13px; color: %8; }"
			"QLineEdit { background: %6; color: %4; border: 1px solid %3; border-radius: %5px; padding: 6px 12px; font-size: 14px; selection-background-color: %9; }"
			"QLineEdit:focus { border: 1px solid %10; }"
			"QProgressBar { background: transparent; border: none; max-height: 3px; min-height: 3px; }"
			"QProgressBar::chunk { background: %10; }"
			"QLabel#status { color: %7; font-size: 12px; padding: 0 12px; background: %2; }")
			.arg(css(bg), css(panel), css(carbon), css(text), QString::number(radius))
			.arg(css(field), css(muted), css(accent), css(deep), css(edge));
	}
};
