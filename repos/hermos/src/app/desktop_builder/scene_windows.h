/*
 * HermOS scene — the six built-in windows (Welcome, System, Files,
 * Hydroponics Agent, Create a New Worker, Software & Updates).
 *
 * Content coordinates are authored on the 1.9x reference sheet and mapped
 * into whatever rectangle the window currently has (move/resize safe).
 */

#ifndef _HERMOS_SCENE_WINDOWS_H_
#define _HERMOS_SCENE_WINDOWS_H_

namespace Scene {

struct WinRef { float w, h, ox, oy; };
static constexpr WinRef WIN_REF[6] = {
	{ 505, 245,  8,  7 },   /* Welcome  */
	{ 450, 241, 33, 12 },   /* System   */
	{ 450, 241, 47,  6 },   /* Files    */
	{ 565, 206,  6,  9 },   /* Hydro    */
	{ 372, 204, 24,  9 },   /* Worker   */
	{ 511, 207, 21,  8 },   /* Updates  */
};

/* the sunrise landscape part of the wallpaper (the lower band is the water reflection) */
static inline Image scenery(Image const &w)
{
	return Image { w.rgba, w.w * 1078 / 1536, w.h * 548 / 1024, w.w };
}

static inline void field(Loc &l, float x0, float y0, float x1, float y1, char const *txt, bool dropdown)
{
	l.rr(x0, y0, x1 - x0, y1 - y0, 9, Rgba { 21, 22, 25, 1.0f }, Rgba { 52, 52, 56, 0.95f }, 1.0f);
	l.text(l.g.f.reg, x0 + 13, y0 + (y1 - y0) * 0.5f - 9.5f, 16.5f, Rgba { 236, 238, 241, 0.97f }, txt);
	if (dropdown) { Ctx s = l.sub(); Icon::chevron_down(s, l.X(x1 - 22), l.Y((y0 + y1) * 0.5f), l.scale() * 1.0f, Rgba { 220, 222, 226, 0.95f }); }
}

static inline void button(Loc &l, float x0, float y0, float x1, float y1, char const *txt, bool primary, int icon_kind = -1)
{
	Fonts &f = l.g.f;
	if (primary) {
		fill_rrect(l.g.c, l.X(x0), l.Y(y0), l.S(x1 - x0), l.S(y1 - y0), l.S(10),
		           VGrad { l.Y(y0), l.Y(y1), Rgba { 236, 124, 34, 1.0f }, Rgba { 200, 86, 18, 1.0f } });
		stroke_rrect(l.g.c, l.X(x0), l.Y(y0), l.S(x1 - x0), l.S(y1 - y0), l.S(10), 1.2f * l.wg.s, Solid { Rgba { 255, 178, 106, 0.9f } });
	} else {
		l.rr(x0, y0, x1 - x0, y1 - y0, 10, Rgba { 28, 29, 32, 1.0f }, Rgba { 70, 70, 76, 0.95f }, 1.0f);
	}
	float const cy = (y0 + y1) * 0.5f;
	float const size = 16.0f;
	float const tw_ = l.tw(f.med, size, txt) / l.wg.s * 1.9f;   /* back to crop units */
	Ctx s = l.sub(); float const k = l.wg.s;
	if (icon_kind >= 0) {
		float const total = tw_ + 30;
		float const ix = (x0 + x1) * 0.5f - total * 0.5f + 8;
		Rgba const ic = Pal::white;
		if (icon_kind == 0) { l.rr(ix - 6, cy - 8, 4.2f * 1.9f, 16, 1.0f, ic, Rgba { 0, 0, 0, 0 }); l.rr(ix + 3, cy - 8, 4.2f * 1.9f, 16, 1.0f, ic, Rgba { 0, 0, 0, 0 }); }
		if (icon_kind == 1) Icon::play(s, l.X(ix), l.Y(cy), k * 0.9f, ic);
		if (icon_kind == 2) Icon::gear(s, l.X(ix), l.Y(cy), k * 0.62f, ic, 1.9f);
		l.text(f.med, ix + 18, cy - 10, size, Pal::white, txt);
	} else {
		l.textc(f.med, (x0 + x1) * 0.5f, cy - 10, size, Pal::white, txt);
	}
}

/* ------------------------------------------------------------------ 0: Welcome */

static inline void win_welcome(Loc &l)
{
	Fonts &f = l.g.f; Canvas &c = l.g.c;

	/* left: scenic image panel with the emblem + wordmark */
	{
		Poly p; chamfer_poly(p, l.X(26), l.Y(26), l.S(452), l.S(442), l.S(60), 0, 0, l.S(60));
		draw_image(c, scenery(l.g.a.wall), l.X(26), l.Y(26), l.S(452), l.S(442),
		           [&](float px, float py) { return p.sd(px, py); });
		Poly pl = p;
		stroke_poly(c, pl, 1.2f * l.wg.s, Solid { Rgba { 255, 190, 120, 0.35f } });
		/* darken the bottom for the wordmark */
		fill_poly(c, pl, VGrad { l.Y(300), l.Y(468), Rgba { 0, 0, 0, 0.0f }, Rgba { 8, 6, 10, 0.55f } });
		/* the big emblem */
		float const cx = l.X(250), cy = l.Y(168), r = l.S(83);
		glow_sd(c, cx - r, cy - r, cx + r, cy + r, l.S(26), l.S(10), Rgba { 255, 130, 30, 0.35f },
		        [&](float px, float py) { return sd_circle(px, py, cx, cy, r - l.S(4)); });
		draw_image(c, l.g.a.compass, cx - r, cy - r, 2 * r, 2 * r,
		           [&](float px, float py) { return sd_circle(px, py, cx, cy, r); });
		/* wordmark */
		float const ws = 45.0f;
		float const hw_ = l.tw(f.reg, ws, "H");
		float const x0 = 150, y0 = 283;
		l.text(f.reg, x0, y0, ws, Pal::white, "H");
		l.ln(x0 + 3, y0 + 40, x0 + 14, y0 + 17, 4.4f, Pal::orange);
		l.ln(x0 + 14, y0 + 17, x0 + 27, y0 + 38, 4.4f, Pal::orange);
		l.ln(x0 + 27, y0 + 38, x0 + hw_ / l.wg.s * 1.9f - 3, y0 + 14, 4.4f, Pal::orange);
		float const ax = x0 + hw_ / l.wg.s * 1.9f + 7;
		float const w1 = l.text(f.reg, ax, y0, ws, Pal::white, "erm", 1.5f);
		l.text(f.reg, ax + w1 / l.wg.s * 1.9f + 6, y0, ws, Pal::white, "OS", 1.5f);
		l.textc(f.med, 250, 332, 16.5f, Rgba { 236, 238, 241, 0.97f }, "A MORE HUMAN COMPUTER", 2.7f);
	}

	/* right: wizard card */
	{
		Poly p; chamfer_poly(p, l.X(452), l.Y(58), l.S(516), l.S(388), l.S(52), l.S(34), l.S(52), l.S(34));
		fill_poly(c, p, VGrad { l.Y(58), l.Y(446), Rgba { 24, 26, 29, 0.93f }, Rgba { 13, 14, 16, 0.95f } });
		stroke_poly(c, p, 1.3f * l.wg.s, Solid { Rgba { 150, 128, 104, 0.55f } });
		l.text(f.med, 535, 98, 27, Pal::white, "Welcome to HermOS");
		l.text(f.reg, 535, 139, 18, Rgba { 214, 218, 224, 0.96f }, "A More Human Computer");
		l.ln(535, 186, 955, 186, 1.0f, Rgba { 120, 116, 110, 0.55f });
		static char const *const steps[5] = { "Detect your computer", "Choose installation target", "Review installation plan", "Install HermOS", "Complete" };
		static float const ys[5] = { 216, 258, 298, 340, 381 };
		l.ln(547, 228, 547, 372, 1.0f, Rgba { 110, 104, 98, 0.6f });
		Ctx s = l.sub();
		for (int i = 0; i < 5; ++i) {
			bool const act = i == 0;
			l.rr(535, ys[i] - 12, 24, 24, 7, act ? Rgba { 60, 38, 22, 1.0f } : Rgba { 42, 42, 44, 1.0f },
			     act ? Rgba { 255, 150, 60, 1.0f } : Rgba { 98, 96, 94, 0.9f }, 1.2f);
			Icon::small_square(s, l.X(547), l.Y(ys[i]), l.wg.s * 0.42f, act ? Pal::peach : Rgba { 200, 200, 204, 0.8f }, i % 2 == 0);
			l.text(act ? f.med : f.reg, 580, ys[i] - 10, 16.5f, act ? Pal::peach : Rgba { 232, 234, 238, 0.96f }, steps[i]);
		}
		/* Continue */
		Poly b; b.n = 0;
		float const bx0 = 673, bx1 = 878, by0 = 405, by1 = 447, tip = 16;
		b.add(l.X(bx0), l.Y((by0 + by1) / 2)); b.add(l.X(bx0 + tip), l.Y(by0)); b.add(l.X(bx1 - tip), l.Y(by0));
		b.add(l.X(bx1), l.Y((by0 + by1) / 2)); b.add(l.X(bx1 - tip), l.Y(by1)); b.add(l.X(bx0 + tip), l.Y(by1)); b.finish();
		glow_poly(c, b, l.S(10), l.S(4), Rgba { 255, 120, 24, 0.4f });
		fill_poly(c, b, VGrad { l.Y(by0), l.Y(by1), Rgba { 26, 22, 20, 1.0f }, Rgba { 12, 11, 12, 1.0f } });
		stroke_poly(c, b, 1.8f * l.wg.s, VGrad { l.Y(by0), l.Y(by1), Rgba { 255, 190, 112, 1.0f }, Rgba { 232, 102, 24, 1.0f } });
		float const tw_ = l.tw(f.med, 16.5f, "Continue") / l.wg.s * 1.9f;
		float const tx = (bx0 + bx1) / 2 - (tw_ + 28) / 2;
		l.text(f.med, tx, 416, 16.5f, Pal::white, "Continue");
		Icon::arrow_right(s, l.X(tx + tw_ + 20), l.Y(426), l.wg.s * 0.62f, Pal::white, 1.9f);
	}
	win_controls(l, 780, 810, 843, 886, 41);
}

/* ------------------------------------------------------------------ 1: System */

static inline void win_system(Loc &l)
{
	Fonts &f = l.g.f; Canvas &c = l.g.c;
	l.text(f.med, 88, 62, 21, Pal::white, "System");
	static char const *const items[10] = { "Overview", "Hardware", "Storage", "Network", "Displays", "Audio", "Users", "Updates", "Security", "About" };
	static float const ys[10] = { 125, 163, 199, 235, 271, 307, 342, 378, 411, 446 };
	for (int i = 0; i < 10; ++i) nav_item(l, f.med, f.reg, 100, 124, ys[i], 14.5f, items[i], i == 0, i % 7, 78, 283, 40);
	l.ln(295, 88, 295, 468, 1.0f, Rgba { 92, 78, 62, 0.7f });

	l.text(f.med, 333, 80, 20, Pal::white, "System Overview");
	/* preview thumbnail (the live wallpaper) */
	draw_image(c, scenery(l.g.a.wall), l.X(335), l.Y(130), l.S(228), l.S(148),
	           [&](float px, float py) { return sd_rrect(px, py, l.X(449), l.Y(204), l.S(114), l.S(74), l.S(13)); });
	stroke_rrect(c, l.X(335), l.Y(130), l.S(228), l.S(148), l.S(13), 1.2f * l.wg.s, Solid { Rgba { 220, 190, 160, 0.5f } });

	l.text(f.med, 565, 122, 22, Pal::white, "HermOS");
	l.text(f.reg, 565, 160, 17, Rgba { 196, 200, 206, 1.0f }, "A More Human Computer");
	static char const *const k_[7] = { "Version", "Architecture", "Uptime", "Memory", "CPU", "Graphics", "Storage" };
	static char const *const v_[7] = { "0.1 (Prototype)", "x86_64 (NOVA)", "2 hours 14 minutes", "6.1 GB / 16 GB", "8 cores (QEMU)", "Virtio-GPU", "20 GB (QEMU Virtual Disk)" };
	static float const ry[7] = { 215, 242, 270, 297, 325, 352, 379 };
	for (int i = 0; i < 7; ++i) {
		bool const hi = (i == 3 || i == 5);
		l.text(f.reg, 565, ry[i] - 10, 14, hi ? Pal::peach : Rgba { 232, 234, 238, 0.97f }, k_[i]);
		l.text(f.reg, 705, ry[i] - 10, 14, Rgba { 232, 234, 238, 0.97f }, v_[i]);
		if (i < 6) l.ln(565, ry[i] + 13.5f, 895, ry[i] + 13.5f, 1.0f, Rgba { 92, 88, 84, 0.4f });
	}
	win_controls(l, 758, 0, 810, 853, 43, false);
}

/* ------------------------------------------------------------------ 2: Files */

static inline void folder_glyph(Loc &l, float cx, float cy, float s, bool pictures)
{
	Canvas &c = l.g.c;
	float const w = 56 * s, h = 44 * s;
	float const x = l.X(cx) - w / 2 * l.wg.s / 1.9f * 1.9f / 1.9f, y = l.Y(cy) - h / 2;
	(void)x; (void)y;
	float const ww = l.S(56 * s), hh = l.S(44 * s);
	float const X0 = l.X(cx) - ww / 2, Y0 = l.Y(cy) - hh / 2;
	/* back + tab */
	Poly back; chamfer_poly(back, X0, Y0, ww * 0.42f, hh * 0.3f, hh * 0.12f, hh * 0.12f, 0, 0);
	fill_poly(c, back, Solid { Rgba { 150, 144, 140, 1.0f } });
	fill_rrect(c, X0, Y0 + hh * 0.14f, ww, hh * 0.86f, hh * 0.1f, Solid { Rgba { 128, 122, 118, 1.0f } });
	/* front with the warm gradient */
	fill_rrect(c, X0, Y0 + hh * 0.34f, ww, hh * 0.66f, hh * 0.1f,
	           DGrad { X0, Y0 + hh * 0.34f, X0 + ww, Y0 + hh, Rgba { 198, 196, 196, 1.0f }, Rgba { 238, 156, 82, 1.0f } });
	stroke_rrect(c, X0, Y0 + hh * 0.34f, ww, hh * 0.66f, hh * 0.1f, 1.0f, Solid { Rgba { 255, 230, 200, 0.5f } });
	if (pictures) {
		Ctx s_ = l.sub();
		fill_rrect(c, X0 + ww * 0.1f, Y0 + hh * 0.18f, ww * 0.8f, hh * 0.72f, hh * 0.06f, Solid { Rgba { 60, 60, 64, 1.0f } });
		fill_circle(c, X0 + ww * 0.5f, Y0 + hh * 0.46f, hh * 0.13f, Rgba { 230, 230, 232, 1.0f });
		fill_rrect(c, X0 + ww * 0.3f, Y0 + hh * 0.6f, ww * 0.4f, hh * 0.25f, hh * 0.1f, Solid { Rgba { 230, 230, 232, 1.0f } });
	}
}

static inline void win_files(Loc &l)
{
	Fonts &f = l.g.f; Canvas &c = l.g.c;
	l.text(f.med, 130, 60, 21, Pal::white, "Files");
	static char const *const items[8] = { "Documents", "Pictures", "Videos", "Music", "Downloads", "Agents", "Shared", "Trash" };
	static float const ys[8] = { 161, 196, 230, 264, 297, 327, 352, 381 };
	nav_item(l, f.med, f.reg, 143, 166, 122, 14.5f, "Home", true, 6, 122, 320, 40);
	for (int i = 0; i < 8; ++i) nav_item(l, f.med, f.reg, 143, 166, ys[i], 13.5f, items[i], false, 7);
	l.ln(335, 140, 335, 468, 1.0f, Rgba { 92, 78, 62, 0.7f });

	Ctx s = l.sub();
	Icon::star4(s, l.X(365), l.Y(97), l.wg.s * 0.5f, Pal::white);
	l.text(f.med, 383, 87, 15.5f, Pal::white, "Home");
	/* top-right tool buttons */
	static float const bx[3] = { 752, 800, 856 };
	for (int i = 0; i < 3; ++i) {
		l.rr(bx[i], 80, i == 0 ? 40 : 42, 32, 9, Rgba { 24, 25, 28, 1.0f }, Rgba { 62, 62, 66, 0.95f }, 1.0f);
		float const cx = bx[i] + (i == 0 ? 20 : 21);
		if (i == 0) Icon::search(s, l.X(cx), l.Y(96), l.wg.s * 0.42f, Pal::white, 1.9f);
		else        { l.ln(cx - 7, 90, cx + 7, 90, 1.4f, Pal::white); l.ln(cx - 7, 96, cx + 7, 96, 1.4f, Pal::white); l.ln(cx - 7, 102, cx + 7, 102, 1.4f, Pal::white); }
	}
	l.ln(355, 121, 905, 121, 1.0f, Rgba { 92, 88, 84, 0.45f });

	static char const *const nm[6] = { "Documents", "Pictures", "Videos", "Music", "Agents", "Projects" };
	static float const cx0[3] = { 363, 545, 726 };
	static float const cw[3]  = { 164, 163, 164 };
	for (int r = 0; r < 2; ++r)
		for (int q = 0; q < 3; ++q) {
			int const i = r * 3 + q;
			float const y0 = r == 0 ? 140 : 282, h = 127;
			l.rr(cx0[q], y0, cw[q], h, 10, Rgba { 22, 23, 26, 1.0f }, Rgba { 56, 56, 60, 0.95f }, 1.0f);
			folder_glyph(l, cx0[q] + cw[q] / 2, y0 + 50, 1.0f, i == 1);
			l.textc(f.med, cx0[q] + cw[q] / 2, y0 + 96, 15.5f, Pal::white, nm[i]);
		}
	/* scrollbar */
	l.rr(918, 150, 4, 168, 2, Rgba { 70, 70, 74, 1.0f }, Rgba { 0, 0, 0, 0 });
	l.rr(918, 160, 4, 90, 2, Rgba { 150, 150, 156, 1.0f }, Rgba { 0, 0, 0, 0 });
	win_controls(l, 780, 810, 842, 885, 38);
	(void)c;
}

/* ------------------------------------------------------------ 3: Hydroponics */

static inline void win_hydro(Loc &l)
{
	Fonts &f = l.g.f; Canvas &c = l.g.c; Ctx s = l.sub();
	/* header */
	l.rr(110, 40, 80, 66, 14, Rgba { 22, 44, 30, 1.0f }, Rgba { 56, 70, 58, 0.9f }, 1.2f);
	Ctx sc = l.sub(); Icon::leaf(sc, l.X(150), l.Y(73), l.wg.s * 0.9f, Rgba { 70, 206, 100, 1.0f });
	l.text(f.med, 200, 40, 21.5f, Pal::white, "Hydroponics Agent");
	l.text(f.reg, 200, 76, 16.5f, Rgba { 214, 218, 224, 0.97f }, "Capturing image in 2 minutes...");
	l.disc(489, 62, 6, Rgba { 70, 214, 100, 1.0f }); l.disc(489, 62, 3, Rgba { 200, 255, 210, 1.0f });
	l.text(f.med, 506, 51, 15.5f, Pal::green, "Active");
	l.text(f.reg, 482, 79, 11.5f, Rgba { 140, 146, 154, 0.95f }, "Next capture 07:00");

	/* left nav */
	static char const *const items[6] = { "Overview", "Live View", "Schedule", "Data", "Permissions", "Logs" };
	static float const ys[6] = { 149, 191, 234, 277, 320, 363 };
	for (int i = 0; i < 6; ++i) nav_item(l, f.med, f.reg, 70, 93, ys[i], 15, items[i], i == 0, i % 7, 48, 228, 44);

	/* photo */
	draw_image(c, l.g.a.hydro_big, l.X(263), l.Y(135), l.S(359), l.S(265),
	           [&](float px, float py) { return sd_rrect(px, py, l.X(442.5f), l.Y(267.5f), l.S(179.5f), l.S(132.5f), l.S(15)); });
	stroke_rrect(c, l.X(263), l.Y(135), l.S(359), l.S(265), l.S(15), 1.4f * l.wg.s, Solid { Rgba { 210, 214, 220, 0.55f } });

	/* current task card */
	l.rr(642, 118, 418, 280, 14, Rgba { 19, 20, 23, 0.96f }, Rgba { 62, 62, 66, 0.9f }, 1.0f);
	l.text(f.med, 663, 133, 18, Pal::white, "Current Task");
	static char const *const k_[5] = { "Action", "Next Run", "Schedule", "Output", "Last Run" };
	static float const ry[5] = { 175, 206, 237, 273, 315 };
	for (int i = 0; i < 5; ++i) {
		l.text(f.reg, 663, ry[i] - 10, 15, Rgba { 224, 226, 230, 0.97f }, k_[i]);
		if (i < 4) l.ln(663, ry[i] + 14, 1040, ry[i] + 14, 1.0f, Rgba { 90, 88, 86, 0.4f });
	}
	l.text(f.reg, 783, 165, 15, Rgba { 236, 238, 241, 1.0f }, "Capture image");
	l.text(f.reg, 783, 196, 15, Rgba { 236, 238, 241, 1.0f }, "07:00 (in 2 minutes)");
	l.text(f.reg, 783, 227, 15, Rgba { 236, 238, 241, 1.0f }, "07:00 and 19:00 daily");
	Icon::arrow_right(s, l.X(792), l.Y(273), l.wg.s * 0.42f, Rgba { 150, 214, 236, 1.0f }, 1.9f);
	l.text(f.reg, 810, 263, 15, Rgba { 150, 214, 236, 1.0f }, "Hydroponics Agent");
	l.text(f.reg, 783, 305, 15, Rgba { 236, 238, 241, 1.0f }, "10 Dec 2024 19:00");
	l.rr(945, 303, 92, 26, 13, Rgba { 22, 36, 26, 1.0f }, Rgba { 40, 90, 54, 0.9f }, 1.0f);
	Icon::check(s, l.X(962), l.Y(316), l.wg.s * 0.4f, Pal::green, 2.4f);
	l.text(f.med, 978, 306, 14.5f, Pal::green, "Success");

	button(l, 663, 353, 780, 392, "Pause", false, 0);
	button(l, 796, 353, 913, 392, "Run Now", false, 1);
	button(l, 929, 353, 1046, 392, "Settings", false, 2);
	win_controls(l, 0, 950, 1000, 1045, 42);
}

/* ---------------------------------------------------------- 4: Create Worker */

static inline void win_worker(Loc &l)
{
	Fonts &f = l.g.f; Canvas &c = l.g.c; Ctx s = l.sub();
	fill_circle(c, l.X(108), l.Y(62), l.S(33), Rgba { 11, 12, 14, 1.0f });
	ring(c, l.X(108), l.Y(62), l.S(33), 1.2f * l.wg.s, Rgba { 70, 66, 62, 0.9f });
	Icon::cube(s, l.X(108), l.Y(62), l.wg.s * 1.0f, Pal::white, 1.8f);
	l.text(f.med, 163, 40, 21, Pal::white, "Create a New Worker");
	Icon::close_x(s, l.X(705), l.Y(48), l.wg.s * 1.0f, Rgba { 255, 160, 48, 1.0f }, 2.4f);

	static char const *const lab[5] = { "Purpose", "Trigger", "Requires", "Provides", "Resource Budget" };
	static float const ly[5] = { 115, 168, 218, 268, 318 };
	for (int i = 0; i < 5; ++i) l.text(f.reg, 78, ly[i] - 9, 14.5f, Rgba { 214, 216, 220, 0.97f }, lab[i]);
	field(l, 235, 95, 718, 135, "Take a photo of my hydroponic seedling", false);
	field(l, 235, 148, 718, 188, "Twice daily (07:00 and 19:00)", true);
	/* chips */
	l.rr(236, 200, 154, 38, 10, Rgba { 22, 23, 26, 1.0f }, Rgba { 58, 58, 62, 0.95f }, 1.0f);
	l.disc(250, 219, 3, Pal::orange);
	l.text(f.reg, 258, 209.5f, 15, Pal::white, "Camera Capture");
	Icon::chevron_down(s, l.X(380), l.Y(219), l.wg.s * 0.5f, Rgba { 220, 222, 226, 0.9f });
	l.rr(400, 200, 85, 38, 10, Rgba { 22, 23, 26, 1.0f }, Rgba { 58, 58, 62, 0.95f }, 1.0f);
	l.ln(416, 219, 428, 219, 1.5f, Pal::white); l.ln(422, 213, 422, 225, 1.5f, Pal::white);
	l.text(f.reg, 436, 209.5f, 15, Pal::white, "Add");
	l.rr(236, 250, 226, 38, 10, Rgba { 22, 23, 26, 1.0f }, Rgba { 58, 58, 62, 0.95f }, 1.0f);
	l.text(f.reg, 251, 259.5f, 15, Pal::white, "Image");
	Icon::arrow_right(s, l.X(306), l.Y(269), l.wg.s * 0.42f, Pal::orange, 2.0f);
	l.text(f.reg, 320, 259.5f, 15, Pal::white, "Hydroponics Agent");
	field(l, 235, 300, 718, 340, "Minimal", true);

	button(l, 408, 355, 537, 392, "Cancel", false);
	button(l, 557, 355, 704, 392, "Create Worker", true);
}

/* --------------------------------------------------------- 5: Software & Updates */

static inline void win_updates(Loc &l)
{
	Fonts &f = l.g.f; Canvas &c = l.g.c; Ctx s = l.sub();
	l.text(f.med, 75, 43, 21, Pal::white, "Software & Updates");
	static char const *const items[4] = { "Updates", "Installed", "Sources", "Recovery" };
	static float const ys[4] = { 106, 145, 182, 218 };
	for (int i = 0; i < 4; ++i) nav_item(l, f.med, f.reg, 88, 109, ys[i], 15, items[i], i == 0, i % 7, 66, 256, 42);
	l.ln(268, 80, 268, 395, 1.0f, Rgba { 92, 78, 62, 0.7f });

	l.rr(318, 95, 72, 70, 16, Rgba { 22, 48, 34, 1.0f }, Rgba { 52, 84, 62, 0.9f }, 1.2f);
	Icon::check(s, l.X(354), l.Y(130), l.wg.s * 1.3f, Rgba { 84, 224, 116, 1.0f }, 2.8f);
	l.text(f.med, 408, 92, 20, Pal::white, "System is up to date");
	l.text(f.reg, 408, 127, 13.5f, Rgba { 200, 204, 210, 1.0f }, "HermOS 0.1 (Prototype)");
	l.text(f.reg, 408, 151, 13.5f, Rgba { 200, 204, 210, 1.0f }, "Last checked: 10 Dec 2024 07:40");

	l.rr(296, 190, 682, 205, 14, Rgba { 19, 20, 23, 0.96f }, Rgba { 60, 60, 64, 0.9f }, 1.0f);
	l.text(f.med, 320, 205, 17, Pal::white, "Independent System Evolution");
	l.text(f.reg, 320, 232, 12.5f, Rgba { 200, 204, 210, 1.0f }, "This HermOS installation can maintain itself securely.");
	l.ln(320, 258, 955, 258, 1.0f, Rgba { 90, 88, 86, 0.4f });
	static char const *const st[5] = { "Verify", "Stage", "Test", "Activate", "Monitor" };
	static float const sx[5] = { 357, 495, 627, 757, 890 };
	for (int i = 0; i < 5; ++i) {
		bool const hot = (i == 0 || i == 3 || i == 4);
		Rgba const ic = hot ? Rgba { 255, 154, 52, 1.0f } : Rgba { 226, 228, 232, 0.95f };
		l.rr(sx[i] - 17, 271, 34, 34, 17, Rgba { 24, 22, 20, 1.0f }, Rgba { 0, 0, 0, 0 });
		l.rng(sx[i], 288, 16, 1.5f, ic);
		l.disc(sx[i], 288, 2.4f, ic);
		l.textc(f.reg, sx[i], 309, 13, hot ? Pal::peach : Rgba { 226, 228, 232, 0.95f }, st[i]);
		if (i < 4) Icon::arrow_right(s, l.X((sx[i] + sx[i + 1]) / 2), l.Y(288), l.wg.s * 0.46f, Rgba { 222, 224, 228, 0.9f }, 1.6f);
	}
	Icon::arrow_right(s, l.X(325), l.Y(288), l.wg.s * 0.3f, Pal::orange, 2.0f);
	Icon::arrow_right(s, l.X(928), l.Y(288), l.wg.s * 0.3f, Pal::orange, 2.0f);
	l.rr(320, 346, 192, 39, 10, Rgba { 36, 37, 40, 1.0f }, Rgba { 74, 74, 80, 0.95f }, 1.0f);
	l.textc(f.med, 416, 355, 15.5f, Pal::white, "Check for Updates");
	win_controls(l, 872, 0, 925, 966, 43, false);
	(void)c;
}

/* ------------------------------------------------- window control buttons */

struct CtrlDef { float undo, mn, mx, close, cy; };    /* reference-sheet (1.9x) coordinates, 0 = absent */
static constexpr CtrlDef CTRL[6] = {
	{ 780, 810, 843, 886, 41 }, { 758,   0, 810, 853, 43 }, { 780, 810, 842, 885, 38 },
	{   0, 950, 1000, 1045, 42 }, { 0, 0, 0, 705, 48 },     { 872,   0, 925, 966, 43 },
};
enum { CTRL_UNDO = 0, CTRL_MIN, CTRL_MAX, CTRL_CLOSE };

struct Ctrls { float x[4]; float y; float r; bool has[4]; };

/* where the control buttons of window i sit (canvas px), for drawing and hit-testing */
static inline Ctrls window_ctrls(int i, WinRect const &r)
{
	WinRef const &ref = WIN_REF[i]; CtrlDef const &d = CTRL[i];
	float const s = minf(r.w / ref.w, r.h / ref.h);
	auto X = [&](float cx) { return r.x + (cx / 1.9f - ref.ox) * s; };
	Ctrls c {};
	float const v[4] = { d.undo, d.mn, d.mx, d.close };
	for (int k = 0; k < 4; ++k) { c.has[k] = v[k] > 0; c.x[k] = c.has[k] ? X(v[k]) : 0; }
	c.y = r.y + (d.cy / 1.9f - ref.oy) * s;
	c.r = 9.0f * s;
	return c;
}

/* ------------------------------------------------------------------ dispatch */

static inline void draw_window(Ctx &g, int i, WinRect const &r, bool active, bool hosted, State const &st)
{
	WinRef const &ref = WIN_REF[i];
	WinGeom wg { r.x, r.y, r.w, r.h, minf(r.w / ref.w, r.h / ref.h) };
	window_frame(g, wg, active, hosted);
	if (hosted) return;                   /* a real app is blitted over the content */
	Loc l { g, wg, ref.ox, ref.oy, 0 };
	switch (i) {
	case 0: win_welcome(l); break;
	case 1: win_system(l);  break;
	case 2: win_files(l);   break;
	case 3: win_hydro(l);   break;
	case 4: win_worker(l);  break;
	case 5: win_updates(l); break;
	}
	(void)st;
}

} /* namespace Scene */

#endif /* _HERMOS_SCENE_WINDOWS_H_ */
