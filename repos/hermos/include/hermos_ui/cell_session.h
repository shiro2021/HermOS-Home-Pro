/*
 * HermOS Cell session — the server-side Gui::Session of the monolithic desktop.
 *
 * One Cell_session = one hosted application Cell. It implements the
 * Genode::Rpc_object<Gui::Session> interface so a stock Genode GUI program
 * (e.g. testnit) can connect, receive a virtual framebuffer + input session,
 * submit a Command_buffer, and have its pixels composited into the desktop.
 *
 * This is deliberately minimal: no view stacking, no domains, no focus, no
 * alpha blending. The client's whole framebuffer is the Cell's texture and is
 * blitted into the Cell's chamfered rectangle by the desktop compositor.
 */

#ifndef _HERMOS_UI__CELL_SESSION_H_
#define _HERMOS_UI__CELL_SESSION_H_

#include <base/attached_ram_dataspace.h>
#include <base/heap.h>
#include <base/session_object.h>
#include <base/signal.h>
#include <root/component.h>

#include <gui_session/gui_session.h>
#include <framebuffer_session/framebuffer_session.h>
#include <input/component.h>
#include <input/event.h>
#include <os/pixel_rgb888.h>
#include <rom_session/rom_session.h>

namespace Hermos {

	using namespace Genode;
	using Pixel = Pixel_rgb888;

	class Cell_session : public Session_object<Gui::Session>
	{
		private:

			using View_id         = Gui::View_id;
			using View_capability = Gui::View_capability;
			using Rect            = Gui::Rect;
			using Point           = Gui::Point;
			using Area            = Gui::Area;
			using Title           = Gui::Title;

			/* framebuffer sub-session served to the client */
			struct Fb_session : Rpc_object<Framebuffer::Session>
			{
				Entrypoint   &_ep;
				Cell_session &_cell;

				Fb_session(Entrypoint &ep, Cell_session &cell)
				: _ep(ep), _cell(cell) { _ep.manage(*this); }
				~Fb_session() { _ep.dissolve(*this); }

				Dataspace_capability dataspace() override { return _cell._fb_dataspace(); }
				Framebuffer::Mode mode() const override   { return _cell._mode; }

				void mode_sigh(Signal_context_capability) override { }
				void refresh(Framebuffer::Rect) override { _cell._notify(); }
				Framebuffer::Session::Blit_result blit(Framebuffer::Blit_batch const &) override
				{ return Framebuffer::Session::Blit_result::OK; }
				void panning(Framebuffer::Point) override { }
				void sync_sigh(Signal_context_capability) override { }
				void sync_source(Session_label const &) override { }
			};

			/* panorama/info ROM served to the client (static, minimal) */
			struct Info_rom : Rpc_object<Rom_session>
			{
				Entrypoint &_ep;
				Attached_ram_dataspace _ds;

				Info_rom(Entrypoint &ep, Env &env, char const *content, size_t len)
				: _ep(ep), _ds(env.ram(), env.rm(), len)
				{
					Genode::memcpy(_ds.local_addr<char>(), content, len);
					_ep.manage(*this);
				}
				~Info_rom() { _ep.dissolve(*this); }

				Rom_dataspace_capability dataspace() override
				{
					Dataspace_capability ds_cap = _ds.cap();
					return static_cap_cast<Rom_dataspace>(ds_cap);
				}

				void sigh(Signal_context_capability) override { }
				bool update() override { return false; }
			};

			/* trivial view-capability object (Gui::View has no RPC methods) */
			struct View_rpc : Rpc_object<Gui::View> { };

			/* no-op input action (exclusive-input policy is not used here) */
			struct No_input_action : Input::Session_component::Action
			{ void exclusive_input_requested(bool) override { } };

			/* view geometry tracking (all views share the one Cell texture) */
			struct View_entry { View_id id { }; Rect rect { }; bool used { false }; };
			enum { MAX_VIEWS = 8 };

			Env        &_env;
			Signal_context_capability _render_sigh;

			Constructible<Attached_ram_dataspace> _fb_ds { };
			size_t           _fb_bytes { 0 };
			Framebuffer::Mode _mode { { 0, 0 }, false };

			Attached_ram_dataspace _command_ds { _env.ram(), _env.rm(),
			                                     sizeof(Command_buffer) };
			Command_buffer &_commands { *_command_ds.local_addr<Command_buffer>() };

			Fb_session _fb { _env.ep(), *this };
			View_rpc   _view_rpc { };
			Info_rom   _info_rom { _env.ep(), _env, _info_xml, sizeof(_info_xml) - 1 };

			No_input_action          _input_action { };
			Input::Session_component _input { _env.ep(), _env.ram(), _env.rm(),
			                                  _input_action };

			Gui::Title  _title { };
			View_entry  _views[MAX_VIEWS] { };

			static constexpr char const _info_xml[] =
				"<info x=\"0\" y=\"0\" w=\"200\" h=\"200\"/>";

