/*
 * HermOS scene graphics — freestanding anti-aliased software renderer.
 *
 * No libc, no heap, no Genode dependency: the same header builds into the
 * desktop_builder (Genode/NOVA) and into the host preview tool used to diff
 * the desktop against the design reference. Shapes are rendered from signed
 * distance fields, so every edge, border and glow is analytically
 * anti-aliased at any resolution. Text uses stb_truetype with a small arena
 * allocator (stb_truetype needs malloc for glyph outlines; without it the
 * glyph bitmap is never written).
 */

#ifndef _HERMOS_SCENE_GFX_H_
#define _HERMOS_SCENE_GFX_H_

#include <stddef.h>
#include <stdint.h>

#ifdef HERMOS_HOST
#  define HM_STRLEN   __builtin_strlen
#  define HM_MEMCPY   __builtin_memcpy
#else
#  include <util/string.h>
#  define HM_STRLEN   Genode::strlen
#  define HM_MEMCPY   Genode::memcpy
#endif

namespace Scene {

/* ---- tiny math (compiler builtins; sqrtf/expf resolve against libm) ---- */

static inline float fabsf_(float x)           { return x < 0 ? -x : x; }
static inline float minf(float a, float b)    { return a < b ? a : b; }
static inline float maxf(float a, float b)    { return a > b ? a : b; }
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float lerpf(float a, float b, float t)    { return a + (b - a) * t; }
static inline float sqrtf_(float x)           { return __builtin_sqrtf(x); }
static inline float expf_(float x)            { return __builtin_expf(x); }
static inline float floorf_(float x)          { return __builtin_floorf(x); }
static inline float sinf_(float x)            { return __builtin_sinf(x); }
static inline float cosf_(float x)            { return __builtin_cosf(x); }
static inline float atan2f_(float y, float x) { return __builtin_atan2f(y, x); }

static constexpr float PI_ = 3.14159265f;

/* ---- colour + canvas ---------------------------------------------------- */

struct Rgba
{
	float r, g, b, a;   /* r,g,b: 0..255   a: 0..1 */
	/* a plain colour doubles as a constant paint functor */
	Rgba operator()(float, float) const { return *this; }
};

static inline Rgba rgba(float r, float g, float b, float a = 1.0f) { return { r, g, b, a }; }
static inline Rgba with_a(Rgba c, float a) { return { c.r, c.g, c.b, c.a * a }; }
static inline Rgba mix(Rgba x, Rgba y, float t)
{ return { lerpf(x.r, y.r, t), lerpf(x.g, y.g, t), lerpf(x.b, y.b, t), lerpf(x.a, y.a, t) }; }

struct Canvas
{
	uint32_t *px;     /* 0x00RRGGBB (Genode Pixel_rgb888 layout) */
	int       w, h;
};

static inline void put(Canvas &c, int x, int y, float r, float g, float b, float a)
{
	if (x < 0 || y < 0 || x >= c.w || y >= c.h || a <= 0.002f) return;
	uint32_t &d = c.px[y * c.w + x];
	if (a > 0.998f) {
		d = (uint32_t(clampf(r, 0, 255) + 0.5f) << 16) | (uint32_t(clampf(g, 0, 255) + 0.5f) << 8)
		  |  uint32_t(clampf(b, 0, 255) + 0.5f);
		return;
	}
	float const dr = float((d >> 16) & 255), dg = float((d >> 8) & 255), db = float(d & 255);
	d = (uint32_t(clampf(dr + (r - dr) * a, 0, 255) + 0.5f) << 16)
	  | (uint32_t(clampf(dg + (g - dg) * a, 0, 255) + 0.5f) << 8)
	  |  uint32_t(clampf(db + (b - db) * a, 0, 255) + 0.5f);
}

static inline Rgba get(Canvas const &c, int x, int y)
{
	if (x < 0) x = 0; if (y < 0) y = 0; if (x >= c.w) x = c.w - 1; if (y >= c.h) y = c.h - 1;
	uint32_t const d = c.px[y * c.w + x];
	return { float((d >> 16) & 255), float((d >> 8) & 255), float(d & 255), 1.0f };
}

/* ---- signed-distance shapes --------------------------------------------- */

struct Pt { float x, y; };

/* rounded box centred at (cx,cy) */
static inline float sd_rrect(float px, float py, float cx, float cy, float hw, float hh, float r)
{
	float const qx = fabsf_(px - cx) - (hw - r), qy = fabsf_(py - cy) - (hh - r);
	float const ox = maxf(qx, 0), oy = maxf(qy, 0);
	return sqrtf_(ox * ox + oy * oy) + minf(maxf(qx, qy), 0) - r;
}

static inline float sd_circle(float px, float py, float cx, float cy, float r)
{
	float const dx = px - cx, dy = py - cy;
	return sqrtf_(dx * dx + dy * dy) - r;
}

/* capsule (rounded line) of half-width hw */
static inline float sd_capsule(float px, float py, float x0, float y0, float x1, float y1, float hw)
{
	float const pax = px - x0, pay = py - y0, bax = x1 - x0, bay = y1 - y0;
	float const bb = bax * bax + bay * bay;
	float const h = bb > 0 ? clampf((pax * bax + pay * bay) / bb, 0, 1) : 0;
	float const dx = pax - bax * h, dy = pay - bay * h;
	return sqrtf_(dx * dx + dy * dy) - hw;
}

/* convex polygon (any winding): max over edge half-planes */
static constexpr int MAX_POLY = 16;
struct Poly
{
	Pt   v[MAX_POLY];
	int  n { 0 };
	float nx[MAX_POLY], ny[MAX_POLY], c[MAX_POLY];

