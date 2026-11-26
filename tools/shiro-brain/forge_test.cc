// Host test for hermos_forge. Build: g++ -std=c++17 -I repos/hermos/include tools/shiro-brain/forge_test.cc -o /tmp/forge_test
#include <cstdio>
#include <hermos_forge/forge.h>
using namespace Hermos::Forge;

static std::string block(std::string p, std::string b) { return "<file path=\"" + p + "\">\n" + b + "</file>\n"; }
static std::string MK(std::string n, std::string libs = "base libc") { return "TARGET = " + n + "\nLIBS = " + libs + "\nSRC_CC = main.cc\n"; }
static std::string MAN(std::string n, std::string extra = "nic: none\n  block: none") { return "+ manifest\n name: " + n + "\n kind: rea\n + caps\n  vfs: /apps/" + n + ",ro\n  " + extra + "\n  gui: cell\n + quotas\n  ram: 32M\n  cpu: 25\n"; }
static std::string MAIN = "#include <base/component.h>\nvoid Component::construct(Genode::Env &) { }\n";

static int fails = 0;
static void expect(char const *what, bool got, bool want) { std::printf("%-46s %s\n", what, got == want ? "ok" : "FAIL"); fails += got != want; }

int main()
{
	std::string good = block("src/app/clockcell/main.cc", MAIN) + block("src/app/clockcell/target.mk", MK("clockcell")) + block("src/app/clockcell/manifest.hid", MAN("clockcell"));
	Result r = check(good);
	expect("a complete component passes", r.ok, true);
	expect("its name is read", r.name == "clockcell", true);
	expect("no file blocks: not a proposal", check("just text").any, false);

	auto fails_with = [&](char const *what, std::string text, char const *needle) {
		Result x = check(text); bool found = false;
		for (auto &p : x.problems) if (p.find(needle) != std::string::npos) found = true;
		expect(what, !x.ok && found, true);
	};
	fails_with("path outside src/app is refused", block("etc/passwd", "x") , "outside");
	fails_with("parent directory in a path is refused", block("src/app/a1/../../x/main.cc", "x"), "outside");
	fails_with("missing manifest is refused", block("src/app/clockcell/main.cc", MAIN) + block("src/app/clockcell/target.mk", MK("clockcell")), "manifest.hid is missing");
	fails_with("network request is refused", block("src/app/clockcell/main.cc", MAIN) + block("src/app/clockcell/target.mk", MK("clockcell")) + block("src/app/clockcell/manifest.hid", MAN("clockcell", "nic: uplink\n  block: none")), "network");
	fails_with("block write is refused", block("src/app/clockcell/main.cc", MAIN) + block("src/app/clockcell/target.mk", MK("clockcell")) + block("src/app/clockcell/manifest.hid", MAN("clockcell", "nic: none\n  block: write")), "block");
	fails_with("library not offered is refused", block("src/app/clockcell/main.cc", MAIN) + block("src/app/clockcell/target.mk", MK("clockcell", "base libc curl")) + block("src/app/clockcell/manifest.hid", MAN("clockcell")), "library not allowed: curl");
	fails_with("system() is refused", block("src/app/clockcell/main.cc", MAIN + "void f(){ system(\"ls\"); }\n") + block("src/app/clockcell/target.mk", MK("clockcell")) + block("src/app/clockcell/manifest.hid", MAN("clockcell")), "system(");
	fails_with("the word window is refused", block("src/app/clockcell/main.cc", MAIN + "// open a window\n") + block("src/app/clockcell/target.mk", MK("clockcell")) + block("src/app/clockcell/manifest.hid", MAN("clockcell")), "window");
	fails_with("two components in one proposal are refused", block("src/app/aa/main.cc", MAIN) + block("src/app/bb/target.mk", MK("bb")), "more than one component");
	fails_with("missing entry point is refused", block("src/app/clockcell/main.cc", "int x;\n") + block("src/app/clockcell/target.mk", MK("clockcell")) + block("src/app/clockcell/manifest.hid", MAN("clockcell")), "construct");
	fails_with("manifest of another name is refused", block("src/app/clockcell/main.cc", MAIN) + block("src/app/clockcell/target.mk", MK("clockcell")) + block("src/app/clockcell/manifest.hid", MAN("other")), "manifest name");

	std::printf("\n%s", summary(check(good)).c_str());
	std::printf("\n\n%s\n", summary(check(block("src/app/clockcell/main.cc", MAIN + "// open a window\n"))).c_str());
	std::printf("%s\n", fails ? "FAILED" : "ALL PASSED");
	return fails ? 1 : 0;
}
