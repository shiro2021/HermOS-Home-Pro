/*
 * The look of the app: the HermOS ink (orange accent when focused, carbon gray when not).
 * Nothing is hard-wired into the drawing code; hermes-agent.theme (next to the program, or in the
 * per-user folder) overrides any value, in the same "key: value" style as theme.hid.
 * Colours are "R,G,B"; lengths are fractions of the shorter side of the Cell in 1/10000.
 */
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "imgui.h"

namespace hermes {

struct Theme
{
	ImVec4 bg           { 11 / 255.f,  12 / 255.f,  14 / 255.f, 1 };
	ImVec4 panel        { 17 / 255.f,  18 / 255.f,  21 / 255.f, 1 };
	ImVec4 text         { 226 / 255.f, 229 / 255.f, 233 / 255.f, 1 };
	ImVec4 muted        { 150 / 255.f, 156 / 255.f, 164 / 255.f, 1 };
	ImVec4 accent       { 255 / 255.f, 156 / 255.f, 48 / 255.f, 1 };      /* focused */
	ImVec4 accent_light { 255 / 255.f, 200 / 255.f, 126 / 255.f, 1 };
	ImVec4 accent_deep  { 226 / 255.f, 92 / 255.f, 20 / 255.f, 1 };
	ImVec4 carbon       { 138 / 255.f, 143 / 255.f, 150 / 255.f, 1 };     /* not focused */
	ImVec4 carbon_edge  { 92 / 255.f,  96 / 255.f,  102 / 255.f, 1 };
	ImVec4 carbon_text  { 168 / 255.f, 172 / 255.f, 178 / 255.f, 1 };
	ImVec4 error        { 232 / 255.f, 84 / 255.f,  40 / 255.f, 1 };
	int    border   { 16 };        /* 1.6 px at 1000 px, like the HermOS frame */
	int    chamfer  { 135 };       /* 13.5 % of the shorter side, like the HermOS Cell */
	int    glow     { 160 };       /* glow reach */
	int    glow_alpha { 58 };      /* percent */
};

inline bool parse_rgb(char const *s, ImVec4 &out)
{
	int c[3] = { 0, 0, 0 }; int idx = 0; bool any = false;
	for (; *s && idx < 3; ++s) {
		if (*s >= '0' && *s <= '9') { c[idx] = c[idx] * 10 + (*s - '0'); any = true; }
		else if (*s == ',') ++idx;
	}
	if (!any || idx != 2) return false;
	out = ImVec4(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, 1);
	return true;
}

inline void load_theme_file(Theme &t, std::string const &path)
{
	std::ifstream f(path);
	if (!f) return;
	std::string line;
	while (std::getline(f, line)) {
		if (line.empty() || line[0] == '#') continue;
		size_t const colon = line.find(':');
		if (colon == std::string::npos) continue;
		std::string key = line.substr(0, colon), val = line.substr(colon + 1);
		auto trim = [] (std::string &s) {
			size_t b = s.find_first_not_of(" \t\r+"); size_t e = s.find_last_not_of(" \t\r");
			s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1); };
		trim(key); trim(val);
		struct { char const *k; ImVec4 *v; } const colours[] = {
			{ "bg", &t.bg }, { "panel", &t.panel }, { "text", &t.text }, { "muted", &t.muted },
			{ "accent", &t.accent }, { "accent_light", &t.accent_light }, { "accent_deep", &t.accent_deep },
			{ "carbon", &t.carbon }, { "carbon_edge", &t.carbon_edge }, { "carbon_text", &t.carbon_text },
			{ "error", &t.error } };
		for (auto const &c : colours) if (key == c.k) parse_rgb(val.c_str(), *c.v);
		struct { char const *k; int *v; } const nums[] = {
			{ "border", &t.border }, { "chamfer", &t.chamfer }, { "glow", &t.glow }, { "glow_alpha", &t.glow_alpha } };
		for (auto const &n : nums) if (key == n.k) *n.v = std::atoi(val.c_str());
	}
}

} /* namespace hermes */
