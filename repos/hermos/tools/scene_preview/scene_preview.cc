/*
 * Host preview of the HermOS desktop scene.
 *
 *   g++ -O2 -std=c++17 -DHERMOS_HOST -I<desktop_builder> -I<desktop_builder>/vendor/stb \
 *       scene_preview.cc -o scene_preview
 *   ./scene_preview <assets-dir> <fonts-dir> <out.ppm> [width height]
 *
 * Runs the very same Scene::render() that desktop_builder runs on Genode and
 * writes the frame as a PPM, so the layout can be diffed against the design
 * reference without booting an image.
 */
#ifndef HERMOS_HOST
#define HERMOS_HOST
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stb_image.h>
#include "scene.h"

static unsigned char *slurp(char const *path, size_t *n)
{
	FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
	fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *b = (unsigned char *)malloc(*n); if (fread(b, 1, *n, f) != *n) exit(1); fclose(f); return b;
}
static Scene::Image load_img(char const *dir, char const *name)
{
	char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name);
	size_t n; unsigned char *b = slurp(p, &n); int w, h, c;
	unsigned char *px = stbi_load_from_memory(b, (int)n, &w, &h, &c, 4);
	if (!px) { fprintf(stderr, "decode %s failed\n", p); exit(1); }
	return { px, w, h };
}

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: %s assets fonts out.ppm [w h] [left=1] [bottom=0]\n", argv[0]); return 2; }
	int const W = argc > 5 ? atoi(argv[4]) : 1536, H = argc > 5 ? atoi(argv[5]) : 1024;
	static Scene::Fonts fonts; static Scene::Assets assets; static Scene::State st; static Scene::Hits hits;
	char p[512]; size_t n;
	snprintf(p, sizeof p, "%s/Inter-Regular.ttf", argv[2]);  fonts.reg.load(slurp(p, &n));
	snprintf(p, sizeof p, "%s/Inter-Medium.ttf", argv[2]);   fonts.med.load(slurp(p, &n));
	snprintf(p, sizeof p, "%s/Inter-SemiBold.ttf", argv[2]); fonts.semi.load(slurp(p, &n));
	assets.wall = load_img(argv[1], "wallpaper.png"); assets.compass = load_img(argv[1], "compass.png");
	assets.hydro_thumb = load_img(argv[1], "hydro_thumb.png"); assets.hydro_big = load_img(argv[1], "hydro_big.png");

	st.cw = W; st.ch = H;
	float const k = Scene::minf(W / Scene::DW, H / Scene::DH);
	float const ox = (W - Scene::DW * k) * 0.5f, oy = (H - Scene::DH * k) * 0.5f;
	static float const d[6][4] = { { 22, 555, 505, 245 }, { 555, 560, 450, 241 }, { 1065, 562, 450, 241 },
	                               { 12, 815, 565, 206 }, { 604, 815, 372, 204 }, { 1001, 814, 511, 207 } };
	for (int i = 0; i < 6; ++i) st.win[i] = { ox + d[i][0] * k, oy + d[i][1] * k, d[i][2] * k, d[i][3] * k };
	if (argc > 6) { st.left.pinned = atoi(argv[6]) != 0; st.left.t = st.left.pinned ? 1.0f : 0.0f; }
	if (argc > 7) { st.bottom.pinned = false; st.bottom.mode = atoi(argv[7]) ? Scene::Visibility::AlwaysVisible : Scene::Visibility::Hidden; st.bottom.t = atoi(argv[7]) ? 1.0f : 0.0f; }
	else st.bottom.t = 1.0f;

	static uint32_t *buf; buf = (uint32_t *)calloc((size_t)W * H, 4);
	Scene::Canvas c { buf, W, H };
	Scene::render(c, fonts, assets, st, hits);

	FILE *o = fopen(argv[3], "wb"); fprintf(o, "P6\n%d %d\n255\n", W, H);
	for (int i = 0; i < W * H; ++i) { unsigned char rgb[3] = { (unsigned char)(buf[i] >> 16), (unsigned char)(buf[i] >> 8), (unsigned char)buf[i] }; fwrite(rgb, 1, 3, o); }
	fclose(o); return 0;
}
