#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/connection.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <input_session/connection.h>
#include <nitpicker_gfx/box_painter.h>
#include <nitpicker_gfx/text_painter.h>
#include <nitpicker_gfx/tff_font.h>
#include <os/pixel_rgb888.h>
#include <timer_session/connection.h>
#include <util/string.h>

#include <hermos_installer/session.h>

extern char _binary_default_tff_start;

using namespace Genode;

namespace Hermos::Installer {
	class Connection : public Genode::Connection<Session>,
	                   public Genode::Rpc_client<Session>
	{
	public:
		Connection(Genode::Env &env)
		: Genode::Connection<Session>(env, "installer-ui",
		                             Genode::Ram_quota { 16 * 1024 },
		                             Genode::Cap_quota { 8 }, Genode::Affinity(),
		                             Genode::Connection<Session>::Args()),
		  Genode::Rpc_client<Session>(cap()) { }
		Genode::Dataspace_capability status_dataspace() override
		{ return call<Session::Rpc_status_dataspace>(); }
		unsigned approve(unsigned confirmation) override
		{ return call<Session::Rpc_approve>(confirmation); }
	};

	struct Buffer
	{
		Gui::Connection &gui;
		Gui::Area area = gui.panorama().convert<Gui::Area>(
			[] (Gui::Rect r) { return r.area; },
			[] (Gui::Undefined) { return Gui::Area { 1024, 768 }; });
		Framebuffer::Mode mode { .area = { area.w, area.h }, .alpha = false };
		static Dataspace_capability alloc(Gui::Connection &gui,
		                                  Framebuffer::Mode mode)
		{ gui.buffer(mode); return gui.framebuffer.dataspace(); }
		Genode::Attached_dataspace ds;
		Buffer(Genode::Env &env, Gui::Connection &gui)
		: gui(gui), ds(env.rm(), alloc(gui, mode)) { }
	};

	struct Main
	{
		Genode::Env &_env;
		Gui::Connection _gui { _env, "hermos-installer" };
		Input::Connection _input { _env, "hermos-installer" };
		Timer::Connection _timer { _env };
		Connection _installer { _env };
		Attached_dataspace _status_ds { _env.rm(), _installer.status_dataspace() };
		Status const &_status = *_status_ds.local_addr<Status>();
		Buffer _buffer { _env, _gui };
		Gui::Top_level_view _view { _gui, Gui::Rect { Gui::Point(0,0), _buffer.area } };
		Tff_font::Static_glyph_buffer<4096> _glyphs { };
		Tff_font _font { &_binary_default_tff_start, _glyphs };
		Pixel_rgb888 *_pixels { _buffer.ds.local_addr<Pixel_rgb888>() };
		unsigned _page { 0 };
		int _pointer_x { 0 }, _pointer_y { 0 };
		bool _approved { false };
		Signal_handler<Main> _input_handler { _env.ep(), *this, &Main::_handle_input };
		Signal_handler<Main> _timer_handler { _env.ep(), *this, &Main::_handle_timer };
		Main(Main const &) = delete;
		Main &operator=(Main const &) = delete;

		Main(Genode::Env &env) : _env(env)
		{
			_view.at({ 0, 0 });
			_input.sigh(_input_handler);
			_timer.sigh(_timer_handler);
			_timer.trigger_periodic(250000);
			_draw();
			Genode::log("HermOS graphical installer connected; phase=", _status.phase,
			            " target_bytes=", _status.target_bytes);
		}

