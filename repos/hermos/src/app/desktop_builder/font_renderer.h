/*
 * HermOS FontRenderer — anti-aliased TrueType text drawn straight to a
 * framebuffer Surface via stb_truetype. Load a .ttf blob once (from the
 * /assets VFS), then draw_text() as often as needed. Each glyph is rasterized
 * to a coverage bitmap and alpha-blended onto the existing framebuffer pixels.
 */

#ifndef _HERMOS_FONT_RENDERER_H_
#define _HERMOS_FONT_RENDERER_H_

/* stb_truetype (implementation + allocator) lives in scene_gfx.h. Its
 * STBTT_malloc is an arena allocator; the old nullptr allocator left every
 * glyph bitmap unwritten, which showed up as scrambled text. */
#include "scene_gfx.h"

#include <os/surface.h>
#include <util/color.h>

namespace Hermos {

using namespace Genode;

class FontRenderer
{
	private:

		stbtt_fontinfo _info;
		bool           _ok { false };

		/* decode one UTF-8 codepoint, advance `next` past it */
		static int _decode(char const *s, char const **next)
		{
			unsigned char const *u = (unsigned char const *)s;
			if (u[0] < 0x80)           { *next = s + 1; return u[0]; }
			if ((u[0] & 0xE0) == 0xC0) { *next = s + 2; return ((u[0] & 0x1F) << 6)  | (u[1] & 0x3F); }
			if ((u[0] & 0xF0) == 0xE0) { *next = s + 3; return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); }
			if ((u[0] & 0xF8) == 0xF0) { *next = s + 4; return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); }
			*next = s + 1; return 0xFFFD;
		}

	public:

		FontRenderer() : _info() { }

		/* parse a .ttf blob; data must stay alive for the object's lifetime */
		bool load(void const *data)
		{
			if (!data) { _ok = false; return false; }
			unsigned char const *d = (unsigned char const *)data;
			_ok = stbtt_InitFont(&_info, d, stbtt_GetFontOffsetForIndex(d, 0)) != 0;
			return _ok;
		}

		bool ok() const { return _ok; }

		float line_height(float px) const
		{
			int asc, desc, gap;
			stbtt_GetFontVMetrics(&_info, &asc, &desc, &gap);
			return float(asc - desc + gap) * stbtt_ScaleForPixelHeight(&_info, px);
		}

		/* advance width of a UTF-8 string at the given pixel size */
		int text_width(float px, char const *text)
		{
			if (!_ok || !text) return 0;
			float const scale = stbtt_ScaleForPixelHeight(&_info, px);
			int width = 0;
			for (char const *p = text; *p; ) {
				char const *nxt;
				int const cp = _decode(p, &nxt);
				int adv, lsb;
				stbtt_GetCodepointHMetrics(&_info, cp, &adv, &lsb);
				width += int(adv * scale);
				p = nxt;
			}
			return width;
		}

		void draw_text(Surface<Pixel_rgb888> &s, int x, int y, float px,
		               Color c, char const *text)
		{
			if (!_ok || !text) return;

			float const scale = stbtt_ScaleForPixelHeight(&_info, px);

			int asc, desc, gap;
			stbtt_GetFontVMetrics(&_info, &asc, &desc, &gap);
			int const baseline = y + int(asc * scale);

			Pixel_rgb888 *base = s.addr();
			int const W = s.size().w, H = s.size().h;

			unsigned char bitmap[128 * 128];

			int pen_x = x;
			for (char const *p = text; *p; ) {
				char const *nxt;
				int const cp = _decode(p, &nxt);

				int adv, lsb;
				stbtt_GetCodepointHMetrics(&_info, cp, &adv, &lsb);

				int x0, y0, x1, y1;
				stbtt_GetCodepointBitmapBox(&_info, cp, scale, scale, &x0, &y0, &x1, &y1);
				int const gw = x1 - x0, gh = y1 - y0;

				if (gw > 0 && gh > 0 && gw <= 128 && gh <= 128) {
					Scene::text_arena().reset();
					for (int i = 0; i < gw * gh; ++i) bitmap[i] = 0;
					stbtt_MakeCodepointBitmap(&_info, bitmap, gw, gh, gw, scale, scale, cp);
					_blit_glyph(base, W, H, pen_x + x0, baseline + y0, gw, gh, bitmap, c);
				}

				pen_x += int(adv * scale);
				p = nxt;
			}
		}

	private:

		static void _blit_glyph(Pixel_rgb888 *base, int W, int H,
		                        int ox, int oy, int gw, int gh,
		                        unsigned char const *bitmap, Color c)
		{
			for (int j = 0; j < gh; j++) {
				int const y = oy + j;
				if (y < 0 || y >= H) continue;
				for (int i = 0; i < gw; i++) {
					int const x = ox + i;
					if (x < 0 || x >= W) continue;
					unsigned const a = bitmap[j * gw + i];
					if (!a) continue;

					Pixel_rgb888 &dst = base[y * W + x];
					unsigned const ia = a, ib = 255 - ia;
					int const r = (c.r * ia + dst.r() * ib) / 255;
					int const g = (c.g * ia + dst.g() * ib) / 255;
					int const b = (c.b * ia + dst.b() * ib) / 255;
					dst.rgba(r, g, b);
				}
			}
		}
};

} /* namespace Hermos */

#endif /* _HERMOS_FONT_RENDERER_H_ */
