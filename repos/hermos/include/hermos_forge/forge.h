/*
 * HermOS Forge: the check a REA Cell applies to a component the Shiro Brain proposes.
 *
 * The model writes a component as file blocks:
 *
 *     <file path="src/app/NAME/main.cc"> ... </file>
 *     <file path="src/app/NAME/target.mk"> ... </file>
 *     <file path="src/app/NAME/manifest.hid"> ... </file>
 *
 * check() parses the blocks and decides, by fixed rules (no model involved), whether the component may
 * be kept: where its files live, what they may contain, and what its manifest asks for. A proposal that
 * fails is shown to the user with the reasons and nothing is kept. Standard C++ only, so the same header
 * is used by the agent (libc component) and by the host test.
 */
#ifndef _HERMOS_FORGE_H_
#define _HERMOS_FORGE_H_

#include <string>
#include <vector>
#include <hermos_manifest/manifest.h>

namespace Hermos { namespace Forge {

struct File { std::string path, body; };

struct Result
{
	bool                     any { false };   /* the reply contained at least one file block */
	bool                     ok  { false };   /* and every rule passed */
	std::string              name;
	std::vector<File>        files;
	std::vector<std::string> problems;
};

namespace _d {

	inline bool has(std::string const &s, char const *t) { return s.find(t) != std::string::npos; }

	inline std::string lower(std::string s) { for (auto &c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32); return s; }

	inline bool name_ok(std::string const &n)
	{
		if (n.size() < 2 || n.size() > 32) return false;
		for (char c : n) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
		return true;
	}

