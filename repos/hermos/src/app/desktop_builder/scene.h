/*
 * HermOS desktop scene — the whole "Shiro" desktop drawn immediate-mode.
 *
 * Geometry follows KDE Plasma's panel model (edge, thickness, visibility mode,
 * reveal animation with ease-out-cubic, client-area = work area minus the
 * struts of visible panels). The layout is authored in a 1536x1024 design
 * space and mapped to the real framebuffer by one uniform scale, so the
 * desktop is resolution independent. Everything is drawn from analytic
 * shapes (see scene_gfx.h); only the compass emblem, the hydroponics photo
 * and the wallpaper are bitmaps.
 */

#ifndef _HERMOS_SCENE_H_
#define _HERMOS_SCENE_H_

#include "scene_gfx.h"

namespace Scene {

/* ---------------------------------------------------------------- palette */

namespace Pal {
	static constexpr Rgba white   { 241, 244, 247, 1.0f };
	static constexpr Rgba text    { 226, 229, 233, 1.0f };
	static constexpr Rgba muted   { 150, 156, 164, 1.0f };
	static constexpr Rgba orange  { 255, 156, 48, 1.0f };
	static constexpr Rgba orange2 { 232, 108, 26, 1.0f };
	static constexpr Rgba peach   { 255, 190, 118, 1.0f };
	static constexpr Rgba green   { 84, 214, 108, 1.0f };
	static constexpr Rgba panel   { 11, 12, 14, 1.0f };
	static constexpr Rgba glass   { 16, 17, 20, 1.0f };
	static constexpr Rgba line    { 62, 60, 58, 1.0f };
}

/* -------------------------------------------- Plasma-style panel + clientArea */

struct Rect { float x, y, w, h; };

enum class Visibility { Hidden, AutoHide, AlwaysVisible };
enum class Edge { Left, Bottom, Right };

static inline float ease_out_cubic(float t) { t = 1.0f - clampf(t, 0, 1); return 1.0f - t * t * t; }

struct Panel
{
	Edge       edge;
	Visibility mode;
	float      thickness;     /* design px */
	float      t { 0 };       /* 0 = hidden, 1 = fully shown (linear time) */
	bool       pinned { false };    /* user pressed "show": overrides AutoHide */
	bool       hovering { false };  /* pointer on the edge / over the panel   */

	bool wants_shown() const
	{
		if (pinned) return true;
		switch (mode) {
		case Visibility::AlwaysVisible: return true;
		case Visibility::AutoHide:      return hovering;
		case Visibility::Hidden:        return false;
		}
		return false;
	}

	/* advance the reveal animation; returns true while it is still moving */
	bool update(float dt_ms)
	{
		float const target = wants_shown() ? 1.0f : 0.0f;
		float const step   = dt_ms / 240.0f;     /* Plasma's panel animation: ~240 ms */
		float const prev   = t;
		if (t < target) t = minf(t + step, target); else if (t > target) t = maxf(t - step, target);
		return t != prev;
	}

	float shown() const { return ease_out_cubic(t); }   /* eased 0..1 */
	bool  visible() const { return t > 0.001f; }
	/* user toggle (Meta+L / Meta+B / clicking the grip): flip between a
	 * permanently shown and a hidden panel */
	void  toggle()
	{
		if (wants_shown()) { pinned = false; mode = Visibility::Hidden; }
		else               { mode = Visibility::AlwaysVisible; }
	}
};

/* Plasma "clientArea": the work area left after the struts of panels that are
 * permanently visible (auto-hide / floating panels reserve no space). */
static inline Rect client_area(Rect screen, Panel const &left, Panel const &bottom)
{
	Rect r = screen;
	if (left.mode == Visibility::AlwaysVisible || left.pinned)     { r.x += left.thickness; r.w -= left.thickness; }
	if (bottom.mode == Visibility::AlwaysVisible || bottom.pinned) { r.h -= bottom.thickness; }
	return r;
}

/* --------------------------------------------------------- assets + fonts */

struct Fonts  { Font reg, med, semi; };
struct Assets { Image wall, compass, hydro_thumb, hydro_big; };

struct WinRect { float x, y, w, h; };

static constexpr int MAX_APP_ENTRIES = 10;

/* hit areas the desktop_builder uses for the panels */
struct Hits
{
	Rect left_grip, bottom_grip, left_panel, bottom_panel;
	Rect pane_reveal;                   /* the slim bar left on the edge while the pane is hidden */
	Rect pane_close;                    /* the X of the Agents & Workers pane (hides it) */
	Rect rail[6];                       /* navigation rail tiles; tile 0 (Agents) toggles the pane */
	Rect launcher[4];
	int  launcher_count { 0 };
	Rect app_play[MAX_APP_ENTRIES];     /* launch (inactive) */
	Rect app_close[MAX_APP_ENTRIES];    /* stop (active) */
	int  app_play_n { 0 };
};

/* Shiro Command Bar style, in canvas pixels, published by the theme engine.
 * `valid` is false until the first theme_state arrives. */
struct CmdBar
{
	bool  valid { false };
	float x { 0 }, y { 0 }, w { 0 };               /* frame position and width */
	float font { 0 };                               /* ImGui FontSize */
	float pad_x { 0 }, pad_y { 0 };                 /* ImGui FramePadding */
	float spacing { 0 };                            /* ImGui ItemInnerSpacing */
	float tip { 0 };                                /* depth of the pointed ends */
	float greet_font { 0 }, sub_font { 0 };
};

/* what the desktop really knows about the system (filled by desktop_builder) */
struct SysInfo
{
	char version[56]  { "HermOS V5 Beta" };
	char kernel[56]   { "" };
	char uptime[32]   { "" };
	char memory[64]   { "" };
	char cpu[72]      { "" };
	char graphics[56] { "" };
	char storage[72]  { "" };
	char network[56]  { "" };
};

/* an app/service the sidebar lists: running ones on top, launchable ones below */
struct AppEntry
{
	char name[24] { "" };
	char sub[40]  { "" };
	int  kind     { 0 };      /* 0 files, 1 terminal, 2 installer, 3 agent */
	bool active   { false };
	int  slot     { -1 };     /* the cell it runs in, -1 = none */
};
/* Cell frame look. Every value comes from theme.hid (`+ frame`); the defaults only apply
 * until the first theme_state arrives. Lengths are design px. Only the active Cell glows. */
struct FrameStyle
{
	float border       { 1.6f };
	float glow_reach   { 16.0f }, glow_sigma   { 5.5f };
	float shadow_reach { 8.0f  }, shadow_sigma { 3.0f };
	Rgba  glow         { 255, 112, 22, 0.58f };
	Rgba  active_a     { 255, 200, 126, 1.0f };   /* active border gradient: light ... */
	Rgba  active_b     { 226, 92, 20, 1.0f };     /* ... deep orange */
	Rgba  carbon_a     { 92, 96, 102, 1.0f };     /* inactive border: carbon gray gradient */
	Rgba  carbon_b     { 48, 51, 56, 1.0f };
	float bloom_reach  { 6.0f }, bloom_sigma { 1.8f }; /* tight hot halo hugging the rim */
	float bloom_alpha  { 0.85f };
	float bezel        { 11.0f };               /* dark carbon bezel just inside the rim */
	float bezel_alpha  { 0.80f };
	Rgba  core         { 255, 214, 150, 1.0f };  /* white-hot centre line of an active rim */
	Rgba  shadow       { 0, 0, 0, 0.38f };        /* inactive Cells only get a soft drop shadow */
};

struct State
{
	FrameStyle frame { };
	Ink        ink   { };
	CmdBar   cmdbar { };
	SysInfo  sys { };
	AppEntry apps[MAX_APP_ENTRIES] { };
	int      app_count { 0 };
	bool     payload_ok { false };   /* this medium can install HermOS */
	bool     preview    { false };   /* show the mock-up preview windows */

	int   cw { 1536 }, ch { 1024 };
	Panel left   { Edge::Left,   Visibility::AutoHide,      78.0f };
	Panel bottom { Edge::Bottom, Visibility::AlwaysVisible, 28.0f };
	Panel right  { Edge::Right,  Visibility::AlwaysVisible, 356.0f };   /* Agents & Workers pane + rail */
	float right_slim { 26.0f };
	float right_rail { 92.0f };            /* width of the navigation rail beside the pane (design px) */            /* width of the slim bar that stays when the pane is hidden (design px) */