		static Color const bg() { return Color::rgb(10,16,26); }
		static Color const panel() { return Color::rgb(24,32,43); }
		static Color const orange() { return Color::rgb(255,139,40); }
		void _text(Surface<Pixel_rgb888> &s, int x, int y, Color c, char const *v)
		{ Text_painter::paint(s, Text_painter::Position(x,y), _font,c,v); }
		void _box(Surface<Pixel_rgb888> &s, int x, int y, unsigned w, unsigned h, Color c)
		{ Box_painter::paint(s, Box_painter::Rect(Point<>(x,y),Area<>(w,h)), c); }
		static void _format_mib(char *out, unsigned value)
		{
			char reverse[16]; unsigned n=0;
			do { reverse[n++]=char('0'+value%10); value/=10; } while (value && n<sizeof(reverse));
			unsigned i=0; while (n) out[i++]=reverse[--n];
			char const suffix[]=" MiB";
			for (unsigned j=0;j<sizeof(suffix);++j) out[i++]=suffix[j];
		}
		void _draw()
		{
			Area const a = _buffer.area;
			Surface<Pixel_rgb888> s(_pixels, a);
			_box(s,0,0,a.w,a.h,bg());
			/* Sunrise bands frame the carbon installer panel. */
			for (unsigned i=0; i<20; ++i) {
				Color const c = Color::rgb(uint8_t(35U+i*5U),uint8_t(42U+i*3U),uint8_t(53U+i*2U));
				_box(s,0,int(i*3),a.w,1,c);
			}
			int const x=int(a.w)*8/100, y=int(a.h)*10/100;
			int const w=int(a.w)*84/100, h=int(a.h)*80/100;
			_box(s,x,y,unsigned(w),unsigned(h),panel());
			_box(s,x,y,unsigned(w),2,orange());
			_text(s,x+28,y+42,Color::rgb(245,247,250),"HermOS Installer");
			_text(s,x+28,y+72,Color::rgb(180,192,205),"ALADAR EDITION  |  GENODE / NOVA");
			/* Six reference stages; current and completed stages glow orange. */
			char const *steps[]={"Welcome","Target","Plan","Review","Installing","Complete"};
			for(unsigned i=0;i<6;++i){
				int const sx=x+28+int(i)*(w-60)/6;
				Color const c=i<=_page?orange():Color::rgb(99,111,124);
				_box(s,sx,y+98,12,12,c);
				_text(s,sx-2,y+130,Color::rgb(205,211,219),steps[i]);
			}
			_text(s,x+32,y+184,Color::rgb(249,249,250),
			      _page==0?"Welcome to HermOS":_page==1?"Installation target":
			      _page==2?"Installation plan":_page==3?"Review and confirm":
			      _page==4?"Installing HermOS":"Installation complete");
			if (_page==0) {
				_text(s,x+32,y+226,Color::rgb(190,199,209),"A more human computer, with Hermes at the center.");
				_text(s,x+32,y+256,Color::rgb(190,199,209),"This guided setup installs the HermOS system image.");
			} else if (_page==1 || _page==2 || _page==3) {
				_box(s,x+28,y+210,unsigned(w-56),154,Color::rgb(15,22,31));
				_text(s,x+48,y+242,Color::rgb(246,247,249),"System-policy storage target");
				_text(s,x+48,y+273,Color::rgb(190,199,209),_status.target);
				_text(s,x+48,y+302,Color::rgb(190,199,209),"Image size: 7.6 MiB  |  Target capacity:");
				/* Capacity is rendered in MiB without floating point. */
				char cap[48];
				_format_mib(cap,unsigned(_status.target_bytes/(1024*1024)));
				_text(s,x+48,y+329,Color::rgb(190,199,209),cap);
				if (_page==3) {
					_text(s,x+48,y+390,orange(),"WARNING: installing erases this target disk.");
					_text(s,x+48,y+419,Color::rgb(230,230,230),"Review the target before confirming installation.");
				}
			} else {
				_text(s,x+32,y+226,Color::rgb(190,199,209),_status.detail);
				unsigned const pct=_status.progress_percent>100?100:_status.progress_percent;
				_box(s,x+32,y+300,unsigned(w-64),12,Color::rgb(65,74,85));
				_box(s,x+32,y+300,unsigned((w-64)*int(pct)/100),12,orange());
				_text(s,x+32,y+338,Color::rgb(239,241,244),"Image transfer and read-back verification");
			}
			if (_page<4) {
				int const bx=x+w-202, by=y+h-64;
				_box(s,bx,by,164,42,orange());
				_text(s,bx+17,by+27,_page==3?Color::rgb(20,20,20):Color::rgb(20,20,20),
				      _page==3?"Install HermOS":"Continue");
			}
			if (_page>=4 && (_status.phase==FAILED || _status.phase==RECOVERY_REQUIRED))
				_text(s,x+32,y+h-50,orange(),"Installation stopped. Keep this screen open for recovery.");
			/* White pointer with the screenshot's solar-orange tip. */
			_box(s,_pointer_x,_pointer_y,3,15,Color::rgb(255,255,255));
			_box(s,_pointer_x,_pointer_y,8,2,orange());
			_gui.framebuffer.refresh(0, 0, int(a.w), int(a.h));
		}
		void _handle_timer()
		{
			_view.front();
			if (_page==4 && _status.phase==COMPLETE) _page=5;
			_draw();
		}
		void _handle_input()
		{
			_input.for_each_event([&] (Input::Event const &e) {
				e.handle_absolute_motion([&] (int x,int y) {
					_pointer_x=x*int(_buffer.area.w-1)/32767;
					_pointer_y=y*int(_buffer.area.h-1)/32767;
					_draw();
				});
				e.handle_press([&] (Input::Keycode key, Input::Codepoint) {
					bool activate=key==Input::KEY_ENTER;
					if (key==Input::BTN_LEFT) {
						int const x=int(_buffer.area.w)*8/100;
						int const w=int(_buffer.area.w)*84/100;
						int const h=int(_buffer.area.h)*80/100;
						int const bx=x+w-202, by=int(_buffer.area.h)*10/100+h-64;
						activate=_pointer_x>=bx&&_pointer_x<bx+164&&_pointer_y>=by&&_pointer_y<by+42;
					}
					if (!activate) return;
					if (_page<3) ++_page;
					else if (_page==3) {
						if (_status.phase==READY && _installer.approve(CONFIRM_ERASE)) {
							_approved=true; _page=4;
						} else Genode::warning("Installer target is unavailable; no disk write was approved");
					}
					_draw();
				});
			});
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Hermos::Installer::Main main(env);
}
