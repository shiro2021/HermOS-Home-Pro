/*
 * HermOS framebuffer driver — direct scanout, no Capture, no Nitpicker.
 *
 * Reads the boot framebuffer (UEFI GOP) out of the 'platform_info' ROM, maps
 * it with IO_MEM, and hands that very dataspace to a single framebuffer
 * client. Writes by the client go straight to the screen: there is no copy,
 * no blit queue and no GUI server in between. This is the framebuffer half
 * of the v18 monolithic desktop.
 */

#include <base/attached_io_mem_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/session_object.h>
#include <root/component.h>

#include <framebuffer_session/framebuffer_session.h>
#include <util/xml_node.h>

namespace Hermos {

	using namespace Genode;

	struct Fb_info
	{
		addr_t   addr   { 0 };
		unsigned width  { 0 };
		unsigned height { 0 };
		unsigned bpp    { 0 };
		unsigned pitch  { 0 };
		unsigned type   { 0 };

		static Fb_info from(Node const &node)
		{
			Fb_info r { };
			node.with_optional_sub_node("boot", [&] (Node const &boot) {
				boot.with_optional_sub_node("framebuffer", [&] (Node const &fb) {
					r.addr   = fb.attribute_value("phys",   0UL);
					r.width  = fb.attribute_value("width",  0U);
					r.height = fb.attribute_value("height", 0U);
					r.bpp    = fb.attribute_value("bpp",    0U);
					r.pitch  = fb.attribute_value("pitch",  0U);
					r.type   = fb.attribute_value("type",   0U);
				});
			});
			return r;
		}
	};


	struct Fb_session : Session_object<Framebuffer::Session>
	{
		Framebuffer::Mode const _mode;
		Dataspace_capability    _ds;

		Fb_session(Env &env, Resources const &resources, Label const &label,
		           Dataspace_capability ds, Framebuffer::Mode mode)
		:
			Session_object<Framebuffer::Session>(env.ep(), resources, label),
			_mode(mode), _ds(ds)
		{ }

		Dataspace_capability dataspace() override { return _ds; }
		Framebuffer::Mode mode() const override { return _mode; }

		/* the client writes straight into the scanout buffer */
		void mode_sigh(Signal_context_capability) override { }
		void refresh(Framebuffer::Rect) override { }
		Framebuffer::Session::Blit_result blit(Framebuffer::Blit_batch const &) override
		{ return Framebuffer::Session::Blit_result::OK; }
		void panning(Framebuffer::Point) override { }
		void sync_sigh(Signal_context_capability) override { }
		void sync_source(Session_label const &) override { }
	};


	struct Fb_root : Root_component<Fb_session>
	{
		Env                   &_env;
		Dataspace_capability   _ds;
		Framebuffer::Mode      _mode;

		Fb_root(Env &env, Allocator &alloc, Dataspace_capability ds,
		        Framebuffer::Mode mode)
		:
			Root_component<Fb_session>(env.ep(), alloc),
			_env(env), _ds(ds), _mode(mode)
		{ }

		Create_result _create_session(char const *args) override
		{
			return *new (md_alloc()) Fb_session {
				_env, session_resources_from_args(args),
				session_label_from_args(args), _ds, _mode };
		}
	};


	struct Main
	{
		Env &_env;

		Sliced_heap _heap { _env.ram(), _env.rm() };

		Attached_rom_dataspace _platform_info { _env, "platform_info" };

		Fb_info const _info = Fb_info::from(_platform_info.node());

		Attached_io_mem_dataspace _fb_ds {
			_env, _info.addr, Genode::size_t(_info.pitch) * _info.height };

		Fb_root _root {
			_env, _heap,
			static_cap_cast<Dataspace>(_fb_ds.cap()),
			Framebuffer::Mode { { _info.width, _info.height }, false } };

		Main(Env &env) : _env(env)
		{
			if (_info.bpp != 32 || _info.type != 1) {
				error("hermos_fb: unsupported framebuffer (bpp=", _info.bpp,
				      " type=", _info.type, ")");
				return;
			}

			_env.parent().announce(_env.ep().manage(_root));
			log("hermos_fb: direct scanout ", _info.width, "x", _info.height,
			    " pitch=", _info.pitch, " (no capture, no nitpicker)");
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Hermos::Main main(env);
}