			View_entry *_find(View_id id)
			{
				for (View_entry &v : _views)
					if (v.used && v.id == id) return &v;
				return nullptr;
			}

			View_entry *_slot(View_id id)
			{
				if (View_entry *v = _find(id)) return v;
				for (View_entry &v : _views)
					if (!v.used) { v.id = id; v.used = true; return &v; }
				return nullptr;
			}

			void _notify()
			{
				if (_render_sigh.valid())
					Signal_transmitter(_render_sigh).submit();
			}

			Dataspace_capability _fb_dataspace()
			{
				size_t const bytes = Gui::Session::ram_quota(_mode);
				if (bytes == 0)
					return Dataspace_capability();

				if (!_fb_ds.constructed() || _fb_bytes != bytes) {
					_fb_ds.construct(_env.ram(), _env.rm(), bytes);
					_fb_bytes = bytes;
				}
				return _fb_ds->cap();
			}

		public:

			Cell_session(Env &env, Resources const &resources, Label const &label,
			             Signal_context_capability render_sigh)
			:
				Session_object<Gui::Session>(env.ep(), resources, label),
				_env(env), _render_sigh(render_sigh)
			{
				_env.ep().manage(_view_rpc);
				/* enable the input event queue, else submitted events are dropped */
				_input.event_queue().enabled(true);
			}

			~Cell_session() { _env.ep().dissolve(_view_rpc); }

			Session_label label() const { return _label; }

			/* accessors for the desktop compositor */
			bool fb_valid() const { return _fb_ds.constructed() && _mode.area.count() > 0; }
			int  fb_w() const { return int(_mode.area.w); }
			int  fb_h() const { return int(_mode.area.h); }
			Pixel const *fb_pixels() const { return _fb_ds->local_addr<Pixel const>(); }

			/* forward a desktop input event into the hosted app */
			void submit_input(Input::Event const &ev) { _input.submit(ev); }

			/*********************
			 ** Gui::Session RPC **
			 *********************/

			Framebuffer::Session_capability framebuffer() override { return _fb.cap(); }
			Input::Session_capability       input()       override { return _input.cap(); }

			Info_result info() override
			{
				/* static panorama ROM; keeps GUI clients (e.g. the terminal)
				 * from spinning on RAM upgrades when registering info_sigh */
				return _info_rom.cap();
			}

			View_result view(View_id id, View_attr const &attr) override
			{
				View_entry *v = _slot(id);
				if (!v) return View_result::OUT_OF_CAPS;
				v->rect = attr.rect;
				log("HermOS cell: view ", id, " ", attr.rect);
				return View_result::OK;
			}

			Child_view_result child_view(View_id id, View_id, View_attr const &attr) override
			{
				View_entry *v = _slot(id);
				if (!v) return Child_view_result::OUT_OF_CAPS;
				v->rect = attr.rect;
				return Child_view_result::OK;
			}

			void destroy_view(View_id id) override
			{
				if (View_entry *v = _find(id)) v->used = false;
			}

			Associate_result associate(View_id id, View_capability) override
			{
				(void)id;   /* single-cell demo: accept and ignore */
				return Associate_result::OK;
			}

			View_capability_result view_capability(View_id) override
			{ return _view_rpc.cap(); }

			void release_view_id(View_id id) override
			{
				if (View_entry *v = _find(id)) v->used = false;
			}

			Dataspace_capability command_dataspace() override { return _command_ds.cap(); }

			void execute() override
			{
				for (unsigned i = 0; i < _commands.num(); i++) {
					Command const cmd = _commands.get(i);
					switch (cmd.opcode) {
					case Command::GEOMETRY:
						if (View_entry *v = _find(cmd.geometry.view)) {
							v->rect = cmd.geometry.rect;
							log("HermOS cell: geometry ", cmd.geometry.rect);
						}
						break;
					case Command::OFFSET:
						if (View_entry *v = _find(cmd.offset.view)) {
							v->rect = Rect(v->rect.at + cmd.offset.offset, v->rect.area);
							log("HermOS cell: offset ", cmd.offset.offset);
						}
						break;
					case Command::TITLE:
						_title = cmd.title.title;
						break;
					case Command::FRONT:      /* single cell: no-op */
					case Command::BACK:
					case Command::FRONT_OF:
					case Command::BEHIND_OF:
					case Command::BACKGROUND:
					case Command::NOP:
						break;
					}
				}
				_commands.reset();
				_notify();
			}

			Buffer_result buffer(Framebuffer::Mode mode) override
			{
				_mode = mode;
				log("HermOS cell: buffer ", _mode.area.w, "x", _mode.area.h);
				_notify();
				return Buffer_result::OK;
			}

			void focus(Capability<Gui::Session>) override { }
	};


	/* manages multiple hosted Cells and the active/focus state */
	class Cell_manager
	{
		public:
			enum { MAX_CELLS = 6 };

