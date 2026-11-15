/*
 * HermOS PNG renderer — decode a PNG blob (stb_image, forced RGBA) and
 * alpha-blit it onto a framebuffer Surface at (x, y). Returns true on success.
 */

#ifndef _HERMOS_PNG_RENDERER_H_
#define _HERMOS_PNG_RENDERER_H_

#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_JPEG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM

/* The desktop no longer links libc, so stb_image's C-library dependencies
 * (stdio.h/stdlib.h/stdarg.h + malloc/free/realloc) are provided natively.
 * The allocator is a size-tracked wrapper over a Genode::Allocator supplied
 * by the caller via Hermos::_stb_alloc before decoding. */
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,t)   __builtin_va_arg(v,t)

namespace Hermos { inline Genode::Allocator *_stb_alloc { nullptr }; }

inline void *_stb_malloc(size_t n)
{
	if (!Hermos::_stb_alloc) return nullptr;
	size_t *const p = (size_t *)Hermos::_stb_alloc->alloc(n + sizeof(size_t));
	if (!p) return nullptr;
	*p = n;
	return p + 1;
}

inline void _stb_free(void *ptr)
{
	if (!ptr || !Hermos::_stb_alloc) return;
	size_t *const p = (size_t *)ptr - 1;
	Hermos::_stb_alloc->free(p, *p + sizeof(size_t));
}

inline void *_stb_realloc(void *ptr, size_t newsz)
{
	size_t const oldsz = ptr ? *((size_t *)ptr - 1) : 0;
	void  *const np    = _stb_malloc(newsz);
	if (np && ptr && oldsz) Genode::memcpy(np, ptr, oldsz < newsz ? oldsz : newsz);
	_stb_free(ptr);
	return np;
}

/* stb_image calls the C library's memcpy/memset directly; provide them at
 * global scope since the desktop no longer links libc. */
inline void *memcpy(void *dst, void const *src, size_t n)
{ return Genode::memcpy(dst, src, n); }

inline void *memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	while (n--) *p++ = (unsigned char)c;
	return s;
}

#define STBI_MALLOC(sz)              _stb_malloc(sz)
#define STBI_REALLOC(p,newsz)        _stb_realloc(p, newsz)
#define STBI_REALLOC_SIZED(p,o,newsz) _stb_realloc(p, newsz)
#define STBI_FREE(p)                 _stb_free(p)

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <os/surface.h>

namespace Hermos {

using namespace Genode;

inline bool draw_png(Surface<Pixel_rgb888> &s, int x, int y,
                     void const *png_data, size_t png_size)
{
	int w = 0, h = 0, n = 0;
	unsigned char *img = stbi_load_from_memory((unsigned char const *)png_data,
	                                           (int)png_size, &w, &h, &n, 4);
	if (!img) return false;

	Pixel_rgb888 *base = s.addr();
	int const W = s.size().w, H = s.size().h;

	for (int j = 0; j < h; j++) {
		int const dy = y + j;
		if (dy < 0 || dy >= H) continue;
		for (int i = 0; i < w; i++) {
			int const dx = x + i;
			if (dx < 0 || dx >= W) continue;
			unsigned char const *p = img + (size_t(j) * w + i) * 4;
			unsigned const a = p[3];
			if (!a) continue;

			Pixel_rgb888 &dst = base[dy * W + dx];
			unsigned const ia = a, ib = 255 - ia;
			int const r = (p[0] * ia + dst.r() * ib) / 255;
			int const g = (p[1] * ia + dst.g() * ib) / 255;
			int const b = (p[2] * ia + dst.b() * ib) / 255;
			dst.rgba(r, g, b);
		}
	}

	stbi_image_free(img);
	return true;
}

/* decode a PNG to raw RGB (no alpha) and return the dimensions.
 * Caller frees the buffer with stbi_image_free(). Returns nullptr on failure. */
struct Png_rgb { unsigned char *px; int w; int h; };

inline Png_rgb load_png_rgb(void const *png_data, size_t png_size)
{
	Png_rgb out { nullptr, 0, 0 };
	out.px = stbi_load_from_memory((unsigned char const *)png_data,
	                               (int)png_size, &out.w, &out.h, nullptr, 3);
	return out;
}

} /* namespace Hermos */

#endif /* _HERMOS_PNG_RENDERER_H_ */
