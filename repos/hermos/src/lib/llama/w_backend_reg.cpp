/*
 * Static backend registry for the Genode build of ggml.
 *
 * Replaces ggml-backend-reg.cpp + ggml-backend-dl.cpp: that code finds and dlopen()s backend libraries
 * with std::filesystem, which Genode's shared libstdc++ does not export, and a HermOS image has exactly one
 * backend (the CPU) built in. The public API is the same; loading from a path is not supported.
 */
#include "ggml-backend-impl.h"
#include "ggml-backend.h"
#include "ggml-impl.h"
#include "ggml-cpu.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

struct Registry
{
	std::vector<ggml_backend_reg_t> backends;
	std::vector<ggml_backend_dev_t> devices;

	Registry() { register_backend(ggml_backend_cpu_reg()); }

	void register_backend(ggml_backend_reg_t reg)
	{
		if (!reg) return;
		for (auto b : backends) if (b == reg) return;
		backends.push_back(reg);
		for (size_t i = 0; i < ggml_backend_reg_dev_count(reg); i++)
			register_device(ggml_backend_reg_dev_get(reg, i));
	}

	void register_device(ggml_backend_dev_t dev)
	{
		for (auto d : devices) if (d == dev) return;
		devices.push_back(dev);
	}
};

Registry &reg() { static Registry r; return r; }

bool striequals(char const *a, char const *b)
{
	for (; *a && *b; a++, b++)
		if (std::tolower(*a) != std::tolower(*b)) return false;
	return *a == *b;
}

} /* namespace */

void ggml_backend_register(ggml_backend_reg_t r)        { reg().register_backend(r); }
void ggml_backend_device_register(ggml_backend_dev_t d) { reg().register_device(d); }

size_t ggml_backend_reg_count() { return reg().backends.size(); }

ggml_backend_reg_t ggml_backend_reg_get(size_t index)
{
	GGML_ASSERT(index < ggml_backend_reg_count());
	return reg().backends[index];
}

ggml_backend_reg_t ggml_backend_reg_by_name(char const *name)
{
	for (size_t i = 0; i < ggml_backend_reg_count(); i++)
		if (striequals(ggml_backend_reg_name(ggml_backend_reg_get(i)), name)) return ggml_backend_reg_get(i);
	return nullptr;
}

size_t ggml_backend_dev_count() { return reg().devices.size(); }

ggml_backend_dev_t ggml_backend_dev_get(size_t index)
{
	GGML_ASSERT(index < ggml_backend_dev_count());
	return reg().devices[index];
}

ggml_backend_dev_t ggml_backend_dev_by_name(char const *name)
{
	for (size_t i = 0; i < ggml_backend_dev_count(); i++)
		if (striequals(ggml_backend_dev_name(ggml_backend_dev_get(i)), name)) return ggml_backend_dev_get(i);
	return nullptr;
}

ggml_backend_dev_t ggml_backend_dev_by_type(enum ggml_backend_dev_type type)
{
	for (size_t i = 0; i < ggml_backend_dev_count(); i++)
		if (ggml_backend_dev_type(ggml_backend_dev_get(i)) == type) return ggml_backend_dev_get(i);
	return nullptr;
}

ggml_backend_t ggml_backend_init_by_name(char const *name, char const *params)
{
	ggml_backend_dev_t dev = ggml_backend_dev_by_name(name);
	return dev ? ggml_backend_dev_init(dev, params) : nullptr;
}

ggml_backend_t ggml_backend_init_by_type(enum ggml_backend_dev_type type, char const *params)
{
	ggml_backend_dev_t dev = ggml_backend_dev_by_type(type);
	return dev ? ggml_backend_dev_init(dev, params) : nullptr;
}

ggml_backend_t ggml_backend_init_best(void)
{
	ggml_backend_dev_t dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
	dev = dev ? dev : ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU);
	dev = dev ? dev : ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
	return dev ? ggml_backend_dev_init(dev, nullptr) : nullptr;
}

/* dynamic loading is not part of this build */
ggml_backend_reg_t ggml_backend_load(char const *) { return nullptr; }

void ggml_backend_unload(ggml_backend_reg_t r)
{
	auto &g = reg();
	g.devices.erase(std::remove_if(g.devices.begin(), g.devices.end(),
	                [r](ggml_backend_dev_t d) { return ggml_backend_dev_backend_reg(d) == r; }), g.devices.end());
	g.backends.erase(std::remove(g.backends.begin(), g.backends.end(), r), g.backends.end());
}

void ggml_backend_load_all() { }
void ggml_backend_load_all_from_path(char const *) { }