	void add(float x, float y) { if (n < MAX_POLY) { v[n].x = x; v[n].y = y; ++n; } }

	void finish()
	{
		/* orientation via signed area so normals always point outward */
		float area = 0;
		for (int i = 0; i < n; ++i) { Pt a = v[i], b = v[(i + 1) % n]; area += a.x * b.y - b.x * a.y; }
		float const sgn = area > 0 ? 1.0f : -1.0f;
		for (int i = 0; i < n; ++i) {
			Pt const a = v[i], b = v[(i + 1) % n];
			float ex = b.x - a.x, ey = b.y - a.y;
			float const l = sqrtf_(ex * ex + ey * ey);
			if (l > 0) { ex /= l; ey /= l; }
			nx[i] = ey * sgn; ny[i] = -ex * sgn;
			c[i] = nx[i] * a.x + ny[i] * a.y;
		}
	}

	float sd(float px, float py) const
	{
		float d = -1e9f;
		for (int i = 0; i < n; ++i) { float const e = nx[i] * px + ny[i] * py - c[i]; if (e > d) d = e; }
		return d;
	}
};

/* rectangle with individually chamfered corners (tl, tr, br, bl) */
static inline void chamfer_poly(Poly &p, float x, float y, float w, float h,
                                float tl, float tr, float br, float bl)
{
	p.n = 0;
	if (tl > 0) { p.add(x + tl, y);         } else p.add(x, y);
	if (tr > 0) { p.add(x + w - tr, y); p.add(x + w, y + tr); } else p.add(x + w, y);
	if (br > 0) { p.add(x + w, y + h - br); p.add(x + w - br, y + h); } else p.add(x + w, y + h);
	if (bl > 0) { p.add(x + bl, y + h); p.add(x, y + h - bl); } else p.add(x, y + h);
	if (tl > 0) p.add(x, y + tl);
	p.finish();
}

/* ---- generic SDF painting ----------------------------------------------- */

/* fill the pixels where sd < 0 with colour fn(x,y); bounds are clamped */
template <class SD, class COL>
static inline void fill_sd(Canvas &c, float bx0, float by0, float bx1, float by1, SD sd, COL col)
{
	int const x0 = int(floorf_(bx0)) - 1, y0 = int(floorf_(by0)) - 1;
	int const x1 = int(floorf_(bx1)) + 2, y1 = int(floorf_(by1)) + 2;
	for (int y = y0 < 0 ? 0 : y0; y < (y1 > c.h ? c.h : y1); ++y)
		for (int x = x0 < 0 ? 0 : x0; x < (x1 > c.w ? c.w : x1); ++x) {
			float const d = sd(float(x) + 0.5f, float(y) + 0.5f);
			float const cov = clampf(0.5f - d, 0, 1);
			if (cov <= 0) continue;
			Rgba const k = col(float(x) + 0.5f, float(y) + 0.5f);
			put(c, x, y, k.r, k.g, k.b, k.a * cov);
		}
}

/* an outline of width t centred on sd==0 */
template <class SD, class COL>
static inline void stroke_sd(Canvas &c, float bx0, float by0, float bx1, float by1, float t, SD sd, COL col)
{
	int const x0 = int(floorf_(bx0 - t)) - 1, y0 = int(floorf_(by0 - t)) - 1;
	int const x1 = int(floorf_(bx1 + t)) + 2, y1 = int(floorf_(by1 + t)) + 2;
	for (int y = y0 < 0 ? 0 : y0; y < (y1 > c.h ? c.h : y1); ++y)
		for (int x = x0 < 0 ? 0 : x0; x < (x1 > c.w ? c.w : x1); ++x) {
			float const d = sd(float(x) + 0.5f, float(y) + 0.5f);
			float const cov = clampf(t * 0.5f + 0.5f - fabsf_(d), 0, 1);
			if (cov <= 0) continue;
			Rgba const k = col(float(x) + 0.5f, float(y) + 0.5f);
			put(c, x, y, k.r, k.g, k.b, k.a * cov);
		}
}

/* soft outer glow: alpha = a * exp(-d/sigma) outside (and fading inside) the shape */
template <class SD>
static inline void glow_sd(Canvas &c, float bx0, float by0, float bx1, float by1,
                           float reach, float sigma, Rgba col, SD sd)
{
	int const x0 = int(floorf_(bx0 - reach)), y0 = int(floorf_(by0 - reach));
	int const x1 = int(floorf_(bx1 + reach)) + 1, y1 = int(floorf_(by1 + reach)) + 1;
	for (int y = y0 < 0 ? 0 : y0; y < (y1 > c.h ? c.h : y1); ++y)
		for (int x = x0 < 0 ? 0 : x0; x < (x1 > c.w ? c.w : x1); ++x) {
			float const d = sd(float(x) + 0.5f, float(y) + 0.5f);
			if (d <= 0 || d > reach) continue;
			put(c, x, y, col.r, col.g, col.b, col.a * expf_(-d / sigma));
		}
}

/* ---- solid / gradient colour functors ----------------------------------- */

struct Solid { Rgba c; Rgba operator()(float, float) const { return c; } };
struct VGrad
{
	float y0, y1; Rgba a, b;
	Rgba operator()(float, float y) const { return mix(a, b, clampf((y - y0) / (y1 - y0), 0, 1)); }
};
struct HGrad
{
	float x0, x1; Rgba a, b;
	Rgba operator()(float x, float) const { return mix(a, b, clampf((x - x0) / (x1 - x0), 0, 1)); }
};
struct DGrad   /* diagonal (top-left -> bottom-right) */
{
	float x0, y0, x1, y1; Rgba a, b;
	Rgba operator()(float x, float y) const
	{
		float const dx = x1 - x0, dy = y1 - y0;
		float const t = ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy);
		return mix(a, b, clampf(t, 0, 1));
	}
};

