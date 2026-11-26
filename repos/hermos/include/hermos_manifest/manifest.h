/*
 * HermOS Capability Broker — manifest schema + validation (Phase 2).
 *
 * Self-contained: no libc / no Genode deps (inline string helpers), so the
 * same header compiles inside no-libc Genode components AND in a host test
 * harness. Manifest is the HermOS HID tree (same "+" indentation + "key: value"
 * shape as theme.hid):
 *
 *   + manifest
 *     name: Files
 *     kind: system            # system | rea
 *     + caps
 *       vfs: /apps/files,ro
 *       nic: none             # none | downlink | uplink
 *       block: none           # none | read | write
 *       gui: cell             # cell | none
 *     + quotas
 *       ram: 32M
 *       cpu: 25
 */
#ifndef _HERMOS_MANIFEST_H_
#define _HERMOS_MANIFEST_H_

#include <stddef.h>   /* size_t */

namespace Hermos {

namespace _detail {
	static inline void copy(char *dst, char const *src, size_t n)
	{ for (size_t i = 0; i < n; ++i) dst[i] = src[i]; dst[n] = 0; }

	static inline size_t len(char const *s)
	{ size_t n = 0; while (s[n]) ++n; return n; }

	static inline bool eq(char const *a, char const *b)
	{ while (*a && *b) { if (*a != *b) return false; ++a; ++b; } return *a == *b; }

	static inline bool eqn(char const *a, char const *b, size_t n)
	{ for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return false; return true; }
}

struct Manifest
{
	char name[32] { 0 };

	bool is_rea { false };              /* kind: system(false) | rea(true) */

	enum Nic  { NIC_NONE, NIC_DOWNLINK, NIC_UPLINK } nic { NIC_NONE };
	enum Blk  { BLK_NONE, BLK_READ, BLK_WRITE }   block { BLK_NONE };
	char vfs[64]  { 0 };
	bool gui      { true };

	unsigned ram_mb { 0 };
	unsigned cpu    { 0 };

	static constexpr unsigned MAX_RAM_MB = 256;
	static constexpr unsigned MAX_CPU    = 100;
};

/* parse the HID manifest text. Returns true on a syntactically valid manifest
 * (fills `out`); false on a malformed input. Does NOT apply policy rules — use
 * validate() for that. */
inline bool parse_manifest(char const *buf, size_t len, Manifest &out)
{
	enum Sect { SECT_NONE, SECT_CAPS, SECT_QUOTAS } sect = SECT_NONE;

	char const *p = buf;
	char const *const end = buf + len;

	while (p < end) {
		char const *ls = p;
		while (p < end && *p != '\n') ++p;
		char const *le = p;
		if (p < end) ++p;

		while (ls < le && (*ls == ' ' || *ls == '\t' || *ls == '+')) ++ls;
		while (le > ls && (le[-1] == ' ' || le[-1] == '\t' || le[-1] == '\r')) --le;
		if (ls >= le || *ls == '#') continue;

		char const *colon = nullptr;
		for (char const *q = ls; q < le; ++q)
			if (*q == ':') { colon = q; break; }

		if (!colon) {
			char word[16];
			size_t wl = (size_t)(le - ls);
			size_t cp = wl < 15 ? wl : 15;
			_detail::copy(word, ls, cp);
			if      (_detail::eq(word, "caps"))     sect = SECT_CAPS;
			else if (_detail::eq(word, "quotas"))   sect = SECT_QUOTAS;
			else if (_detail::eq(word, "manifest")) sect = SECT_NONE;
			continue;
		}

		char key[16];
		size_t kl = (size_t)(colon - ls);
		size_t kcp = kl < 15 ? kl : 15;
		_detail::copy(key, ls, kcp);

		char const *vs = colon + 1;
		while (vs < le && (*vs == ' ' || *vs == '\t')) ++vs;
		char const *const ve = le;

		auto val_str = [&](char *dst, size_t cap) {
			size_t l = (size_t)(ve - vs);
			size_t cp = l < cap - 1 ? l : cap - 1;
			_detail::copy(dst, vs, cp);
		};
		auto val_eq = [&](char const *s) {
			size_t l = (size_t)(ve - vs);
			return l == _detail::len(s) && _detail::eqn(vs, s, l);
		};

		if      (_detail::eq(key, "name"))  val_str(out.name, sizeof(out.name));
		else if (_detail::eq(key, "kind"))  out.is_rea = val_eq("rea");
		else if (_detail::eq(key, "vfs")   && sect == SECT_CAPS)   val_str(out.vfs, sizeof(out.vfs));
		else if (_detail::eq(key, "nic")   && sect == SECT_CAPS) {
			if      (val_eq("downlink")) out.nic = Manifest::NIC_DOWNLINK;
			else if (val_eq("uplink"))   out.nic = Manifest::NIC_UPLINK;
			else                         out.nic = Manifest::NIC_NONE;
		}
		else if (_detail::eq(key, "block") && sect == SECT_CAPS) {
			if      (val_eq("read"))  out.block = Manifest::BLK_READ;
			else if (val_eq("write")) out.block = Manifest::BLK_WRITE;
			else                      out.block = Manifest::BLK_NONE;
		}
		else if (_detail::eq(key, "gui")   && sect == SECT_CAPS)   out.gui = !val_eq("none");
		else if (_detail::eq(key, "ram")   && sect == SECT_QUOTAS) {
			unsigned n = 0;
			for (char const *q = vs; q < ve && *q >= '0' && *q <= '9'; ++q) n = n * 10 + (unsigned)(*q - '0');
			out.ram_mb = n;
		}
		else if (_detail::eq(key, "cpu")   && sect == SECT_QUOTAS) {
			unsigned n = 0;
			for (char const *q = vs; q < ve && *q >= '0' && *q <= '9'; ++q) n = n * 10 + (unsigned)(*q - '0');
			out.cpu = n;
		}
	}
	return true;
}

/* Policy validation — the broker's gate. Returns 0 on accept, or a non-zero
 * code with a human-readable `reason` (fits in `reason_cap` bytes). */
inline int validate_manifest(Manifest const &m, char *reason, size_t reason_cap)
{
	auto fail = [&](int code, char const *msg) {
		if (reason && reason_cap) {
			size_t l = _detail::len(msg);
			size_t cp = l < reason_cap - 1 ? l : reason_cap - 1;
			_detail::copy(reason, msg, cp);
		}
		return code;
	};

	if (m.name[0] == 0)          return fail(1, "manifest: missing name");
	if (m.vfs[0] != '/')         return fail(2, "manifest: vfs must be an absolute path");

	/* REA Cells are ephemeral + strictly sandboxed: no uplink, no disk write */
	if (m.is_rea && m.nic == Manifest::NIC_UPLINK)  return fail(3, "manifest: rea cell denied uplink");
	if (m.is_rea && m.block == Manifest::BLK_WRITE) return fail(4, "manifest: rea cell denied block write");

	if (m.ram_mb > Manifest::MAX_RAM_MB) return fail(5, "manifest: ram quota exceeded");
	if (m.cpu    > Manifest::MAX_CPU)    return fail(6, "manifest: cpu quota exceeded");
	if (m.cpu == 0)                      return fail(7, "manifest: cpu quota must be > 0");

	return 0;
}

} /* namespace Hermos */

#endif /* _HERMOS_MANIFEST_H_ */