		private:
			Cell_session *_slot[MAX_CELLS] { };
			int _focused { -1 };

		public:
			/* place a session into a Cell; returns its index or -1.  'preferred'
			 * pins a client to a specific slot (the terminal -> hero cell 0);
			 * a non-preferred client starts at slot 1 so cell 0 stays reserved. */
			int attach(Cell_session &s, int preferred = -1)
			{
				if (preferred >= 0 && preferred < MAX_CELLS && !_slot[preferred]) {
					_slot[preferred] = &s;
					if (_focused < 0) _focused = preferred;
					return preferred;
				}
				int const start = 0;
				for (int i = start; i < MAX_CELLS; ++i)
					if (!_slot[i]) {
						_slot[i] = &s;
						if (_focused < 0) _focused = i;
						return i;
					}
				return -1;
			}

			void detach(Cell_session &s)
			{
				for (int i = 0; i < MAX_CELLS; ++i)
					if (_slot[i] == &s) {
						_slot[i] = nullptr;
						if (_focused == i) _focused = -1;
					}
				if (_focused < 0)
					for (int i = 0; i < MAX_CELLS; ++i)
						if (_slot[i]) { _focused = i; break; }
			}

			void focus(int idx)
			{
				if (idx >= 0 && idx < MAX_CELLS && _slot[idx]) _focused = idx;
			}

			Cell_session *at(int idx) const
			{ return (idx >= 0 && idx < MAX_CELLS) ? _slot[idx] : nullptr; }

			Cell_session *focused() const
			{ return _focused >= 0 ? _slot[_focused] : nullptr; }

			int focused_index() const { return _focused; }

			unsigned count() const
			{
				unsigned n = 0;
				for (Cell_session *s : _slot) if (s) n++;
				return n;
			}
	};


	/* simple substring scan (Genode base has no strstr) */
	static bool _contains(char const *haystack, char const *needle)
	{
		for (char const *p = haystack; *p; ++p) {
			char const *a = p, *b = needle;
			while (*a && *b && *a == *b) { ++a; ++b; }
			if (!*b) return true;
		}
		return false;
	}


	/* Root of the "Gui" service; places each client into a Cell slot */
	struct Cell_root : Root_component<Cell_session>
	{
		Cell_root(Cell_root const &)              = delete;
		Cell_root &operator = (Cell_root const &) = delete;

		Env &_env;
		Signal_context_capability _render_sigh;
		Cell_manager _cells { };

		/* Step 5b: the agents_sidebar session — it is NOT a floating Cell;
		 * it is pinned to the theme's <sidebar> rectangle and composited
		 * behind the Cells by the desktop. */
		Cell_session *_sidebar_session { nullptr };

		Cell_root(Env &env, Allocator &alloc, Signal_context_capability sigh)
		:
			Root_component<Cell_session>(env.ep(), alloc),
			_env(env), _render_sigh(sigh)
		{ }

		Create_result _create_session(char const *args) override
		{
			Cell_session &s = *new (md_alloc()) Cell_session {
				_env, session_resources_from_args(args),
				session_label_from_args(args), _render_sigh };

			/* the sidebar pins itself to the theme rectangle */
			{
				Session_label const label = s.label();
				char const *const p = label.string();
				if (_contains(p, "agents_sidebar")) {
					_sidebar_session = &s;
					log("HermOS: agents_sidebar session connected");
					return s;
				}
			}

			/* pin clients to deterministic cells regardless of connect order:
			 * the first terminal -> hero cell 0, a second terminal -> cell 1 */
			int preferred = -1;
			{
				Session_label const label = s.label();
				char const *const p = label.string();
				if      (_contains(p, "terminal2")) preferred = 1;
				else if (_contains(p, "terminal"))  preferred = 0;
			}

			int const idx = _cells.attach(s, preferred);
			if (idx < 0)
				log("HermOS: no free Cell for '", s.label().string(), "'");
			else
				log("HermOS: Cell ", idx, " hosting '", s.label().string(), "'");
			return s;
		}

		void _destroy_session(Cell_session &s) override
		{
			if (&s == _sidebar_session) _sidebar_session = nullptr;
			else                        _cells.detach(s);
			Root_component<Cell_session>::_destroy_session(s);
		}

		Cell_manager &cells() { return _cells; }

		Cell_manager const &cells() const { return _cells; }

		Cell_session *sidebar() { return _sidebar_session; }

		Cell_session const *sidebar() const { return _sidebar_session; }

		/* destroy a Cell's session by slot, releasing framebuffer + sub-sessions */
		void close_cell(int idx)
		{
			Cell_session *s = _cells.at(idx);
			if (!s) return;
			_cells.detach(*s);
			destroy(*s);
			log("HermOS: Cell ", idx, " session destroyed");
		}
	};

} /* namespace Hermos */

#endif /* _HERMOS_UI__CELL_SESSION_H_ */