/* ---- convenience wrappers ----------------------------------------------- */

template <class COL>
static inline void fill_rrect(Canvas &c, float x, float y, float w, float h, float r, COL col)
{
	float const cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	fill_sd(c, x, y, x + w, y + h, [=](float px, float py) { return sd_rrect(px, py, cx, cy, hw, hh, r); }, col);
}
template <class COL>
static inline void stroke_rrect(Canvas &c, float x, float y, float w, float h, float r, float t, COL col)
{
	float const cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	stroke_sd(c, x, y, x + w, y + h, t, [=](float px, float py) { return sd_rrect(px, py, cx, cy, hw, hh, r); }, col);
}
template <class COL>
static inline void fill_poly(Canvas &c, Poly const &p, COL col)
{
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (int i = 0; i < p.n; ++i) { x0 = minf(x0, p.v[i].x); y0 = minf(y0, p.v[i].y); x1 = maxf(x1, p.v[i].x); y1 = maxf(y1, p.v[i].y); }
	fill_sd(c, x0, y0, x1, y1, [&](float px, float py) { return p.sd(px, py); }, col);
}
template <class COL>
static inline void stroke_poly(Canvas &c, Poly const &p, float t, COL col)
{
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (int i = 0; i < p.n; ++i) { x0 = minf(x0, p.v[i].x); y0 = minf(y0, p.v[i].y); x1 = maxf(x1, p.v[i].x); y1 = maxf(y1, p.v[i].y); }
	stroke_sd(c, x0, y0, x1, y1, t, [&](float px, float py) { return p.sd(px, py); }, col);
}
static inline void glow_poly(Canvas &c, Poly const &p, float reach, float sigma, Rgba col)
{
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (int i = 0; i < p.n; ++i) { x0 = minf(x0, p.v[i].x); y0 = minf(y0, p.v[i].y); x1 = maxf(x1, p.v[i].x); y1 = maxf(y1, p.v[i].y); }
	glow_sd(c, x0, y0, x1, y1, reach, sigma, col, [&](float px, float py) { return p.sd(px, py); });
}
static inline void fill_circle(Canvas &c, float cx, float cy, float r, Rgba col)
{
	fill_sd(c, cx - r, cy - r, cx + r, cy + r, [=](float px, float py) { return sd_circle(px, py, cx, cy, r); }, Solid { col });
}
static inline void ring(Canvas &c, float cx, float cy, float r, float t, Rgba col)
{
	stroke_sd(c, cx - r, cy - r, cx + r, cy + r, t, [=](float px, float py) { return sd_circle(px, py, cx, cy, r); }, Solid { col });
}
static inline void line(Canvas &c, float x0, float y0, float x1, float y1, float t, Rgba col)
{
	float const hw = t * 0.5f;
	fill_sd(c, minf(x0, x1) - hw, minf(y0, y1) - hw, maxf(x0, x1) + hw, maxf(y0, y1) + hw,
	        [=](float px, float py) { return sd_capsule(px, py, x0, y0, x1, y1, hw); }, Solid { col });
}
/* arc of a circle between angles a0..a1 (radians), stroke width t */
static inline void arc(Canvas &c, float cx, float cy, float r, float a0, float a1, float t, Rgba col)
{
	int const steps = 12 + int(fabsf_(a1 - a0) * r * 0.25f);
	float px = cx + cosf_(a0) * r, py = cy + sinf_(a0) * r;
	for (int i = 1; i <= steps; ++i) {
		float const a = lerpf(a0, a1, float(i) / float(steps));
		float const nx = cx + cosf_(a) * r, ny = cy + sinf_(a) * r;
		line(c, px, py, nx, ny, t, col);
		px = nx; py = ny;
	}
}