	char const *clock   { "Tue 10 Dec 2024 07:42" };
	char const *prompt  { nullptr };       /* user-typed text, null = placeholder */
	int   active_win    { 0 };
	bool  win_open[6]   { true, true, true, true, true, true };
	WinRect win[6];                        /* pixel rects (already in canvas space) */
	bool  win_hosted[6] { };               /* an app owns the content area       */
	bool  search_focus  { false };
	int   rail_active   { 0 };             /* Agents */
	int   hover_win     { -1 };
	int   order[6]      { 0, 1, 2, 3, 4, 5 };   /* z-order, bottom -> top */
};

/* design <-> canvas mapping (one uniform scale, letterboxed) */
struct Map
{
	float k, ox, oy;
	float X(float x) const { return ox + x * k; }
	float Y(float y) const { return oy + y * k; }
	bool  hit(Rect const &r, int px, int py) const
	{ return float(px) >= X(r.x) && float(px) < X(r.x + r.w) && float(py) >= Y(r.y) && float(py) < Y(r.y + r.h); }
};
static inline Map map_for(int w, int h)
{
	float const k = minf(float(w) / 1536.0f, float(h) / 1024.0f);
	return { k, (float(w) - 1536.0f * k) * 0.5f, (float(h) - 1024.0f * k) * 0.5f };
}

static constexpr float DW = 1536.0f, DH = 1024.0f;   /* design size */
static constexpr float TASKBAR_H = 28.0f;
/* the bottom bar: full width, on the bottom edge of the screen */
static constexpr Rect TASKBAR_RECT { 0, DH - TASKBAR_H, DW, TASKBAR_H };

/* -------------------------------------------------------------- the Ctx */

struct Ctx
{
	Canvas &c; Fonts &f; Assets &a;
	float k, ox, oy;                       /* design -> canvas */

	float X(float x) const { return ox + x * k; }
	float Y(float y) const { return oy + y * k; }
	float L(float v) const { return v * k; }

	/* text in design coordinates: (x,y) = top-left of the line box */
	float text(Font &fo, float x, float y, float size, Rgba col, char const *s, float tr = 0, float bold = 0)
	{ return draw_text(c, fo, X(x), Y(y), size * k, col, s, tr * k, bold) / k; }
	float textc(Font &fo, float cx, float y, float size, Rgba col, char const *s, float tr = 0, float bold = 0)
	{ return draw_text_center(c, fo, X(cx), Y(y), size * k, col, s, tr * k, bold) / k; }
	float textr(Font &fo, float rx, float y, float size, Rgba col, char const *s, float tr = 0, float bold = 0)
	{ return draw_text_right(c, fo, X(rx), Y(y), size * k, col, s, tr * k, bold) / k; }
	float tw(Font &fo, float size, char const *s, float tr = 0) { return text_width(fo, size * k, s, tr * k) / k; }