	inline bool word_in(std::string const &text, char const *w)   /* whole word, case-insensitive */
	{
		std::string const t = lower(text), k = lower(w);
		for (size_t p = t.find(k); p != std::string::npos; p = t.find(k, p + 1)) {
			bool const l = p == 0 || !((t[p-1] >= 'a' && t[p-1] <= 'z') || (t[p-1] >= '0' && t[p-1] <= '9') || t[p-1] == '_');
			size_t const e = p + k.size();
			bool const r = e >= t.size() || !((t[e] >= 'a' && t[e] <= 'z') || (t[e] >= '0' && t[e] <= '9') || t[e] == '_');
			if (l && r) return true;
		}
		return false;
	}
}

inline Result check(std::string const &reply)
{
	Result r;
	static char const open[] = "<file path=\"";
	size_t pos = 0;

	for (;;) {
		size_t const a = reply.find(open, pos);
		if (a == std::string::npos) break;
		r.any = true;
		size_t const pa = a + sizeof(open) - 1, pe = reply.find('"', pa);
		size_t const ge = pe == std::string::npos ? pe : reply.find('>', pe);
		if (ge == std::string::npos) { r.problems.push_back("a file block is not closed"); break; }
		size_t const be = reply.find("</file>", ge);
		if (be == std::string::npos) { r.problems.push_back("a file block has no end"); break; }
		std::string body = reply.substr(ge + 1, be - ge - 1);
		if (!body.empty() && body[0] == '\n') body.erase(0, 1);
		r.files.push_back({ reply.substr(pa, pe - pa), body });
		pos = be + 7;
	}
	if (!r.any) return r;

	auto bad = [&](std::string const &m) { r.problems.push_back(m); };

	if (r.files.size() > 8) bad("more than 8 files");
	size_t total = 0;
	for (auto const &f : r.files) total += f.body.size();
	if (total > 256 * 1024) bad("more than 256 KiB in total");

	/* where the files live: src/app/NAME/FILE, one NAME, a known FILE */
	std::string name;
	bool have_main = false, have_mk = false, have_man = false;
	std::string main_cc, target_mk, manifest;
	for (auto const &f : r.files) {
		std::string const &p = f.path;
		if (p.compare(0, 8, "src/app/") != 0 || _d::has(p, "..") || p.find("//") != std::string::npos) {
			bad("path outside src/app/NAME/: " + p); continue;
		}
		size_t const sl = p.find('/', 8);
		if (sl == std::string::npos) { bad("path has no file name: " + p); continue; }
		std::string const n = p.substr(8, sl - 8), file = p.substr(sl + 1);
		if (!_d::name_ok(n)) { bad("component name must be 2-32 characters of a-z 0-9 _ : " + n); continue; }
		if (name.empty()) name = n;
		else if (name != n) { bad("files of more than one component"); continue; }
		bool const header = file.size() > 2 && file.compare(file.size() - 2, 2, ".h") == 0 && file.find('/') == std::string::npos;
		if      (file == "main.cc")      { have_main = true; main_cc = f.body; }
		else if (file == "target.mk")    { have_mk = true; target_mk = f.body; }
		else if (file == "manifest.hid") { have_man = true; manifest = f.body; }
		else if (!header)                  bad("file name not allowed: " + file);
		if (f.body.size() > 64 * 1024) bad("file larger than 64 KiB: " + file);
	}
	r.name = name;
	if (!have_main) bad("main.cc is missing");
	if (!have_mk)   bad("target.mk is missing");
	if (!have_man)  bad("manifest.hid is missing");

	/* the manifest: parsed and judged by the Capability Broker's own rules */
	bool wants_net = false;
	if (have_man) {
		Hermos::Manifest m;
		Hermos::parse_manifest(manifest.c_str(), manifest.size(), m);
		char why[128] { };
		int const code = Hermos::validate_manifest(m, why, sizeof(why));
		if (code != 0) bad(std::string("manifest refused by the broker: ") + why);
		if (!name.empty() && name != m.name) bad("manifest name is not the component name");
		if (!m.is_rea) bad("a proposed component must be kind: rea until the user promotes it");
		if (m.nic != Hermos::Manifest::NIC_NONE) { wants_net = true; bad("a proposed component may not ask for the network"); }
		if (m.block == Hermos::Manifest::BLK_WRITE) bad("a proposed component may not write block devices");
	}

	/* target.mk: its own target, and only the libraries the system offers */
	if (have_mk) {
		if (!_d::has(target_mk, ("TARGET = " + name).c_str()) && !_d::has(target_mk, ("TARGET=" + name).c_str()))
			bad("target.mk does not build TARGET = " + name);
		size_t const l = target_mk.find("LIBS");
		if (l != std::string::npos) {
			std::string line = target_mk.substr(l, target_mk.find('\n', l) - l);
			size_t eq = line.find('=');
			std::string rest = eq == std::string::npos ? "" : line.substr(eq + 1);
			size_t i = 0;
			while (i < rest.size()) {
				while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t' || rest[i] == '\r')) ++i;
				size_t j = i;
				while (j < rest.size() && rest[j] != ' ' && rest[j] != '\t' && rest[j] != '\r') ++j;
				std::string const lib = rest.substr(i, j - i);
				if (!lib.empty() && lib != "base" && lib != "libc" && lib != "stdcxx" && lib != "libm")
					bad("library not allowed: " + lib);
				i = j;
			}
		}
		if (_d::has(target_mk, "INC_DIR") && _d::has(target_mk, "..")) bad("target.mk reaches outside its directory");
	}

	/* the source: an entry point, no way out of the sandbox, no windows */
	if (have_main) {
		if (!_d::has(main_cc, "construct(")) bad("main.cc has no Component::construct");
		static char const *const banned[] = { "system(", "popen(", "fork(", "execv", "execl", "dlopen(", "__asm__", "asm volatile", "ptrace(" };
		for (char const *b : banned) if (_d::has(main_cc, b)) bad(std::string("main.cc uses ") + b);
		if (!wants_net && (_d::has(main_cc, "<sys/socket.h>") || _d::has(main_cc, "<netinet/in.h>") || _d::has(main_cc, "socket(")))
			bad("main.cc opens sockets but the manifest asks for no network");
	}
	for (auto const &f : r.files)
		if (_d::word_in(f.body, "window") || _d::word_in(f.body, "windows")) { bad("the word window is not used in HermOS: say Cell (" + f.path + ")"); break; }

	r.ok = r.problems.empty();
	return r;
}

/* the text the user reads in the reply panel */
inline std::string summary(Result const &r)
{
	std::string s;
	if (r.ok) {
		s = "Proposed component '" + r.name + "' (" + std::to_string(r.files.size()) + " files) passed every check:\n";
		for (auto const &f : r.files) s += "  " + f.path + " (" + std::to_string(f.body.size()) + " bytes)\n";
		s += "It is a REA component: no network, no disk write. Nothing is kept until you approve it.";
	} else {
		s = "Proposed component" + (r.name.empty() ? std::string() : " '" + r.name + "'") + " was refused:\n";
		for (auto const &p : r.problems) s += "  - " + p + "\n";
		s += "Nothing was kept.";
	}
	return s;
}

}} /* namespace Hermos::Forge */

#endif /* _HERMOS_FORGE_H_ */
