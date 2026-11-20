/*
 * Genode 26.08's dynamic linker (ld.lib.so) looks up only the C++ entry
 * `Component::construct(Genode::Env&)` — it no longer falls back to `main`.
 * A Rust binary exports `main` (the compiler-generated C-ABI entry that runs
 * `std::rt::lang_start`), so we provide that mangled symbol here and forward
 * to it.
 */
extern int main(int argc, char **argv);

void _ZN9Component9constructERN6Genode3EnvE(void *env)
{
	(void)env;
	main(0, 0);
}