/* ---- images ------------------------------------------------------------- */

struct Image { uint8_t const *rgba; int w, h; int stride { 0 }; };   /* straight RGBA8; stride in px (0 = w) */

/* bilinear sample, returns straight RGBA */
static inline Rgba sample(Image const &im, float u, float v)   /* u,v in pixels */
{
	u = clampf(u - 0.5f, 0, float(im.w - 1)); v = clampf(v - 0.5f, 0, float(im.h - 1));
	int const x0 = int(u), y0 = int(v);
	int const x1 = x0 + 1 < im.w ? x0 + 1 : x0, y1 = y0 + 1 < im.h ? y0 + 1 : y0;
	float const fx = u - float(x0), fy = v - float(y0);
	int const st_ = im.stride ? im.stride : im.w;
	auto px = [&](int x, int y) { uint8_t const *p = im.rgba + (y * st_ + x) * 4; return Rgba { float(p[0]), float(p[1]), float(p[2]), float(p[3]) / 255.0f }; };
	Rgba const a = px(x0, y0), b = px(x1, y0), c = px(x0, y1), d = px(x1, y1);
	return mix(mix(a, b, fx), mix(c, d, fx), fy);
}

/* draw the image scaled into a destination rect, masked by an SDF (box-filtered
 * when shrinking so thumbnails stay smooth) */
