/*
 * Genode 26.08's dynamic linker (ld.lib.so) looks up only the C++ entry
 * `Component::construct(Genode::Env&)`. Provide it and forward to the Rust
 * `main`. Uses the raw base LOG session (via Genode::log) so it does NOT
 * depend on the libc being initialized.
 */
#include <base/log.h>

extern "C" int main(int argc, char **argv);

struct Component {
	void construct(Genode::Env &env);
};

void Component::construct(Genode::Env &env)
{
	(void)env;
	Genode::log("SHIM: Component::construct called");
	main(0, nullptr);
}