	void ln(float x0, float y0, float x1, float y1, float t, Rgba col) { line(c, X(x0), Y(y0), X(x1), Y(y1), t * k, col); }
	void rr_fill(float x, float y, float w, float h, float r, Rgba col)
	{ fill_rrect(c, X(x), Y(y), L(w), L(h), L(r), Solid { col }); }
	template <class COL> void rr_fillg(float x, float y, float w, float h, float r, COL col)
	{ fill_rrect(c, X(x), Y(y), L(w), L(h), L(r), col); }
	void rr_stroke(float x, float y, float w, float h, float r, float t, Rgba col)
	{ stroke_rrect(c, X(x), Y(y), L(w), L(h), L(r), L(t), Solid { col }); }
	void disc(float x, float y, float r, Rgba col) { fill_circle(c, X(x), Y(y), L(r), col); }
	void rng(float x, float y, float r, float t, Rgba col) { ring(c, X(x), Y(y), L(r), L(t), col); }
	void arcs(float x, float y, float r, float a0, float a1, float t, Rgba col) { arc(c, X(x), Y(y), L(r), a0, a1, L(t), col); }
};

/* ------------------------------------------------------------------ icons */

namespace Icon {

/* all icons are drawn into a 24x24 design box centred at (cx,cy), scaled by s */

static inline void gear(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.7f)
{
	g.rng(cx, cy, 7.2f * s, t * s, col);
	g.rng(cx, cy, 2.8f * s, t * s, col);
	for (int i = 0; i < 8; ++i) {
		float const a = float(i) * PI_ / 4.0f;
		g.ln(cx + cosf_(a) * 7.6f * s, cy + sinf_(a) * 7.6f * s, cx + cosf_(a) * 10.2f * s, cy + sinf_(a) * 10.2f * s, 2.6f * s, col);
	}
}
static inline void folder(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{
	Rect const r { cx - 10 * s, cy - 7 * s, 20 * s, 15 * s };
	g.rr_stroke(r.x, r.y + 2.5f * s, r.w, r.h - 2.5f * s, 2.5f * s, t * s, col);
	g.ln(r.x + 1.5f * s, r.y + 5.0f * s, r.x + 1.5f * s, r.y + 1.0f * s, t * s, col);
	g.ln(r.x + 1.5f * s, r.y + 1.0f * s, r.x + 7.5f * s, r.y + 1.0f * s, t * s, col);
	g.ln(r.x + 7.5f * s, r.y + 1.0f * s, r.x + 9.5f * s, r.y + 4.0f * s, t * s, col);
	g.ln(r.x + 2.0f * s, r.y + 8.5f * s, r.x + r.w - 2.0f * s, r.y + 8.5f * s, t * s, col);
}
static inline void network(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{
	g.arcs(cx, cy + 4 * s, 4.5f * s, -PI_ * 0.85f, -PI_ * 0.15f, t * s, col);
	g.arcs(cx, cy + 4 * s, 8.0f * s, -PI_ * 0.85f, -PI_ * 0.15f, t * s, col);
	g.arcs(cx, cy + 4 * s, 11.5f * s, -PI_ * 0.82f, -PI_ * 0.18f, t * s, col);
	g.disc(cx, cy + 4 * s, 1.6f * s, col);
}
static inline void monitor(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{
	g.rr_stroke(cx - 9 * s, cy - 8 * s, 18 * s, 13 * s, 2.0f * s, t * s, col);
	g.ln(cx - 4 * s, cy + 9 * s, cx + 4 * s, cy + 9 * s, t * s, col);
	g.ln(cx, cy + 5 * s, cx, cy + 9 * s, t * s, col);
}
static inline void devices(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{
	g.rr_stroke(cx - 8 * s, cy - 9 * s, 16 * s, 18 * s, 3.0f * s, t * s, col);
	g.rng(cx, cy - 1.5f * s, 2.6f * s, t * s, col);
	g.arcs(cx, cy + 6.0f * s, 4.0f * s, PI_, 2 * PI_, t * s, col);
}
static inline void agents(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.8f)
{
	g.rng(cx, cy - 4.0f * s, 3.4f * s, t * s, col);
	g.arcs(cx, cy + 9.0f * s, 8.0f * s, PI_ * 1.12f, PI_ * 1.88f, t * s, col);
	g.rng(cx - 8.0f * s, cy - 1.0f * s, 2.4f * s, (t - 0.3f) * s, col);
	g.rng(cx + 8.0f * s, cy - 1.0f * s, 2.4f * s, (t - 0.3f) * s, col);
}
static inline void user(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.7f)
{
	g.rng(cx, cy - 4.0f * s, 3.8f * s, t * s, col);
	g.arcs(cx, cy + 10.0f * s, 8.5f * s, PI_ * 1.15f, PI_ * 1.85f, t * s, col);
}
static inline void leaf(Ctx &g, float cx, float cy, float s, Rgba col)
{
	/* two-lobed leaf: fill a rotated lens + a vein */
	Poly p; p.n = 0;
	p.add(cx - 9 * s, cy + 9 * s); p.add(cx - 8 * s, cy - 1 * s); p.add(cx - 2 * s, cy - 8 * s);
	p.add(cx + 9 * s, cy - 10 * s); p.add(cx + 10 * s, cy - 1 * s); p.add(cx + 4 * s, cy + 6 * s);
	p.add(cx - 3 * s, cy + 9 * s);
	/* non-convex-safe: draw as a thick polyline lens instead */
	(void)p;
	for (int i = 0; i <= 14; ++i) {
		float const u = float(i) / 14.0f;
		float const wdt = 1.0f - fabsf_(2 * u - 1);
		float const x = lerpf(cx - 7.5f * s, cx + 8.0f * s, u), y = lerpf(cy + 8.0f * s, cy - 8.5f * s, u);
		g.disc(x, y, (1.2f + 6.2f * wdt) * s, col);
	}
	g.ln(cx - 8.5f * s, cy + 10.0f * s, cx + 1.5f * s, cy - 1.0f * s, 1.5f * s, Rgba { 22, 56, 30, 0.9f });
}
static inline void wrench(Ctx &g, float cx, float cy, float s, Rgba col)
{
	g.ln(cx - 8 * s, cy + 8 * s, cx + 5 * s, cy - 5 * s, 3.4f * s, col);
	g.rng(cx + 6 * s, cy - 6 * s, 4.6f * s, 3.0f * s, col);
	g.ln(cx + 8 * s, cy + 8 * s, cx - 5 * s, cy - 5 * s, 3.4f * s, col);
	g.rng(cx - 6 * s, cy - 6 * s, 4.6f * s, 3.0f * s, col);
}
static inline void cloud(Ctx &g, float cx, float cy, float s, Rgba col)
{
	g.disc(cx - 5 * s, cy + 2 * s, 5.5f * s, col);
	g.disc(cx + 1 * s, cy - 2.5f * s, 7.0f * s, col);
	g.disc(cx + 7 * s, cy + 2.5f * s, 5.0f * s, col);
	g.rr_fill(cx - 10 * s, cy + 2 * s, 21 * s, 6 * s, 3 * s, col);
}
static inline void lock(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{
	g.rr_stroke(cx - 7 * s, cy - 1 * s, 14 * s, 11 * s, 2.5f * s, t * s, col);
	g.arcs(cx, cy - 2.0f * s, 4.6f * s, PI_, 2 * PI_, t * s, col);
	g.disc(cx, cy + 4 * s, 1.5f * s, col);
}
static inline void flask(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{
	g.ln(cx - 3 * s, cy - 9 * s, cx - 3 * s, cy - 2 * s, t * s, col);
	g.ln(cx + 3 * s, cy - 9 * s, cx + 3 * s, cy - 2 * s, t * s, col);
	g.ln(cx - 3 * s, cy - 2 * s, cx - 8 * s, cy + 8 * s, t * s, col);
	g.ln(cx + 3 * s, cy - 2 * s, cx + 8 * s, cy + 8 * s, t * s, col);
	g.ln(cx - 8 * s, cy + 8 * s, cx + 8 * s, cy + 8 * s, t * s, col);
	g.ln(cx - 5 * s, cy - 9 * s, cx + 5 * s, cy - 9 * s, t * s, col);
}
static inline void bolt(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.7f)
{
	g.rng(cx, cy, 8.5f * s, t * s, col);
	g.ln(cx + 1.5f * s, cy - 5 * s, cx - 2.5f * s, cy + 0.5f * s, t * s, col);
	g.ln(cx - 2.5f * s, cy + 0.5f * s, cx + 2.5f * s, cy + 0.5f * s, t * s, col);
	g.ln(cx + 2.5f * s, cy + 0.5f * s, cx - 1.5f * s, cy + 6 * s, t * s, col);
}
static inline void search(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.8f)
{
	g.rng(cx - 1.5f * s, cy - 1.5f * s, 6.2f * s, t * s, col);
	g.ln(cx + 3.0f * s, cy + 3.0f * s, cx + 8.0f * s, cy + 8.0f * s, t * s, col);
}
static inline void arrow_right(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.8f)
{
	g.ln(cx - 7 * s, cy, cx + 7 * s, cy, t * s, col);
	g.ln(cx + 1.5f * s, cy - 5.5f * s, cx + 7 * s, cy, t * s, col);
	g.ln(cx + 1.5f * s, cy + 5.5f * s, cx + 7 * s, cy, t * s, col);
}
static inline void check(Ctx &g, float cx, float cy, float s, Rgba col, float t = 2.4f)
{
	g.ln(cx - 7 * s, cy + 0.5f * s, cx - 2 * s, cy + 5.5f * s, t * s, col);
	g.ln(cx - 2 * s, cy + 5.5f * s, cx + 8 * s, cy - 5.5f * s, t * s, col);
}
static inline void play(Ctx &g, float cx, float cy, float s, Rgba col)
{
	Poly p; p.n = 0; p.add(cx - 5 * s, cy - 7 * s); p.add(cx + 7 * s, cy); p.add(cx - 5 * s, cy + 7 * s); p.finish();
	Canvas &c = g.c;
	Poly q; q.n = 0;
	for (int i = 0; i < p.n; ++i) q.add(g.X(p.v[i].x), g.Y(p.v[i].y));
	q.finish();
	fill_poly(c, q, Solid { col });
}
static inline void dots(Ctx &g, float cx, float cy, float s, Rgba col)
{
	g.disc(cx - 7 * s, cy, 1.5f * s, col); g.disc(cx, cy, 1.5f * s, col); g.disc(cx + 7 * s, cy, 1.5f * s, col);
}
static inline void cube(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.7f)
{
	g.ln(cx, cy - 10 * s, cx + 9 * s, cy - 5 * s, t * s, col); g.ln(cx + 9 * s, cy - 5 * s, cx + 9 * s, cy + 5 * s, t * s, col);
	g.ln(cx + 9 * s, cy + 5 * s, cx, cy + 10 * s, t * s, col); g.ln(cx, cy + 10 * s, cx - 9 * s, cy + 5 * s, t * s, col);
	g.ln(cx - 9 * s, cy + 5 * s, cx - 9 * s, cy - 5 * s, t * s, col); g.ln(cx - 9 * s, cy - 5 * s, cx, cy - 10 * s, t * s, col);
	g.ln(cx - 9 * s, cy - 5 * s, cx, cy, t * s, col); g.ln(cx + 9 * s, cy - 5 * s, cx, cy, t * s, col); g.ln(cx, cy, cx, cy + 10 * s, t * s, col);
}
static inline void chevron_down(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{ g.ln(cx - 4 * s, cy - 2 * s, cx, cy + 2 * s, t * s, col); g.ln(cx, cy + 2 * s, cx + 4 * s, cy - 2 * s, t * s, col); }
static inline void close_x(Ctx &g, float cx, float cy, float s, Rgba col, float t = 2.0f)
{ g.ln(cx - 5 * s, cy - 5 * s, cx + 5 * s, cy + 5 * s, t * s, col); g.ln(cx + 5 * s, cy - 5 * s, cx - 5 * s, cy + 5 * s, t * s, col); }
static inline void minus(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.8f)
{ g.ln(cx - 5 * s, cy, cx + 5 * s, cy, t * s, col); }
static inline void maximize(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.6f)
{ g.rr_stroke(cx - 5 * s, cy - 5 * s, 10 * s, 10 * s, 2.0f * s, t * s, col); }
static inline void undo(Ctx &g, float cx, float cy, float s, Rgba col, float t = 1.5f)
{
	g.arcs(cx + 0.5f * s, cy + 0.5f * s, 4.0f * s, -PI_ * 0.3f, PI_ * 0.95f, t * s, col);
	g.ln(cx - 3.8f * s, cy - 3.8f * s, cx - 3.8f * s, cy + 0.2f * s, t * s, col);
	g.ln(cx - 3.8f * s, cy - 3.8f * s, cx + 0.2f * s, cy - 3.8f * s, t * s, col);
}
static inline void small_square(Ctx &g, float cx, float cy, float s, Rgba col, bool dot = false)
{
	g.rr_stroke(cx - 6 * s, cy - 6 * s, 12 * s, 12 * s, 2.5f * s, 1.4f * s, col);
	if (dot) g.disc(cx, cy, 1.8f * s, col);
	else     g.ln(cx - 2.5f * s, cy, cx + 2.5f * s, cy, 1.3f * s, col);
}
static inline void star4(Ctx &g, float cx, float cy, float s, Rgba col)
{
	g.ln(cx, cy - 5 * s, cx, cy + 5 * s, 1.6f * s, col); g.ln(cx - 5 * s, cy, cx + 5 * s, cy, 1.6f * s, col);
	g.ln(cx - 2.8f * s, cy - 2.8f * s, cx + 2.8f * s, cy + 2.8f * s, 1.2f * s, col); g.ln(cx + 2.8f * s, cy - 2.8f * s, cx - 2.8f * s, cy + 2.8f * s, 1.2f * s, col);
}

} /* namespace Icon */

/* ------------------------------------------------- shared decorative bits */


static inline Rgba orange_edge(float t)   /* top-light -> bottom-deep border gradient */
{ return mix(Rgba { 255, 188, 112, 1.0f }, Rgba { 226, 96, 22, 1.0f }, clampf(t, 0, 1)); }

/* hexagonal tile (pointy left/right) used by the navigation rail */
static inline void hex_tile(Ctx &g, float cx, float cy, float w, float h, bool active)
{
	float const hw = w * 0.5f, hh = h * 0.5f, tip = hw * 0.42f;
	Poly p; p.n = 0;
	p.add(g.X(cx - hw), g.Y(cy)); p.add(g.X(cx - hw + tip), g.Y(cy - hh)); p.add(g.X(cx + hw - tip), g.Y(cy - hh));
	p.add(g.X(cx + hw), g.Y(cy)); p.add(g.X(cx + hw - tip), g.Y(cy + hh)); p.add(g.X(cx - hw + tip), g.Y(cy + hh)); p.finish();
	if (active) {
		glow_poly(g.c, p, g.L(12), g.L(4), Rgba { 255, 130, 30, 0.60f });
		fill_poly(g.c, p, VGrad { g.Y(cy - hh), g.Y(cy + hh), Rgba { 44, 26, 14, 1.0f }, Rgba { 22, 14, 9, 1.0f } });
		stroke_poly(g.c, p, g.L(2.0f), VGrad { g.Y(cy - hh), g.Y(cy + hh), Rgba { 255, 196, 130, 1.0f }, Rgba { 255, 110, 26, 1.0f } });
	} else {
		fill_poly(g.c, p, VGrad { g.Y(cy - hh), g.Y(cy + hh), Rgba { 38, 38, 40, 1.0f }, Rgba { 20, 20, 22, 1.0f } });
		stroke_poly(g.c, p, g.L(1.2f), Solid { Rgba { 104, 88, 70, 0.9f } });
	}
}

/* hexagon with points at the top and bottom (the mockup's rail tiles) */
static inline void hex_tile_v(Ctx &g, float cx, float cy, float w, float h, bool active)
{
	float const hw = w * 0.5f, hh = h * 0.5f, q = hh * 0.5f;
	Poly p; p.n = 0;
	p.add(g.X(cx), g.Y(cy - hh)); p.add(g.X(cx + hw), g.Y(cy - q)); p.add(g.X(cx + hw), g.Y(cy + q));
	p.add(g.X(cx), g.Y(cy + hh)); p.add(g.X(cx - hw), g.Y(cy + q)); p.add(g.X(cx - hw), g.Y(cy - q)); p.finish();
	if (active) {
		glow_poly(g.c, p, g.L(12), g.L(4), Rgba { 255, 130, 30, 0.60f });
		fill_poly(g.c, p, VGrad { g.Y(cy - hh), g.Y(cy + hh), Rgba { 44, 26, 14, 1.0f }, Rgba { 22, 14, 9, 1.0f } });
		stroke_poly(g.c, p, g.L(2.0f), VGrad { g.Y(cy - hh), g.Y(cy + hh), Rgba { 255, 196, 130, 1.0f }, Rgba { 255, 110, 26, 1.0f } });
	} else {
		fill_poly(g.c, p, VGrad { g.Y(cy - hh), g.Y(cy + hh), Rgba { 38, 38, 40, 1.0f }, Rgba { 20, 20, 22, 1.0f } });
		stroke_poly(g.c, p, g.L(1.2f), Solid { Rgba { 104, 88, 70, 0.9f } });
	}
}

/* ------------------------------------------------------------ the header */

static inline void draw_header(Ctx &g)
{
	/* compass emblem (bitmap, keyed by its own alpha) */
	float const cx = 82, cy = 58, r = 42;
	glow_sd(g.c, g.X(cx - r), g.Y(cy - r), g.X(cx + r), g.Y(cy + r), g.L(14), g.L(5), Rgba { 255, 120, 20, 0.30f },
	        [&](float px, float py) { return sd_circle(px, py, g.X(cx), g.Y(cy), g.L(r - 4)); });
	draw_image(g.c, g.a.compass, g.X(cx - r), g.Y(cy - r), g.L(2 * r), g.L(2 * r),
	           [&](float px, float py) { return sd_circle(px, py, g.X(cx), g.Y(cy), g.L(r)); });

	/* wordmark: HermOS with the orange "M" stroke through the H */
	float const wx = 146, wy = 32, ws = 27.5f;
	g.text(g.f.reg, wx, wy, ws, Pal::white, "H", 0, 0.45f);
	float const hw_ = g.tw(g.f.reg, ws, "H");
	/* orange M-notch inside the H */
	g.ln(wx + 1.0f, wy + 25.0f, wx + 5.8f, wy + 14.0f, 2.6f, Pal::orange);
	g.ln(wx + 5.8f, wy + 14.0f, wx + 10.5f, wy + 24.0f, 2.6f, Pal::orange);
	g.ln(wx + 10.5f, wy + 24.0f, wx + hw_ - 1.0f, wy + 13.0f, 2.6f, Pal::orange);
	g.text(g.f.reg, wx + hw_ + 7.5f, wy, ws, Pal::white, "erm", 7.5f, 0.45f);
	float const x2 = wx + hw_ + 7.5f + g.tw(g.f.reg, ws, "erm", 7.5f) + 7.5f;
	g.text(g.f.reg, x2, wy, ws, Pal::white, "OS", 7.5f, 0.45f);
	g.text(g.f.med, 146, 66, 12.4f, Pal::text, "A MORE HUMAN COMPUTER", 1.8f);
}

/* ----------------------------------------------- greeting + the search bar */

static inline Poly search_shape(Ctx &g, float x, float y, float w, float h, float tip)
{
	Poly p; p.n = 0;
	p.add(g.X(x), g.Y(y + h / 2)); p.add(g.X(x + tip), g.Y(y)); p.add(g.X(x + w - tip), g.Y(y));
	p.add(g.X(x + w), g.Y(y + h / 2)); p.add(g.X(x + w - tip), g.Y(y + h)); p.add(g.X(x + tip), g.Y(y + h)); p.finish();
	return p;
}

/* Until the theme arrives: proportions of the screen, never pixels. */
static inline CmdBar cmdbar_fallback(int cw, int ch)
{
	CmdBar b; b.valid = true;
	float const H = float(ch);
	b.w = float(cw) * 0.3411f; b.x = float(cw) * 0.1868f; b.y = H * 0.2900f;
	b.font = H * 0.0127f; b.pad_x = H * 0.0234f; b.pad_y = H * 0.0205f; b.spacing = H * 0.0098f;
	b.tip = H * 0.0244f; b.greet_font = H * 0.0322f; b.sub_font = H * 0.0154f;
	return b;
}

/* The bar laid out in design units (ImGui rules). One function feeds both the
 * drawing and the hit test, so they cannot drift apart. */
struct CmdBarLayout
{
	float x, y, w, h;                    /* frame (design units) */
	float tip, spacing, font, pad_x;
	float btn;                           /* icon button diameter = frame height - spacing */
	float greet_font, sub_font;
};

static inline CmdBarLayout cmdbar_layout(State const &st)
{
	CmdBar const b = st.cmdbar.valid ? st.cmdbar : cmdbar_fallback(st.cw, st.ch);
	Map const m = map_for(st.cw, st.ch);
	float const u = 1.0f / m.k;
	CmdBarLayout l;
	l.x = (b.x - m.ox) * u; l.y = (b.y - m.oy) * u; l.w = b.w * u;
	l.font = b.font * u; l.pad_x = b.pad_x * u; l.spacing = b.spacing * u; l.tip = b.tip * u;
	l.h = l.font + 2.0f * b.pad_y * u;                       /* ImGui::GetFrameHeight() */
	l.btn = l.h - l.spacing;
	l.greet_font = b.greet_font * u; l.sub_font = b.sub_font * u;
	return l;
}

static inline Rect search_rect(State const &st)
{
	CmdBarLayout const l = cmdbar_layout(st);
	return { l.x, l.y, l.w, l.h };
}

static inline void hex_button(Ctx &g, float cx, float cy, float d, Rgba fill, Rgba edge)
{
	Poly hx; hx.n = 0;
	for (int i = 0; i < 6; ++i) { float const a = PI_ / 6 + float(i) * PI_ / 3; hx.add(g.X(cx + cosf_(a) * d * 0.5f), g.Y(cy + sinf_(a) * d * 0.5f)); }
	hx.finish();
	fill_poly(g.c, hx, Solid { fill });
	stroke_poly(g.c, hx, g.L(1.0f), Solid { edge });
}

static inline void draw_greeting_search(Ctx &g, State const &st)
{
	CmdBarLayout const l = cmdbar_layout(st);
	float const cx = l.x + l.w * 0.5f, cy = l.y + l.h * 0.5f;

	/* greeting: stacked above the bar by ItemSpacing, centred on it */
	float const sub_h = line_height(g.f.reg, l.sub_font * g.k) / g.k;
	float const ttl_h = line_height(g.f.med, l.greet_font * g.k) / g.k;
	float const sub_top = l.y - 2.0f * l.spacing - sub_h;
	float const ttl_top = sub_top - l.spacing - ttl_h;
	g.textc(g.f.med, cx, ttl_top, l.greet_font, Pal::white, "Good Morning");
	g.textc(g.f.reg, cx, sub_top, l.sub_font, Rgba { 245, 247, 250, 0.95f }, "How can I help you today?");

	/* the ONE command bar: hexagonal pill, glass fill, orange edge */
	Poly p = search_shape(g, l.x, l.y, l.w, l.h, l.tip);
	glow_poly(g.c, p, g.L(22), g.L(7), Rgba { 255, 120, 24, st.search_focus ? 0.60f : 0.38f });
	fill_poly(g.c, p, VGrad { g.Y(l.y), g.Y(l.y + l.h), Rgba { 24, 26, 30, 0.90f }, Rgba { 12, 13, 15, 0.92f } });
	stroke_poly(g.c, p, g.L(st.search_focus ? 2.6f : 2.0f), VGrad { g.Y(l.y), g.Y(l.y + l.h), Rgba { 255, 190, 112, 1.0f }, Rgba { 232, 102, 24, 1.0f } });

	/* content row, left to right, the way ImGui lays out a frame:
	 *   [tip][spacing][icon button][spacing][text ...........][spacing][button][spacing][tip] */
	float const bx = l.x + l.tip + l.spacing + l.btn * 0.5f;
	float const ax = l.x + l.w - l.tip - l.spacing - l.btn * 0.5f;
	float const is = l.btn / 38.0f;                              /* icon scale relative to a 38-unit button */
	hex_button(g, bx, cy, l.btn, Rgba { 34, 36, 40, 1.0f }, Rgba { 96, 98, 104, 0.9f });
	Icon::search(g, bx, cy, 0.78f * is, Pal::white);
	hex_button(g, ax, cy, l.btn, Rgba { 24, 25, 28, 1.0f }, Rgba { 110, 108, 104, 0.9f });
	Icon::arrow_right(g, ax, cy, 0.72f * is, Pal::white);

	/* text: starts one spacing after the icon button, vertically centred on its line box */
	char const *t = st.prompt ? st.prompt : "Ask or describe what you want...";
	Rgba const tc = st.prompt ? Pal::white : Rgba { 214, 218, 224, 0.92f };
	float const tx = bx + l.btn * 0.5f + l.spacing + l.pad_x * 0.5f;
	float const line_h = line_height(g.f.reg, l.font * g.k) / g.k;
	g.text(g.f.reg, tx, cy - line_h * 0.5f, l.font, tc, t);
}

/* ------------------------------------------------------ navigation rail (right) */

static inline void draw_rail(Ctx &g, State const &st, Hits &hits)
{
	float const x = 1090, y = 12, w = 92, h = 540;
	/* silhouette traced from the mockup: a chamfered strip plus the notched bulge around the Agents
	 * tile. A Poly is convex, so the strip and the bulge are two shapes sharing one outline. */
	Poly p; p.n = 0;
	static float const pts[][2] = { { 1108, 12 }, { 1163, 12 }, { 1176, 24 }, { 1176, 522 }, { 1163, 534 }, { 1108, 534 },
	                                { 1090, 516 }, { 1090, 33 } };
	for (auto const &v : pts) p.add(g.X(v[0]), g.Y(v[1]));
	p.finish();
	Poly bu; bu.n = 0;
	bu.add(g.X(1090), g.Y(85)); bu.add(g.X(1082), g.Y(93)); bu.add(g.X(1082), g.Y(125)); bu.add(g.X(1090), g.Y(133)); bu.finish();
	glow_poly(g.c, p, g.L(8), g.L(3), Rgba { 0, 0, 0, 0.5f });
	VGrad const body { g.Y(y), g.Y(y + h), Rgba { 17, 17, 19, 0.97f }, Rgba { 9, 9, 10, 0.98f } };
	VGrad const edge { g.Y(y), g.Y(y + h), Rgba { 176, 128, 76, 0.95f }, Rgba { 96, 70, 46, 0.9f } };
	fill_poly(g.c, p, body);
	fill_poly(g.c, bu, body);
	stroke_poly(g.c, p, g.L(1.4f), edge);
	stroke_poly(g.c, bu, g.L(1.4f), edge);
	/* open the strip's own edge where the bulge joins it */
	fill_poly(g.c, [&] { Poly m; m.n = 0; m.add(g.X(1088.4f), g.Y(86.5f)); m.add(g.X(1092.4f), g.Y(86.5f)); m.add(g.X(1092.4f), g.Y(131.5f)); m.add(g.X(1088.4f), g.Y(131.5f)); m.finish(); return m; }(), body);

	/* top: compass medallion in a hexagonal frame */
	hex_tile_v(g, 1128, 54, 46, 52, false);
	draw_image(g.c, g.a.compass, g.X(1128 - 19), g.Y(52 - 19), g.L(38), g.L(38),
	           [&](float px, float py) { return sd_circle(px, py, g.X(1128), g.Y(52), g.L(19)); });

	static char const *const names[6] = { "Agents", "System", "Files", "Network", "Devices", "Settings" };
	for (int i = 0; i < 6; ++i) {
		float const cy = 115 + float(i) * 70.8f;
		bool const act = i == st.rail_active;
		hits.rail[i] = { 1128 - 26, cy - 26, 52, 64 };     /* tile + label */
		if (act) hex_tile_v(g, 1128, cy, 46, 52, true); else hex_tile_v(g, 1128, cy, 44, 50, false);
		Rgba const ic = act ? Rgba { 255, 164, 64, 1.0f } : Rgba { 236, 238, 240, 0.96f };
		switch (i) {
		case 0: Icon::agents(g, 1128, cy, 1.0f, ic); break;
		case 1: Icon::gear(g, 1128, cy, 0.95f, ic); break;
		case 2: Icon::folder(g, 1128, cy, 1.0f, ic); break;
		case 3: Icon::network(g, 1128, cy - 1, 0.95f, ic); break;
		case 4: Icon::devices(g, 1128, cy, 1.0f, ic); break;
		case 5: Icon::gear(g, 1128, cy, 0.95f, ic); break;
		}
		g.textc(g.f.med, 1128, cy + 25.5f, 11.4f, Rgba { 245, 246, 248, 0.97f }, names[i]);
	}
	/* badge on Agents: the number of apps that are really running */
	int running = 0;
	for (int i = 0; i < st.app_count; ++i) if (st.apps[i].active) ++running;
	if (running > 0) {
		char num[3] = { char('0' + (running > 9 ? 9 : running)), 0, 0 };
		g.disc(1147, 98, 8.6f, Rgba { 255, 120, 24, 1.0f });
		g.rng(1147, 98, 8.6f, 1.0f, Rgba { 255, 200, 140, 0.9f });
		g.textc(g.f.semi, 1147, 90.5f, 11.5f, Pal::white, num);
	}
}

/* -------------------------------------------------- Agents & Workers panel */

static inline void app_icon(Ctx &g, int kind, float cx, float cy, float s, Rgba col)
{
	switch (kind) {
	case 0: Icon::folder(g, cx, cy, s, col); break;
	case 1: g.rr_stroke(cx - 9 * s, cy - 7 * s, 18 * s, 14 * s, 2.5f * s, 1.6f * s, col);
	        g.ln(cx - 5 * s, cy - 2 * s, cx - 2 * s, cy + 1 * s, 1.6f * s, col);
	        g.ln(cx - 2 * s, cy + 1 * s, cx - 5 * s, cy + 4 * s, 1.6f * s, col);
	        g.ln(cx + 1 * s, cy + 4 * s, cx + 5 * s, cy + 4 * s, 1.6f * s, col); break;
	case 2: Icon::arrow_right(g, cx, cy, s, col); break;
	default: Icon::agents(g, cx, cy, s, col); break;
	}
}

static inline void agent_icon_tile(Ctx &g, float x, float y, float s, int kind, bool active)
{
	g.rr_fillg(x, y, s, s, 8, active ? Rgba { 30, 22, 16, 1.0f } : Rgba { 30, 29, 28, 1.0f });
	g.rr_stroke(x, y, s, s, 8, 1.0f, active ? Rgba { 120, 80, 40, 0.9f } : Rgba { 82, 74, 64, 0.8f });
	app_icon(g, kind, x + s / 2, y + s / 2, s / 40.0f * 1.0f, active ? Rgba { 255, 170, 80, 1.0f } : Rgba { 232, 214, 190, 0.95f });
}

/* A list card in the Boot/Storyboard look, drawn from the same theme frame values as a Cell:
 * crisp rim (white-hot core when active), tight bloom, dark bezel with a fine inner line. */
static inline void card_chrome(Ctx &g, float x, float y, float w, float h, float ch, bool active, FrameStyle const &fs)
{
	Poly cp; chamfer_poly(cp, g.X(x), g.Y(y), g.L(w), g.L(h), g.L(ch), g.L(ch), g.L(ch), g.L(ch));
	if (active) {
		Rgba hot = fs.glow; hot.a = fs.bloom_alpha * 0.7f;
		glow_poly(g.c, cp, g.L(fs.bloom_reach * 0.8f), g.L(fs.bloom_sigma * 0.8f), hot);
		fill_poly(g.c, cp, VGrad { g.Y(y), g.Y(y + h), Rgba { 29, 18, 11, 1.0f }, Rgba { 15, 11, 9, 1.0f } });
	} else
		fill_poly(g.c, cp, VGrad { g.Y(y), g.Y(y + h), Rgba { 19, 19, 21, 1.0f }, Rgba { 12, 12, 14, 1.0f } });
	float const in = 3.5f;
	Poly ip; chamfer_poly(ip, g.X(x + in), g.Y(y + in), g.L(w - 2 * in), g.L(h - 2 * in), g.L(ch - 2), g.L(ch - 2), g.L(ch - 2), g.L(ch - 2));
	stroke_poly(g.c, ip, g.L(1.0f), Solid { active ? Rgba { 255, 170, 90, 0.26f } : Rgba { 150, 155, 162, 0.14f } });
	float const t = (active ? fs.border * 0.75f : fs.border * 0.5f);
	Rgba const a = active ? fs.active_a : fs.carbon_a, b = active ? fs.active_b : fs.carbon_b;
	stroke_poly(g.c, cp, g.L(t), VGrad { g.Y(y), g.Y(y + h), a, b });
	if (active) stroke_poly(g.c, cp, g.L(t * 0.4f), Solid { fs.core });
}

static inline void more_dots(Ctx &g, float cx, float cy, Rgba col)
{ for (int i = -1; i <= 1; ++i) g.disc(cx + float(i) * 5.2f, cy, 1.5f, col); }

static inline void draw_sidebar(Ctx &g, State const &st, Hits &hits)
{
	float const x = 1180, y = 0, w = 356, h = 548;
	Poly p; chamfer_poly(p, g.X(x), g.Y(y), g.L(w + 4), g.L(h), 0, 0, 0, 0);
	fill_poly(g.c, p, VGrad { g.Y(y), g.Y(y + h), Rgba { 10, 10, 12, 0.985f }, Rgba { 8, 8, 9, 0.985f } });
	g.ln(1180.5f, 0, 1180.5f, 548, 1.6f, Rgba { 255, 140, 46, 0.75f });

	int nact = 0, nina = 0;
	for (int i = 0; i < st.app_count; ++i) (st.apps[i].active ? nact : nina)++;

	g.text(g.f.med, 1200, 12, 17.3f, Pal::white, "Agents & Workers");
	Icon::close_x(g, 1514, 22, 0.95f, Rgba { 230, 232, 235, 0.95f }, 1.7f);
	hits.pane_close = { 1498, 6, 32, 32 };
	char hdr[24];
	{
		/* "Active (n)" */
		int n = nact; int k = 0;
		char const *t = "Active ("; while (*t) hdr[k++] = *t++;
		if (n >= 10) hdr[k++] = char('0' + n / 10);
		hdr[k++] = char('0' + n % 10); hdr[k++] = ')'; hdr[k] = 0;
	}
	g.text(g.f.med, 1200, 47, 13.3f, Pal::orange, hdr);

	hits.app_play_n = st.app_count;
	int ai = 0, ii = 0;
	int const nshown = nact > 3 ? 3 : nact;
	float const ihy = nshown ? 69.0f + float(nshown) * 67.0f + 4.0f : 120.0f;   /* 'Inactive' header y */
	for (int i = 0; i < st.app_count; ++i) {
		AppEntry const &a = st.apps[i];
		if (a.active) {
			if (ai >= 3) continue;
			float const cy = 69 + float(ai) * 67, ch = 58;
			card_chrome(g, 1195, cy, 327, ch, 9, true, st.frame);
			g.disc(1204, cy + 11, 1.9f, Rgba { 255, 140, 40, 1.0f });
			g.disc(1204, cy + ch - 12, 1.9f, Rgba { 255, 140, 40, 1.0f });
			agent_icon_tile(g, 1211, cy + (ch - 40) / 2, 40, a.kind, true);
			g.text(g.f.med, 1261, cy + 12, 11.2f, Pal::white, a.name);
			g.text(g.f.reg, 1261, cy + 32, 10.6f, Rgba { 188, 192, 198, 1.0f }, a.sub);
			if (a.slot >= 0) {                    /* running app: a close button */
				g.rr_fill(1478, cy + 12, 32, 32, 8, Rgba { 36, 28, 22, 1.0f });
				g.rr_stroke(1478, cy + 12, 32, 32, 8, 1.0f, Rgba { 150, 100, 60, 0.9f });
				Icon::close_x(g, 1494, cy + 28, 0.7f, Rgba { 240, 220, 200, 1.0f }, 1.8f);
				hits.app_close[i] = { 1478, cy + 12, 32, 32 };
			} else more_dots(g, 1494, cy + ch / 2, Rgba { 236, 238, 242, 0.95f });
			++ai;
		} else {
			if (ii >= 5) continue;
			float const ry = ihy + 24.5f + float(ii) * 51.2f, rh = 48;
			card_chrome(g, 1195, ry, 327, rh, 8, false, st.frame);
			agent_icon_tile(g, 1210, ry + 6, 36, a.kind, false);
			g.text(g.f.med, 1261, ry + 7, 11.1f, Pal::white, a.name);
			g.text(g.f.reg, 1261, ry + 25.5f, 10.5f, Rgba { 178, 180, 184, 1.0f }, a.sub);
			g.rr_fill(1446, ry + 7, 33, 33, 8, Rgba { 36, 35, 36, 1.0f });
			g.rr_stroke(1446, ry + 7, 33, 33, 8, 1.0f, Rgba { 96, 88, 78, 0.9f });
			Icon::play(g, 1462, ry + 23.5f, 0.78f, Rgba { 238, 214, 190, 1.0f });
			hits.app_play[i] = { 1446, ry + 7, 33, 33 };
			more_dots(g, 1498, ry + 23.5f, Rgba { 236, 238, 242, 0.95f });
			++ii;
		}
	}
	if (nact == 0)
		g.text(g.f.reg, 1200, 100, 12.0f, Rgba { 150, 156, 164, 1.0f }, "Nothing is running.");

	char ihdr[24]; { int k = 0; char const *t = "Inactive ("; while (*t) ihdr[k++] = *t++;
	                 ihdr[k++] = char('0' + (nina > 9 ? 9 : nina)); ihdr[k++] = ')'; ihdr[k] = 0; }
	g.text(g.f.med, 1200, ihy, 13.4f, Pal::white, ihdr);
}

/* ------------------------------------------------------------- taskbar */

static inline void draw_taskbar(Ctx &g, State const &st)
{
	float const sh = st.bottom.shown();
	if (sh <= 0.002f) return;
	float const full = TASKBAR_H;
	float const y = TASKBAR_RECT.y + (1.0f - sh) * (full + 8);
	Rect const r { 0, y, DW, full };
	float const dx = DW - 1090.0f;               /* the status cluster is anchored to the right edge */
	Poly p; chamfer_poly(p, g.X(r.x), g.Y(r.y), g.L(r.w), g.L(r.h), 0, g.L(14), 0, 0);
	fill_poly(g.c, p, VGrad { g.Y(r.y), g.Y(r.y + r.h), Rgba { 14, 15, 18, 0.93f }, Rgba { 8, 9, 11, 0.95f } });
	g.ln(0, r.y + 0.5f, DW, r.y + 0.5f, 1.0f, Rgba { 90, 80, 70, 0.55f });

	/* small emblem + wordmark */
	draw_image(g.c, g.a.compass, g.X(10), g.Y(y + 4), g.L(20), g.L(20),
	           [&](float px, float py) { return sd_circle(px, py, g.X(20), g.Y(y + 14), g.L(10)); });
	g.text(g.f.med, 34, y + 6.0f, 10.9f, Pal::white, "HermOS");

	/* status glyphs: display, volume, network */
	Icon::monitor(g, 843 + dx, y + 13.5f, 0.62f, Rgba { 214, 218, 224, 1.0f });
	{
		float const vx = 876 + dx, vy = y + 13.5f;
		g.rr_fill(vx - 5, vy - 2.6f, 3.4f, 5.2f, 1.0f, Rgba { 214, 218, 224, 1.0f });
		Poly sp; sp.n = 0; sp.add(g.X(vx - 3), g.Y(vy - 2.8f)); sp.add(g.X(vx + 1.5f), g.Y(vy - 6)); sp.add(g.X(vx + 1.5f), g.Y(vy + 6)); sp.add(g.X(vx - 3), g.Y(vy + 2.8f)); sp.finish();
		fill_poly(g.c, sp, Solid { Rgba { 214, 218, 224, 1.0f } });
		g.arcs(vx + 2, vy, 4.2f, -0.8f, 0.8f, 1.1f, Rgba { 214, 218, 224, 1.0f });
		g.arcs(vx + 2, vy, 7.0f, -0.8f, 0.8f, 1.1f, Rgba { 214, 218, 224, 1.0f });
	}
	Icon::network(g, 912 + dx, y + 12.5f, 0.5f, Rgba { 214, 218, 224, 1.0f }, 1.9f);
	g.text(g.f.reg, 939 + dx, y + 6.0f, 11.4f, Pal::text, st.clock);
}

/* --------------------------------------------------------- hideable left dock */

static inline void draw_left_dock(Ctx &g, State const &st, Hits &hits)
{
	float const sh = st.left.shown();
	float const tw_ = st.left.thickness;
	hits.left_panel = { 0, 0, 0, 0 };
	if (sh <= 0.002f) return;
	float const x = -(tw_ + 6) * (1.0f - sh);
	float const y = 120, h = 40.0f + float(st.payload_ok ? 3 : 2) * 68.0f - 4.0f;
	hits.left_panel = { x, y, tw_, h };

	Poly p; chamfer_poly(p, g.X(x), g.Y(y), g.L(tw_), g.L(h), 0, g.L(16), g.L(16), 0);
	glow_poly(g.c, p, g.L(12), g.L(5), Rgba { 0, 0, 0, 0.5f });
	fill_poly(g.c, p, VGrad { g.Y(y), g.Y(y + h), Rgba { 17, 17, 19, 0.96f }, Rgba { 9, 9, 10, 0.97f } });
	stroke_poly(g.c, p, g.L(1.3f), Solid { Rgba { 110, 94, 76, 0.9f } });

	static char const *const labels[3] = { "Files", "Terminal", "Install" };
	int const count = st.payload_ok ? 3 : 2;
	hits.launcher_count = count;
	for (int i = 0; i < count; ++i) {
		float const cy = y + 40 + float(i) * 68, cx = x + tw_ / 2 - 3;
		hex_tile(g, cx, cy, 48, 46, i == 2);
		Rgba const ic { 236, 238, 240, 0.96f };
		switch (i) {
		case 0: Icon::folder(g, cx, cy, 1.0f, ic); break;
		case 1: g.rr_stroke(cx - 9, cy - 7, 18, 14, 2.5f, 1.6f, ic); g.ln(cx - 5, cy - 2, cx - 2, cy + 1, 1.6f, ic); g.ln(cx - 2, cy + 1, cx - 5, cy + 4, 1.6f, ic); g.ln(cx + 1, cy + 4, cx + 5, cy + 4, 1.6f, ic); break;
		default: Icon::arrow_right(g, cx, cy, 0.9f, Pal::white); break;
		}
		g.textc(g.f.med, cx, cy + 25.0f, 10.4f, Rgba { 245, 246, 248, 0.97f }, labels[i]);
		hits.launcher[i] = { cx - 25, cy - 27, 50, 54 };
	}
}

/* ---------------------------------------------------------- window chrome */

struct WinGeom
{
	float x, y, w, h;           /* canvas px */
	float s;                    /* local scale (px per reference px) */
};

static inline Poly window_shape(float x, float y, float w, float h)
{
	float const c = minf(w, h) * 0.135f;
	Poly p; chamfer_poly(p, x, y, w, h, c, c, c, c);
	return p;
}

static inline void window_frame(Ctx &g, WinGeom const &wg, bool active, bool hosted, FrameStyle const &fs)
{
	Poly p = window_shape(wg.x, wg.y, wg.w, wg.h);
	float const k = wg.s;
	if (active) {
		/* wide soft bloom + tight hot halo (Boot / Storyboard look) */
		glow_poly(g.c, p, fs.glow_reach * k, fs.glow_sigma * k, fs.glow);
		Rgba hot = fs.glow; hot.a = fs.bloom_alpha;
		glow_poly(g.c, p, fs.bloom_reach * k, fs.bloom_sigma * k, hot);
	} else
		glow_poly(g.c, p, fs.shadow_reach * k, fs.shadow_sigma * k, fs.shadow);
	/* a Cell that hosts an app is fully opaque, so a Cell behind it can never bleed through */
	float const a0 = hosted ? 1.0f : 0.95f, a1 = hosted ? 1.0f : 0.97f;
	fill_poly(g.c, p, VGrad { wg.y, wg.y + wg.h, Rgba { 17, 18, 21, a0 }, Rgba { 8, 9, 11, a1 } });
	/* dark carbon bezel band inside the rim, closed by a fine inner line */
	float const bz = fs.bezel * k;
	if (bz > 1.0f) {
		Poly q = window_shape(wg.x + bz * 0.5f, wg.y + bz * 0.5f, wg.w - bz, wg.h - bz);
		stroke_poly(g.c, q, bz, Solid { Rgba { 4, 5, 6, fs.bezel_alpha } });
		Poly r = window_shape(wg.x + bz, wg.y + bz, wg.w - 2 * bz, wg.h - 2 * bz);
		stroke_poly(g.c, r, 1.0f * k, Solid { active ? Rgba { 255, 170, 90, 0.22f } : Rgba { 150, 155, 162, 0.12f } });
	}
	/* rim: bright orange, with a white-hot core line when active */
	Rgba const a = active ? fs.active_a : fs.carbon_a, b = active ? fs.active_b : fs.carbon_b;
	stroke_poly(g.c, p, fs.border * k, DGrad { wg.x, wg.y, wg.x + wg.w, wg.y + wg.h, a, b });
	if (active) stroke_poly(g.c, p, fs.border * k * 0.45f, Solid { fs.core });
}

/* local-coordinate helper for window content; (lx,ly) in reference px */
struct Loc
{
	Ctx &g; WinGeom wg; float ox_, oy_;     /* design offset so that crop coords map correctly */
	float s_;
	/* map "crop" coordinates (1.9x zoom of the reference sheet) into canvas px */
	float X(float cx) const { return wg.x + (cx / 1.9f - ox_) * wg.s; }
	float Y(float cy) const { return wg.y + (cy / 1.9f - oy_) * wg.s; }
	float S(float v)  const { return v / 1.9f * wg.s; }

	float text(Font &f, float cx, float cy, float size, Rgba col, char const *t, float tr = 0, float bold = 0)
	{ return draw_text(g.c, f, X(cx), Y(cy), S(size), col, t, tr * wg.s, bold) ; }
	float textc(Font &f, float cx, float cy, float size, Rgba col, char const *t, float tr = 0)
	{ return draw_text_center(g.c, f, X(cx), Y(cy), S(size), col, t, tr * wg.s); }
	float textr(Font &f, float cx, float cy, float size, Rgba col, char const *t)
	{ return draw_text_right(g.c, f, X(cx), Y(cy), S(size), col, t); }
	float tw(Font &f, float size, char const *t) { return text_width(f, S(size), t); }

	void rr(float cx, float cy, float w, float h, float r, Rgba fill, Rgba stroke, float st = 1.0f)
	{
		if (fill.a > 0)   fill_rrect(g.c, X(cx), Y(cy), S(w), S(h), S(r), Solid { fill });
		if (stroke.a > 0) stroke_rrect(g.c, X(cx), Y(cy), S(w), S(h), S(r), st * wg.s, Solid { stroke });
	}
	template <class COL> void rrg(float cx, float cy, float w, float h, float r, COL col)
	{ fill_rrect(g.c, X(cx), Y(cy), S(w), S(h), S(r), col); }
	void ln(float x0, float y0, float x1, float y1, float t, Rgba col) { line(g.c, X(x0), Y(y0), X(x1), Y(y1), t * wg.s, col); }
	void disc(float cx, float cy, float r, Rgba col) { fill_circle(g.c, X(cx), Y(cy), S(r), col); }
	void rng(float cx, float cy, float r, float t, Rgba col) { ring(g.c, X(cx), Y(cy), S(r), t * wg.s, col); }

	/* a Ctx whose design space equals this window's pixel space, for the shared icons */
	Ctx sub() const { return Ctx { g.c, g.f, g.a, 1.0f, 0, 0 }; }
	float scale() const { return wg.s; }
};

/* generic pieces ----------------------------------------------------------- */

static inline void win_controls(Loc &l, float x_undo, float x_min, float x_max, float x_close, float cy, bool have_min = true)
{
	Ctx s = l.sub(); float const k = l.wg.s * 0.9f;
	Rgba const dim { 212, 214, 218, 0.92f };
	if (x_undo > 0) Icon::undo(s, l.X(x_undo), l.Y(cy), k, dim);
	if (have_min && x_min > 0) Icon::minus(s, l.X(x_min), l.Y(cy), k, dim);
	if (x_max > 0) Icon::maximize(s, l.X(x_max), l.Y(cy), k, dim);
	Icon::close_x(s, l.X(x_close), l.Y(cy), k * 1.1f, Rgba { 255, 160, 48, 1.0f }, 2.2f);
}

static inline void nav_item(Loc &l, Font &fm, Font &fr, float icx, float tcx, float cy, float size, char const *name, bool active, int kind,
                            float pill_x0 = 0, float pill_x1 = 0, float pill_h = 40)
{
	Ctx s = l.sub(); float const k = l.wg.s;
	if (active) {
		float const w = pill_x1 - pill_x0;
		fill_rrect(l.g.c, l.X(pill_x0), l.Y(cy - pill_h / 2), l.S(w), l.S(pill_h), l.S(8),
		           HGrad { l.X(pill_x0), l.X(pill_x1), Rgba { 232, 112, 30, 1.0f }, Rgba { 120, 62, 24, 1.0f } });
		stroke_rrect(l.g.c, l.X(pill_x0), l.Y(cy - pill_h / 2), l.S(w), l.S(pill_h), l.S(8), 1.0f * k, Solid { Rgba { 255, 170, 96, 0.9f } });
	}
	Rgba const ic = active ? Rgba { 255, 244, 232, 1.0f } : Rgba { 232, 234, 238, 0.92f };
	float const sz = 0.62f * k;
	float const x = l.X(icx), y = l.Y(cy);
	switch (kind) {
	case 0: Icon::small_square(s, x, y, sz * 1.0f, ic, true); break;
	case 1: Icon::monitor(s, x, y, sz * 0.7f, ic, 1.8f); break;
	case 2: Icon::folder(s, x, y, sz * 0.7f, ic, 1.8f); break;
	case 3: Icon::network(s, x, y, sz * 0.6f, ic, 1.9f); break;
	case 4: Icon::user(s, x, y, sz * 0.7f, ic, 1.9f); break;
	case 5: Icon::gear(s, x, y, sz * 0.62f, ic, 1.9f); break;
	case 6: Icon::lock(s, x, y, sz * 0.7f, ic, 1.8f); break;
	default: Icon::small_square(s, x, y, sz * 1.0f, ic, false); break;
	}
	l.text(active ? fm : fr, tcx, cy - size * 0.62f, size, active ? Pal::white : Rgba { 226, 228, 232, 0.96f }, name);
}

} /* namespace Scene */

#include "scene_windows.h"

namespace Scene {

/* ------------------------------------------------------- full desktop */

static inline void draw_wallpaper(Ctx &g)
{
	Canvas &c = g.c;
	Image const &w = g.a.wall;
	if (!w.rgba) { for (int i = 0; i < c.w * c.h; ++i) c.px[i] = 0x10161c; return; }
	/* cover-fit with the design-space centre preserved */
	for (int y = 0; y < c.h; ++y) {
		float const v = (float(y) - g.oy) / g.k * float(w.h) / DH;
		for (int x = 0; x < c.w; ++x) {
			float const u = (float(x) - g.ox) / g.k * float(w.w) / DW;
			Rgba const s = sample(w, u, v);
			c.px[y * c.w + x] = (uint32_t(s.r) << 16) | (uint32_t(s.g) << 8) | uint32_t(s.b);
		}
	}
}

static inline Ctx make_ctx(Canvas &c, Fonts &f, Assets &a)
{
	Ctx g { c, f, a, minf(float(c.w) / DW, float(c.h) / DH), 0, 0 };
	g.ox = (float(c.w) - DW * g.k) * 0.5f; g.oy = (float(c.h) - DH * g.k) * 0.5f;
	return g;
}

/* index into the z-order of the first open Cell that hosts an app (6 = none) */
static inline int top_split(State const &st)
{
	for (int oi = 0; oi < 6; ++oi) {
		int const i = st.order[oi];
		if (i >= 0 && i < 6 && st.win_open[i] && st.win_hosted[i]) return oi;
	}
	return 6;
}

/* Base layer: everything that does not animate with the hideable panels.
 * desktop_builder caches this frame and only redraws it when the windows,
 * the search text or the clock change. */
static inline void render_base(Canvas &c, Fonts &f, Assets &a, State &st, Hits &hits)
{
	c.ink = &st.ink; c.inactive = false;
	Ctx g = make_ctx(c, f, a);
	draw_wallpaper(g);
	draw_header(g);
	draw_greeting_search(g, st);

	/* the Cells below the first hosted one, in z-order (static content only) */
	int const split = top_split(st);
	for (int oi = 0; oi < split; ++oi) {
		int const i = st.order[oi];
		if (i < 0 || i > 5 || !st.win_open[i]) continue;
		draw_window(g, i, st.win[i], i == st.active_win, st.win_hosted[i], st);
	}
}

/* Top pass: from the first hosted Cell upwards every Cell is drawn frame first, then its
 * app content (blit callback), bottom to top, so a Cell in front fully hides the ones behind.
 * Redrawn every frame because hosted apps change continuously. */
static inline void render_top(Canvas &c, Fonts &f, Assets &a, State &st, void (*blit)(void *, int), void *ctx)
{
	c.ink = &st.ink; c.inactive = false;
	Ctx g = make_ctx(c, f, a);
	for (int oi = top_split(st); oi < 6; ++oi) {
		int const i = st.order[oi];
		if (i < 0 || i > 5 || !st.win_open[i]) continue;
		draw_window(g, i, st.win[i], i == st.active_win, st.win_hosted[i], st);
		if (st.win_hosted[i] && blit) blit(ctx, i);
	}
}

/* Panel layer: the two hideable panels (left launcher dock, bottom bar) and
 * their reveal grips. Cheap enough to redraw every animation frame. */
static inline void render_panels(Canvas &c, Fonts &f, Assets &a, State &st, Hits &hits)
{
	c.ink = &st.ink; c.inactive = false;
	Ctx g = make_ctx(c, f, a);
	/* the right group: the navigation rail and the Agents & Workers pane slide out to the edge
	 * together; the rail stays at the edge so the pane can be brought back with its Agents tile */
	{
		float const sh = st.right.shown();
		float const pane_w = st.right.thickness;
		Rect const none { 0, 0, 0, 0 };
		hits.pane_close = none; hits.pane_reveal = none;
		float const slim = st.right_slim;

		/* the slim bar: always there, under the pane; clicking it brings the pane back */
		{
			float const bx = DW - slim, by = 0, bh = 548;
			Poly bp; chamfer_poly(bp, g.X(bx), g.Y(by), g.L(slim + 4), g.L(bh), 0, 0, 0, 0);
			fill_poly(g.c, bp, VGrad { g.Y(by), g.Y(by + bh), Rgba { 10, 10, 12, 0.985f }, Rgba { 8, 8, 9, 0.985f } });
			g.ln(bx + 0.5f, by, bx + 0.5f, by + bh, 1.2f, Rgba { 70, 62, 54, 0.8f });
			/* chevron pointing into the screen, and a thin pill, centred on the bar */
			float const cx = bx + slim * 0.5f, cy = bh * 0.5f;
			g.ln(cx + 3.5f, cy - 7, cx - 3.5f, cy, 2.0f, Rgba { 255, 170, 80, 0.95f });
			g.ln(cx - 3.5f, cy, cx + 3.5f, cy + 7, 2.0f, Rgba { 255, 170, 80, 0.95f });
			g.rr_fill(cx - 1.5f, cy - 34, 3, 16, 1.5f, Rgba { 255, 170, 80, 0.45f });
			g.rr_fill(cx - 1.5f, cy + 18, 3, 16, 1.5f, Rgba { 255, 170, 80, 0.45f });
			if (sh < 0.5f) hits.pane_reveal = { bx, by, slim, bh };
		}
		for (int i = 0; i < MAX_APP_ENTRIES; ++i) { hits.app_close[i] = none; hits.app_play[i] = none; }
		hits.app_play_n = 0;

		float const rdx = (1.0f - sh) * (pane_w - slim);        /* rail: ends docked beside the slim bar */
		Ctx gr = g; gr.ox += g.L(rdx);
		draw_rail(gr, st, hits);
		for (int i = 0; i < 6; ++i) hits.rail[i].x += rdx;

		if (sh > 0.002f) {
			float const pdx = (1.0f - sh) * (pane_w + 8.0f);
			Ctx gp = g; gp.ox += g.L(pdx);
			draw_sidebar(gp, st, hits);
			hits.pane_close.x += pdx;
			for (int i = 0; i < MAX_APP_ENTRIES; ++i) { hits.app_close[i].x += pdx; hits.app_play[i].x += pdx; }
		}
	}
	draw_taskbar(g, st);
	draw_left_dock(g, st, hits);

	hits.left_grip   = { 0, 120, 6, 220 };
	hits.bottom_grip = { DW * 0.5f - 165, DH - 20, 330, 20 };
	if (!st.bottom.visible()) g.rr_fill(DW * 0.5f - 25, DH - 12, 50, 4, 2, Rgba { 255, 170, 80, 0.55f });
	if (!st.left.visible())   g.rr_fill(3, 245, 4, 80, 2, Rgba { 255, 170, 80, 0.55f });
}

/* whole frame in one call (host preview) */
static inline void render(Canvas &c, Fonts &f, Assets &a, State &st, Hits &hits)
{
	render_base(c, f, a, st, hits);
	render_panels(c, f, a, st, hits);
}

} /* namespace Scene */

#endif /* _HERMOS_SCENE_H_ */