template <class SD>
static inline void draw_image(Canvas &c, Image const &im, float x, float y, float w, float h,
                              SD sd, float alpha = 1.0f)
{
	float const sx = float(im.w) / w, sy = float(im.h) / h;
	int const taps = (sx > 1.5f || sy > 1.5f) ? 3 : 1;
	fill_sd(c, x, y, x + w, y + h, sd, [&](float px, float py) {
		float const u = (px - x) * sx, v = (py - y) * sy;
		if (taps == 1) { Rgba k = sample(im, u, v); k.a *= alpha; return k; }
		Rgba acc { 0, 0, 0, 0 }; float const o = 0.33f;
		for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
			Rgba k = sample(im, u + float(i) * o * sx, v + float(j) * o * sy);
			acc.r += k.r; acc.g += k.g; acc.b += k.b; acc.a += k.a;
		}
		return Rgba { acc.r / 9, acc.g / 9, acc.b / 9, acc.a / 9 * alpha };
	});
}

static inline void draw_image_rect(Canvas &c, Image const &im, float x, float y, float w, float h, float alpha = 1.0f)
{
	float const cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	draw_image(c, im, x, y, w, h, [=](float px, float py) { return sd_rrect(px, py, cx, cy, hw, hh, 0.0f); }, alpha);
}

/* ---- arena allocator for stb_truetype ----------------------------------- */

struct Arena
{
	alignas(16) uint8_t mem[512 * 1024];
	size_t used { 0 };
	void *alloc(size_t n)
	{
		n = (n + 15) & ~size_t(15);
		if (used + n > sizeof(mem)) return nullptr;
		void *p = mem + used; used += n; return p;
	}
	void reset() { used = 0; }
};

inline Arena g_text_arena;
static inline Arena &text_arena() { return g_text_arena; }

} /* namespace Scene */

/* stb_truetype configuration: no libc */
#define STBTT_ifloor(x)   ((int)__builtin_floorf(x))
#define STBTT_iceil(x)    ((int)__builtin_ceilf(x))
#define STBTT_sqrt(x)     __builtin_sqrtf(x)
#define STBTT_pow(x,y)    __builtin_powf(x,y)
#define STBTT_fmod(x,y)   __builtin_fmodf(x,y)
#define STBTT_cos(x)      __builtin_cosf(x)
#define STBTT_acos(x)     __builtin_acosf(x)
#define STBTT_fabs(x)     __builtin_fabsf(x)
#define STBTT_strlen(x)   HM_STRLEN(x)
#define STBTT_memcpy      HM_MEMCPY
static inline void *hm_stbtt_memset(void *s, int c, size_t n)
{ unsigned char *p = (unsigned char *)s; while (n--) *p++ = (unsigned char)c; return s; }
#define STBTT_memset      hm_stbtt_memset
#define STBTT_assert(x)   ((void)0)
#define STBTT_malloc(x,u) ((void)(u), Scene::text_arena().alloc(x))
#define STBTT_free(x,u)   ((void)(u), (void)(x))
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace Scene {

/* ---- text --------------------------------------------------------------- */

struct Font
{
	stbtt_fontinfo info;
	bool ok { false };

	bool load(void const *data)
	{
		if (!data) return ok = false;
		unsigned char const *d = (unsigned char const *)data;
		ok = stbtt_InitFont(&info, d, stbtt_GetFontOffsetForIndex(d, 0)) != 0;
		return ok;
	}
};

static inline int utf8_next(char const *&s)
{
	unsigned char const *u = (unsigned char const *)s;
	if (u[0] < 0x80) { s += 1; return u[0]; }
	if ((u[0] & 0xE0) == 0xC0) { s += 2; return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); }
	if ((u[0] & 0xF0) == 0xE0) { s += 3; return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); }
	if ((u[0] & 0xF8) == 0xF0) { s += 4; return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); }
	s += 1; return 0xFFFD;
}

