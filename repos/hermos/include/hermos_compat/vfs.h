/*
 * Small differences between Genode releases that HermOS code has to bridge.
 *
 * Root_directory(env, alloc, config) exists in every release; the two-argument form only in newer ones.
 * Always passing a (here empty) config node builds on both.
 */
#pragma once
#include <base/node.h>

namespace Hermos {

	static inline Genode::Node const &empty_vfs_config()
	{
		static char const text[] = "<vfs/>";
		static Genode::Node node(Genode::Const_byte_range_ptr(text, sizeof(text) - 1));
		return node;
	}
}