/* pixel size -> advance width (px) of a string, with extra per-glyph tracking */
static inline float text_width(Font &f, float px, char const *t, float tracking = 0)
{
	if (!f.ok || !t) return 0;
	float const sc = stbtt_ScaleForMappingEmToPixels(&f.info, px);
	float w = 0; int prev = 0;
	for (char const *p = t; *p; ) {
		int const cp = utf8_next(p);
		int adv, lsb; stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
		if (prev) w += float(stbtt_GetCodepointKernAdvance(&f.info, prev, cp)) * sc;
		w += float(adv) * sc + tracking; prev = cp;
	}
	return w;
}

/* Draw text with its top (ascender line) at y. Returns the advance width.
 * `bold` thickens the strokes by re-drawing with a sub-pixel offset. */
static inline float draw_text(Canvas &c, Font &f, float x, float y, float px, Rgba col,
                              char const *t, float tracking = 0, float bold = 0)
{
	if (!f.ok || !t) return 0;
	float const sc = stbtt_ScaleForMappingEmToPixels(&f.info, px);
	int asc, desc, gap; stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
	float const base = y + float(asc) * sc;
	float pen = x; int prev = 0;
	for (char const *p = t; *p; ) {
		int const cp = utf8_next(p);
		int adv, lsb; stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
		if (prev) pen += float(stbtt_GetCodepointKernAdvance(&f.info, prev, cp)) * sc;
		prev = cp;

		int x0, y0, x1, y1;
		float const fx = pen - floorf_(pen);
		stbtt_GetCodepointBitmapBoxSubpixel(&f.info, cp, sc, sc, fx, 0, &x0, &y0, &x1, &y1);
		int const gw = x1 - x0, gh = y1 - y0;
		if (gw > 0 && gh > 0 && gw <= 256 && gh <= 256) {
			text_arena().reset();
			uint8_t *bmp = (uint8_t *)text_arena().alloc(size_t(gw) * size_t(gh));
			if (bmp) {
				for (int i = 0; i < gw * gh; ++i) bmp[i] = 0;
				stbtt_MakeCodepointBitmapSubpixel(&f.info, bmp, gw, gh, gw, sc, sc, fx, 0, cp);
				int const ox = int(floorf_(pen)) + x0, oy = int(floorf_(base)) + y0 + int(base - floorf_(base) + 0.5f);
				int const passes = bold > 0.01f ? 2 : 1;
				for (int pass = 0; pass < passes; ++pass) {
					int const dx = pass ? int(bold + 0.5f) : 0;
					for (int j = 0; j < gh; ++j)
						for (int i = 0; i < gw; ++i) {
							uint8_t const a = bmp[j * gw + i];
							if (a) put(c, ox + i + dx, oy + j, col.r, col.g, col.b, col.a * float(a) / 255.0f);
						}
				}
			}
		}
		pen += float(adv) * sc + tracking;
	}
	return pen - x;
}

static inline float draw_text_center(Canvas &c, Font &f, float cx, float y, float px, Rgba col,
                                     char const *t, float tracking = 0, float bold = 0)
{
	float const w = text_width(f, px, t, tracking);
	draw_text(c, f, cx - w * 0.5f, y, px, col, t, tracking, bold);
	return w;
}

static inline float draw_text_right(Canvas &c, Font &f, float rx, float y, float px, Rgba col,
                                    char const *t, float tracking = 0, float bold = 0)
{
	float const w = text_width(f, px, t, tracking);
	draw_text(c, f, rx - w, y, px, col, t, tracking, bold);
	return w;
}

} /* namespace Scene */

#endif /* _HERMOS_SCENE_GFX_H_ */
