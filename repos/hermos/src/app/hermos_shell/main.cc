#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <input/keycodes.h>
#include <input_session/connection.h>
#include <nitpicker_gfx/box_painter.h>
#include <nitpicker_gfx/text_painter.h>
#include <nitpicker_gfx/tff_font.h>
#include <os/pixel_rgb888.h>
#include <util/string.h>
#include <stdint.h>

#include <hermos_director/session.h>
#include <hermos_ui/window_geometry.h>

using namespace Genode;

extern char _binary_default_tff_start;
extern char _binary_emblem_rgba_start[];
extern char _binary_wallpaper_rgb_start[];

namespace Hermos::Shell {

	class Director_connection : public Connection<Hermos::Session>,
	                            public Rpc_client<Hermos::Session>
	{
	public:
		Director_connection(Env &env)
		:
			Connection<Hermos::Session>(env, "hermos-shell",
			                            Ram_quota { 16 * 1024 }, Cap_quota { 8 },
			                            Affinity(), Connection<Hermos::Session>::Args()),
			Rpc_client<Hermos::Session>(cap())
		{ }

		Dataspace_capability request_dataspace() override
		{ return call<Hermos::Session::Rpc_request_dataspace>(); }

		Dataspace_capability response_dataspace() override
		{ return call<Hermos::Session::Rpc_response_dataspace>(); }

		unsigned health() override { return call<Hermos::Session::Rpc_health>(); }

		unsigned submit(unsigned size) override
		{ return call<Hermos::Session::Rpc_submit>(size); }
	};

	struct Buffer
	{
		Gui::Connection &_gui;
		Gui::Area const area = _gui.panorama().convert<Gui::Area>(
			[&] (Gui::Rect r) { return r.area; },
			[&] (Gui::Undefined) { return Gui::Area { 1024, 768 }; });
		Framebuffer::Mode const mode { .area = { area.w, area.h }, .alpha = false };

		static Dataspace_capability allocate(Gui::Connection &gui, Framebuffer::Mode mode)
		{
			gui.buffer(mode);
			return gui.framebuffer.dataspace();
		}

		Attached_dataspace ds;

		Buffer(Env &env, Gui::Connection &gui)
		: _gui(gui), ds(env.rm(), allocate(gui, mode)) { }
	};

	struct Main
	{
		Env &_env;
		Gui::Connection _gui { _env, "hermos_shell" };
		Input::Connection _input { _env, "hermos_shell" };
		Director_connection _director { _env };
		Buffer _buffer { _env, _gui };
		Gui::Top_level_view _view {
			_gui, Gui::Rect { Gui::Point(0, 0), _buffer.area } };

		Tff_font::Static_glyph_buffer<4096> _glyphs { };
		Tff_font _font { &_binary_default_tff_start, _glyphs };
		Pixel_rgb888 *_pixels { _buffer.ds.local_addr<Pixel_rgb888>() };
		Main(Main const &) = delete;
		Main &operator=(Main const &) = delete;

		Attached_dataspace _request_ds { _env.rm(), _director.request_dataspace() };
		Attached_dataspace _response_ds { _env.rm(), _director.response_dataspace() };

		bool _director_ready { _director.health() == Hermos::READY };
		char _intent[Hermos::GOAL_CAPACITY] { };
		unsigned _intent_size { 0 };
		bool _intent_focused { false };
		bool _pointer_seen { false };
		bool _key_seen { false };
		int _pointer_x { 0 }, _pointer_y { 0 };
		unsigned _selected_nav { 0 };
		int _selected_node { -1 };
		unsigned _workspace { 1 };
		unsigned _active_workers { 0 };
		char _state[48] { "READY" };
		char _plan[Hermos::PLAN_CAPACITY] { "Enter an intent to ask Hermes for a safe proposal." };

		Signal_handler<Main> _input_handler { _env.ep(), *this, &Main::_handle_input };

		static Color _solar() { return Color::rgb(255, 137, 37); }

		void _pixel(int x, int y, Color color)
		{
			if (x < 0 || y < 0 || x >= int(_buffer.area.w) || y >= int(_buffer.area.h)) return;
			_pixels[y * _buffer.area.w + x] = Pixel_rgb888(color.r, color.g, color.b);
		}

		void _line(int x0, int y0, int x1, int y1, Color color, int thickness = 1)
		{
			int const dx = x1 > x0 ? x1 - x0 : x0 - x1;
			int const sx = x0 < x1 ? 1 : -1;
			int const dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
			int const sy = y0 < y1 ? 1 : -1;
			int error = dx + dy;
			for (;;) {
				for (int oy = 0; oy < thickness; ++oy)
					for (int ox = 0; ox < thickness; ++ox)
						_pixel(x0 + ox, y0 + oy, color);
				if (x0 == x1 && y0 == y1) break;
				int const e2 = 2 * error;
				if (e2 >= dy) { error += dy; x0 += sx; }
				if (e2 <= dx) { error += dx; y0 += sy; }
			}
		}

		void _draw_text(Surface<Pixel_rgb888> &surface, int x, int y,
		                Color color, char const *text)
		{
			Text_painter::paint(surface, Text_painter::Position(x, y), _font, color, text);
		}

		void _draw_emblem(int x, int y, int size)
		{
			unsigned char const * const data = reinterpret_cast<unsigned char const *>(
				_binary_emblem_rgba_start);
			for (int py = 0; py < size; ++py)
				for (int px = 0; px < size; ++px) {
					int const sx = px * 80 / size;
					int const sy = py * 80 / size;
					unsigned const at = unsigned((sy * 80 + sx) * 4);
					/*
					 * The supplied emblem is a golden glow on a black
					 * background, stored without transparency. Key the
					 * black out by luminance so the hexagon stays whole
					 * instead of showing a black square.
					 */
					unsigned const lum = (unsigned(data[at]) * 77
					                    + unsigned(data[at + 1]) * 150
					                    + unsigned(data[at + 2]) * 29) >> 8;
					unsigned const a = (unsigned(data[at + 3]) * lum) / 255;
					if (!a) continue;
					Pixel_rgb888 const dst = _pixels[(y + py) * _buffer.area.w + x + px];
					unsigned const r = (data[at] * a + unsigned(dst.r()) * (255 - a)) / 255;
					unsigned const g = (data[at + 1] * a + unsigned(dst.g()) * (255 - a)) / 255;
					unsigned const b = (data[at + 2] * a + unsigned(dst.b()) * (255 - a)) / 255;
					_pixel(x + px, y + py, Color::rgb(uint8_t(r), uint8_t(g), uint8_t(b)));
				}
		}

		Hermos::Ui::Window_geometry _main_geometry() const
		{
			int const w = int(_buffer.area.w * 69 / 100);
			int const h = int(_buffer.area.h * 78 / 100);

			/* corner inset is derived from the width, never passed in */
			return { Point(20, 60), Area(unsigned(w), unsigned(h)) };
		}

		void _draw()
		{
			using namespace Hermos::Ui;
			Area const screen = _buffer.area;
			Surface<Pixel_rgb888> surface(_pixels, screen);

			/* 4:3 crop of the supplied-screen landscape, kept separate from UI. */
			unsigned char const * const wallpaper = reinterpret_cast<unsigned char const *>(
				_binary_wallpaper_rgb_start);
			for (int y = 0; y < int(screen.h); ++y)
				for (int x = 0; x < int(screen.w); ++x) {
					unsigned const sx = unsigned(x) * 1024 / screen.w;
					unsigned const sy = unsigned(y) * 768 / screen.h;
					unsigned const at = (sy * 1024 + sx) * 3;
					_pixel(x, y, Color::rgb(wallpaper[at], wallpaper[at+1], wallpaper[at+2]));
				}

			Color const glass = Color::rgb(19, 29, 42);
			Color const edge = Color::rgb(103, 130, 154);
			Color const muted = Color::rgb(187, 202, 216);
			Color const white = Color::rgb(245, 248, 251);
			auto glass_box=[&](int x,int y,int w,int h,Color tint,unsigned alpha) {
				for(int row=0;row<h;++row) for(int col=0;col<w;++col) {
					int const px=x+col, py=y+row;
					if(px<0||py<0||px>=int(screen.w)||py>=int(screen.h)) continue;
					Pixel_rgb888 const dst=_pixels[py*screen.w+px];
					uint8_t const r=uint8_t((unsigned(dst.r())*(255-alpha)+unsigned(tint.r)*alpha)/255);
					uint8_t const g=uint8_t((unsigned(dst.g())*(255-alpha)+unsigned(tint.g)*alpha)/255);
					uint8_t const b=uint8_t((unsigned(dst.b())*(255-alpha)+unsigned(tint.b)*alpha)/255);
					_pixel(px,py,Color::rgb(r,g,b));
				}
			};

			/* Translucent desktop header. */
			glass_box(10,8,int(screen.w)-20,39,glass,180);
			Box_painter::paint(surface, Rect(Point(10, 8), Area(screen.w-20, 1)), edge);
			_draw_emblem(20, 14, 27);
			_draw_text(surface, 57, 26, white, "HermOS");
			_draw_text(surface, 138, 26, muted, "A More Human Computer");
			int const status_x=int(screen.w)-280;
			/* Search, Wi-Fi, sound, and battery glyphs from the reference header. */
			_line(status_x+8,20,status_x+15,20,muted,1);
			_line(status_x+15,20,status_x+15,26,muted,1);
			_line(status_x+15,26,status_x+8,26,muted,1);
			_line(status_x+8,26,status_x+8,20,muted,1);
			_line(status_x+14,25,status_x+18,29,muted,1);
			_line(status_x+37,22,status_x+42,27,muted,1);
			_line(status_x+42,27,status_x+47,22,muted,1);
			_line(status_x+39,24,status_x+42,27,muted,1);
			_line(status_x+42,27,status_x+45,24,muted,1);
			_line(status_x+70,22,status_x+73,22,muted,2);
			_line(status_x+70,22,status_x+70,28,muted,2);
			_line(status_x+70,28,status_x+74,28,muted,2);
			_line(status_x+74,28,status_x+78,30,muted,1);
			Box_painter::paint(surface,Rect(Point(status_x+100,19),Area(17,12)),muted);
			Box_painter::paint(surface,Rect(Point(status_x+102,21),Area(12,8)),glass);
			Box_painter::paint(surface,Rect(Point(status_x+117,22),Area(2,6)),muted);
			_draw_text(surface,int(screen.w)-152,26,muted,"Mon 26 May 08:24");

			/* Compact navigation rail, matching the reference's left edge. */
			int const nav_x=10, nav_y=88, nav_w=126, nav_h=300;
			glass_box(nav_x,nav_y,nav_w,nav_h,glass,185);
			Box_painter::paint(surface, Rect(Point(nav_x,nav_y),Area(1,nav_h)), edge);
			char const *nav[]={"Home","Workspaces","Applications","Agents","Files","Settings"};
			for (int i=0;i<6;++i) {
				int const y=nav_y+35+i*42;
				Color const icon_color=i?muted:white;
				if (unsigned(i) == _selected_nav)
					glass_box(nav_x+6,y-14,nav_w-12,36,Color::rgb(51,91,122),195);
				int const ix=nav_x+15, iy=y-2;
				if (i==0) { _line(ix,iy,ix+7,iy-7,icon_color,1); _line(ix+7,iy-7,ix+14,iy,icon_color,1); _line(ix+2,iy,ix+2,iy+10,icon_color,1); _line(ix+12,iy,ix+12,iy+10,icon_color,1); _line(ix+2,iy+10,ix+12,iy+10,icon_color,1); }
				if (i==1) { _line(ix,iy-5,ix+10,iy-5,icon_color,1); _line(ix,iy-5,ix,iy+7,icon_color,1); _line(ix,iy+7,ix+10,iy+7,icon_color,1); _line(ix+4,iy-1,ix+14,iy-1,icon_color,1); _line(ix+14,iy-1,ix+14,iy+11,icon_color,1); _line(ix+4,iy+11,ix+14,iy+11,icon_color,1); }
				if (i==2) for(int gy=0;gy<2;++gy) for(int gx=0;gx<2;++gx) Box_painter::paint(surface,Rect(Point(ix+gx*7,iy-5+gy*7),Area(5,5)),icon_color);
				if (i==3) { Box_painter::paint(surface,Rect(Point(ix+5,iy-6),Area(5,5)),icon_color); _line(ix+2,iy+9,ix+2,iy+4,icon_color,1); _line(ix+2,iy+4,ix+13,iy+4,icon_color,1); _line(ix+13,iy+4,ix+13,iy+9,icon_color,1); }
				if (i==4) { _line(ix+1,iy-5,ix+13,iy-5,icon_color,1); _line(ix+1,iy-5,ix+1,iy+10,icon_color,1); _line(ix+13,iy-5,ix+13,iy+10,icon_color,1); _line(ix+1,iy+10,ix+13,iy+10,icon_color,1); }
				if (i==5) { _line(ix+7,iy-7,ix+13,iy-4,icon_color,1); _line(ix+13,iy-4,ix+13,iy+5,icon_color,1); _line(ix+13,iy+5,ix+7,iy+9,icon_color,1); _line(ix+7,iy+9,ix+1,iy+5,icon_color,1); _line(ix+1,iy+5,ix+1,iy-4,icon_color,1); _line(ix+1,iy-4,ix+7,iy-7,icon_color,1); }
				_draw_text(surface,nav_x+36,y+7,i?muted:white,nav[i]);
			}

			/* Central eight-node orbit, positioned from the provided desktop image. */
			int const cx=int(screen.w)*50/100, cy=int(screen.h)*46/100;
			/*
			 * Stroke the outline row by row with the very same inset
			 * function the fill uses, so the frame can never disagree
			 * with the body (and the mid-height vertex stays sharp).
			 */
			auto hex=[&](int x,int y,int w,int h,Color c) {
				int const cut=Hermos::Ui::hex_corner_inset(w);
				for(int row=0; row<h; ++row) {
					int const inset=Hermos::Ui::hex_inset_at(row,w,h);
					for(int t=0;t<2;++t) {
						_pixel(x+inset+t,   y+row, c);
						_pixel(x+w-1-inset-t, y+row, c);
					}
				}
				for(int col=cut; col<w-cut; ++col) {
					for(int t=0;t<2;++t) {
						_pixel(x+col, y+t, c);
						_pixel(x+col, y+h-1-t, c);
					}
				}
			};
			auto fill_hex=[&](int x,int y,int w,int h,Color color) {
				for (int row=0; row<h; ++row) {
					int const inset = Hermos::Ui::hex_inset_at(row, w, h);
					for(int col=inset;col<w-inset;++col) {
						int const px=x+col, py=y+row;
						if(px<0||py<0||px>=int(screen.w)||py>=int(screen.h)) continue;
						Pixel_rgb888 const dst=_pixels[py*screen.w+px];
						uint8_t const r=uint8_t((unsigned(dst.r())*105+unsigned(color.r)*150)/255);
						uint8_t const g=uint8_t((unsigned(dst.g())*105+unsigned(color.g)*150)/255);
						uint8_t const b=uint8_t((unsigned(dst.b())*105+unsigned(color.b)*150)/255);
						_pixel(px,py,Color::rgb(r,g,b));
					}
				}
			};
			int const nx[]={cx-38,cx-146,cx+74,cx-191,cx+122,cx-146,cx+74,cx-38};
			int const ny[]={cy-270,cy-195,cy-195,cy-63,cy-63,cy+73,cy+73,cy+118};
			char const *labels[]={"Research","Creative","Productivity","Web","System","Media","Files","Development"};
			Color const connector=Color::rgb(86,111,132);
			for(int i=0;i<8;++i) _line(cx,cy,nx[i]+38,ny[i]+54,connector,1);
			for(int i=0;i<8;++i) {
				Color const node_colors[]={Color::rgb(41,89,124),Color::rgb(100,61,132),
				                           Color::rgb(131,102,51),Color::rgb(38,79,115),
				                           Color::rgb(52,92,72),Color::rgb(112,57,91),
				                           Color::rgb(137,97,43),Color::rgb(42,79,116)};
				fill_hex(nx[i],ny[i],76,108,node_colors[i]);
				hex(nx[i],ny[i],76,108,i == _selected_node ? _solar() : Color::rgb(136,178,210));
				int const ix=nx[i]+38, iy=ny[i]+34;
				if(i==0) { _line(ix,iy-7,ix,iy+7,white,1); _line(ix,iy-6,ix-10,iy-4,white,1); _line(ix-10,iy-4,ix-10,iy+6,white,1); _line(ix-10,iy+6,ix,iy+8,white,1); _line(ix,iy-6,ix+10,iy-4,white,1); _line(ix+10,iy-4,ix+10,iy+6,white,1); _line(ix+10,iy+6,ix,iy+8,white,1); }
				if(i==1) { _line(ix-7,iy+7,ix+5,iy-6,white,2); _line(ix+5,iy-6,ix+8,iy-2,white,2); _line(ix+8,iy-2,ix-4,iy+9,white,2); }
				if(i==2) { _line(ix-7,iy-8,ix+7,iy-8,white,1); _line(ix-7,iy-8,ix-7,iy+9,white,1); _line(ix+7,iy-8,ix+7,iy+9,white,1); _line(ix-7,iy+9,ix+7,iy+9,white,1); _line(ix-4,iy-3,ix+4,iy-3,white,1); _line(ix-4,iy+1,ix+4,iy+1,white,1); }
				if(i==3) { _line(ix-8,iy-7,ix+8,iy-7,white,1); _line(ix-8,iy-7,ix-8,iy+8,white,1); _line(ix+8,iy-7,ix+8,iy+8,white,1); _line(ix-8,iy+8,ix+8,iy+8,white,1); _line(ix-6,iy,ix+6,iy,white,1); }
				if(i==4) { _line(ix,iy-9,ix+6,iy-2,white,1); _line(ix+6,iy-2,ix+4,iy+6,white,1); _line(ix+4,iy+6,ix-4,iy+6,white,1); _line(ix-4,iy+6,ix-6,iy-2,white,1); _line(ix-6,iy-2,ix,iy-9,white,1); _line(ix-7,iy-1,ix+7,iy-1,white,1); }
				if(i==5) { _line(ix-7,iy-8,ix-7,iy+8,white,1); _line(ix-7,iy-8,ix+8,iy,white,1); _line(ix+8,iy,ix-7,iy+8,white,1); }
				if(i==6) { _line(ix-9,iy-3,ix-9,iy+7,white,1); _line(ix-9,iy-3,ix-2,iy-3,white,1); _line(ix-2,iy-3,ix+1,iy,white,1); _line(ix+1,iy,ix+9,iy,white,1); _line(ix+9,iy,ix+9,iy+7,white,1); _line(ix+9,iy+7,ix-9,iy+7,white,1); }
				if(i==7) { _line(ix-8,iy-6,ix-3,iy,white,2); _line(ix-3,iy,ix-8,iy+6,white,2); _line(ix+8,iy-6,ix+3,iy,white,2); _line(ix+3,iy,ix+8,iy+6,white,2); }
				_draw_text(surface,nx[i]+5,ny[i]+92,white,labels[i]);
			}
			fill_hex(cx-67,cy-100,134,200,Color::rgb(17,38,57));
			hex(cx-67,cy-100,134,200,_solar());
			_draw_emblem(cx-32,cy-71,64);
			_draw_text(surface,cx-29,cy+22,white,"HERMES");
			_draw_text(surface,cx-25,cy+43,muted,"Dirigent");

			/* Right edge worker and Hermes panels. */
			int const sx=int(screen.w)-214, sw=204;
			glass_box(sx,75,sw,270,glass,190);
			Box_painter::paint(surface,Rect(Point(sx,75),Area(sw,1)),edge);
			_draw_text(surface,sx+13,99,white,"Active Agents");
			Genode::String<12> const worker_count(_active_workers);
			_draw_text(surface,sx+165,99,muted,worker_count.string());
			char const *agents[]={"Research Agent","Creative Agent","System Agent"};
			char const *states[]={"Not configured","Not configured","Not configured"};
			for(int i=0;i<3;++i){
				int const y=134+i*75;
				_draw_text(surface,sx+13,y+5,muted,agents[i]);
				_draw_text(surface,sx+13,y+26,muted,states[i]);
				Box_painter::paint(surface,Rect(Point(sx+13,y+38),Area(sw-26,3)),Color::rgb(70,82,95));
				
			}
			glass_box(sx,360,sw,245,glass,190);
			Box_painter::paint(surface,Rect(Point(sx,346),Area(sw,1)),edge);
			_draw_text(surface,sx+13,385,white,"Hermes  ");
			_draw_text(surface,sx+13,423,muted," ");
			glass_box(sx+10,400,sw-20,34,Color::rgb(39,50,63),185);
			Color const field_edge=_intent_focused?_solar():Color::rgb(144,164,181);
			_line(sx+10,400,sx+sw-10,400,field_edge,1);
			_line(sx+10,433,sx+sw-10,433,field_edge,1);
			_line(sx+10,400,sx+10,433,field_edge,1);
			_line(sx+sw-10,400,sx+sw-10,433,field_edge,1);
			_draw_text(surface,sx+17,423,_intent_size?white:muted,_intent_size?_intent:"Ask Hermes...");
			if (_state[0] != 'R' || _intent_size) {
				_draw_text(surface,sx+13,461,_solar(),_state);
				_draw_text(surface,sx+13,489,muted,_plan);
			} else {
				_draw_text(surface,sx+13,461,muted,"Plan a trip to Japan");
				_draw_text(surface,sx+13,489,muted,"Organize my photos");
				_draw_text(surface,sx+13,517,muted,"Summarize this document");
				_draw_text(surface,sx+13,545,muted,"Help me be productive");
				_draw_text(surface,sx+13,573,muted,"Show system status");
			}

			/* Wallpaper picker, centered app dock, and HermOS status tile. */
			int const shelf_y=int(screen.h)*855/1000;
			glass_box(12,shelf_y,258,62,glass,185);
			for(int i=0;i<4;++i) {
				int const tx=20+i*53, ty=shelf_y+5;
				Box_painter::paint(surface,Rect(Point(tx,ty),Area(45,38)),Color::rgb(34,57,77));
				if (unsigned(i+1) == _workspace) {
					Box_painter::paint(surface,Rect(Point(tx,ty),Area(45,1)),_solar());
					Box_painter::paint(surface,Rect(Point(tx,ty+37),Area(45,1)),_solar());
				}
				for(int yy=0; yy<36; ++yy) for(int xx=0; xx<43; ++xx) {
					unsigned const wx=unsigned(xx*20+i*220)%1024, wy=unsigned(yy*17+90+i*143)%768;
					unsigned const at=(wy*1024+wx)*3;
					_pixel(tx+1+xx,ty+1+yy,Color::rgb(wallpaper[at],wallpaper[at+1],wallpaper[at+2]));
				}
				_draw_text(surface,tx+17,ty+49,muted,i==0?"1":(i==1?"2":(i==2?"3":"4")));
			}
			_draw_text(surface,232,shelf_y+40,white,"+");
			glass_box(304,shelf_y,414,62,glass,185);
			for(int i=0;i<9;++i) {
				int const dx=314+i*43;
				glass_box(dx,shelf_y+7,34,48,Color::rgb(uint8_t(44+i*5),uint8_t(83+i*3),uint8_t(131-i*4)),200);
				int const ix=dx+17, iy=shelf_y+30;
				if(i==0) { /* Photos */
					_line(dx+7,iy-8,dx+27,iy-8,white,1); _line(dx+7,iy-8,dx+7,iy+8,white,1);
					_line(dx+27,iy-8,dx+27,iy+8,white,1); _line(dx+7,iy+8,dx+27,iy+8,white,1);
					_line(dx+9,iy+6,dx+15,iy,white,2); _line(dx+15,iy,dx+19,iy+4,white,2);
					_line(dx+19,iy+4,dx+23,iy-2,white,2);
				} else if(i==1) { /* Web */
					_line(ix,iy-10,ix+6,iy-8,white,1); _line(ix+6,iy-8,ix+9,iy-3,white,1);
					_line(ix+9,iy-3,ix+8,iy+5,white,1); _line(ix+8,iy+5,ix+3,iy+10,white,1);
					_line(ix+3,iy+10,ix-4,iy+9,white,1); _line(ix-4,iy+9,ix-9,iy+4,white,1);
					_line(ix-9,iy+4,ix-9,iy-4,white,1); _line(ix-9,iy-4,ix-4,iy-9,white,1);
					_line(ix-4,iy-9,ix,iy-10,white,1); _line(ix-9,iy,ix+9,iy,white,1);
					_line(ix,iy-10,ix-3,iy,white,1); _line(ix-3,iy,ix,iy+10,white,1);
				} else if(i==2) { /* Files */
					_line(dx+7,iy-6,dx+15,iy-6,white,2); _line(dx+15,iy-6,dx+18,iy-2,white,2);
					_line(dx+18,iy-2,dx+27,iy-2,white,2); _line(dx+27,iy-2,dx+27,iy+8,white,2);
					_line(dx+27,iy+8,dx+7,iy+8,white,2); _line(dx+7,iy+8,dx+7,iy-6,white,2);
				} else if(i==3) { /* Mail */
					_line(dx+7,iy-7,dx+27,iy-7,white,1); _line(dx+27,iy-7,dx+27,iy+7,white,1);
					_line(dx+27,iy+7,dx+7,iy+7,white,1); _line(dx+7,iy+7,dx+7,iy-7,white,1);
					_line(dx+7,iy-7,ix,iy+1,white,1); _line(ix,iy+1,dx+27,iy-7,white,1);
				} else if(i==4) { /* Calendar */
					_line(dx+8,iy-8,dx+26,iy-8,white,1); _line(dx+8,iy-8,dx+8,iy+8,white,1);
					_line(dx+26,iy-8,dx+26,iy+8,white,1); _line(dx+8,iy+8,dx+26,iy+8,white,1);
					_draw_text(surface,dx+10,iy+3,white,"26");
				} else if(i==5) { /* Tasks */
					for(int row=0;row<3;++row) { int const yy=iy-7+row*7;
						Box_painter::paint(surface,Rect(Point(dx+6,yy-3),Area(4,4)),white);
						_line(dx+13,yy-1,dx+27,yy-1,white,1); }
				} else if(i==6) { /* Terminal */
					_line(dx+6,iy-8,dx+28,iy-8,white,1); _line(dx+28,iy-8,dx+28,iy+8,white,1);
					_line(dx+28,iy+8,dx+6,iy+8,white,1); _line(dx+6,iy+8,dx+6,iy-8,white,1);
					_line(dx+10,iy-3,dx+14,iy,white,1); _line(dx+14,iy,dx+10,iy+3,white,1);
					_line(dx+17,iy+4,dx+24,iy+4,white,1);
				} else if(i==7) { /* Settings */
					_line(ix,iy-10,ix+5,iy-8,white,1); _line(ix+5,iy-8,ix+8,iy-4,white,1);
					_line(ix+8,iy-4,ix+8,iy+4,white,1); _line(ix+8,iy+4,ix+4,iy+8,white,1);
					_line(ix+4,iy+8,ix-4,iy+8,white,1); _line(ix-4,iy+8,ix-8,iy+4,white,1);
					_line(ix-8,iy+4,ix-8,iy-4,white,1); _line(ix-8,iy-4,ix-4,iy-8,white,1);
					_line(ix-4,iy-8,ix,iy-10,white,1); _line(ix-3,iy,ix+3,iy,white,1);
				} else { /* Add */
					_line(ix,iy-11,ix+9,iy-6,white,1); _line(ix+9,iy-6,ix+9,iy+6,white,1);
					_line(ix+9,iy+6,ix,iy+11,white,1); _line(ix,iy+11,ix-9,iy+6,white,1);
					_line(ix-9,iy+6,ix-9,iy-6,white,1); _line(ix-9,iy-6,ix,iy-11,white,1);
					_line(ix-5,iy,ix+5,iy,white,2); _line(ix,iy-5,ix,iy+5,white,2);
				}
			}
			glass_box(int(screen.w)-180,shelf_y,168,62,glass,185);
			_draw_emblem(int(screen.w)-168,shelf_y+7,32);
			_draw_text(surface,int(screen.w)-132,shelf_y+25,white,"HermOS");
			_draw_text(surface,int(screen.w)-132,shelf_y+47,muted,"A More Human Computer");

			/* Draw a live pointer; otherwise absolute motion is invisible until a click. */
			_line(_pointer_x,_pointer_y,_pointer_x+9,_pointer_y+14,white,2);
			_line(_pointer_x,_pointer_y,_pointer_x,_pointer_y+12,_solar(),1);
			_gui.framebuffer.refresh({ { 0, 0 }, screen });
		}

		void _submit()
		{
			if (!_director_ready || !_intent_size) {
				copy_cstring(_state, _director_ready ? "AWAITING_INPUT" : "UNAVAILABLE",
				             sizeof(_state));
				return;
			}

			Request &request = *_request_ds.local_addr<Request>();
			request = { };
			request.abi_version = ABI_VERSION;
			request.operation = INTENT;
			request.task_id = 1;
			request.goal_size = _intent_size;
			copy_cstring(request.goal, _intent, sizeof(request.goal));

			unsigned const result = _director.submit(sizeof(Request));
			Response const &response = *_response_ds.local_addr<Response>();
			copy_cstring(_state, response.state, sizeof(_state));
			copy_cstring(_plan, response.plan, sizeof(_plan));
			_active_workers = response.workers;
			if (result != response.result)
				copy_cstring(_state, "FAILED", sizeof(_state));
			_draw();
		}

		void _handle_input()
		{
			bool redraw = false;
			auto update_pointer = [&] (int x, int y) {
				int const old_x = _pointer_x, old_y = _pointer_y;
				int const max_x = int(_buffer.area.w) - 1;
				int const max_y = int(_buffer.area.h) - 1;
				_pointer_x = x < 0 ? 0 : (x > max_x ? max_x : x);
				_pointer_y = y < 0 ? 0 : (y > max_y ? max_y : y);
				if (!_pointer_seen) { _pointer_seen = true; log("HermOS shell received pointer motion"); }
				if (_pointer_x != old_x || _pointer_y != old_y) redraw = true;
			};
			_input.for_each_event([&] (Input::Event const &event) {
				event.handle_absolute_motion([&] (int x, int y) {
					int const max_x = int(_buffer.area.w) - 1;
					int const max_y = int(_buffer.area.h) - 1;
					update_pointer(x * max_x / 32767, y * max_y / 32767);
				});
				event.handle_relative_motion([&] (int x, int y) {
					update_pointer(_pointer_x + x, _pointer_y + y);
				});
				event.handle_press([&] (Input::Keycode key, Input::Codepoint cp) {
					if (key == Input::BTN_LEFT) {
						int const left = int(_buffer.area.w) - 204;
						_intent_focused = _pointer_x >= left && _pointer_x <= int(_buffer.area.w) - 10
					               && _pointer_y >= 400 && _pointer_y <= 434;
						if (_intent_focused) log("HermOS intent field focused by pointer");
						if (!_intent_focused && _pointer_x >= 10 && _pointer_x < 136 &&
						    _pointer_y >= 109 && _pointer_y < 360) {
							_selected_nav = unsigned((_pointer_y - 109) / 42);
							if (_selected_nav > 5) _selected_nav = 5;
							char const *pages[]={"HOME","WORKSPACES","APPLICATIONS","AGENTS","FILES","SETTINGS"};
							copy_cstring(_state, pages[_selected_nav], sizeof(_state));
							copy_cstring(_plan, _selected_nav == 0 ? "HermOS home is active." :
							             "This view is not configured in this prototype.", sizeof(_plan));
						}
						int const cx=int(_buffer.area.w)*50/100, cy=int(_buffer.area.h)*46/100;
						int const nx[]={cx-38,cx-146,cx+74,cx-191,cx+122,cx-146,cx+74,cx-38};
						int const ny[]={cy-270,cy-195,cy-195,cy-63,cy-63,cy+73,cy+73,cy+118};
						char const *labels[]={"Research","Creative","Productivity","Web","System","Media","Files","Development"};
						for (int i=0;i<8;++i)
							if (_pointer_x>=nx[i] && _pointer_x<nx[i]+76 && _pointer_y>=ny[i] && _pointer_y<ny[i]+108) {
								_selected_node=i;
								copy_cstring(_state, labels[i], sizeof(_state));
								copy_cstring(_plan, "No application service is configured for this workspace.", sizeof(_plan));
							}
						int const shelf_y=int(_buffer.area.h)*855/1000;
						if (_pointer_y>=shelf_y && _pointer_y<shelf_y+62 && _pointer_x>=20 && _pointer_x<232) {
							_workspace=unsigned((_pointer_x-20)/53)+1;
							if (_workspace>4) _workspace=4;
							char const *spaces[]={"Workspace 1","Workspace 2","Workspace 3","Workspace 4"};
							copy_cstring(_state, spaces[_workspace-1], sizeof(_state));
							copy_cstring(_plan, "Workspace selected.", sizeof(_plan));
						}
						if (_pointer_y>=shelf_y+7 && _pointer_y<shelf_y+55 && _pointer_x>=314 && _pointer_x<701) {
							char const *apps[]={"Photos","Web","Files","Mail","Calendar","Tasks","Terminal","Settings","Applications"};
							unsigned const app=unsigned((_pointer_x-314)/43);
							if (app<9) {
								copy_cstring(_state, apps[app], sizeof(_state));
								copy_cstring(_plan, "This application service is not configured.", sizeof(_plan));
							}
						}
						int const panel_x=int(_buffer.area.w)-214;
						if (_pointer_x>=panel_x+8 && _pointer_x<=panel_x+194 &&
						    _pointer_y>=458 && _pointer_y<=600) {
							char const *prompts[]={"Plan a trip to Japan","Organize my photos","Summarize this document","Help me be more productive","Show system status"};
							unsigned const prompt=unsigned((_pointer_y-458)/29);
							if (prompt<5) {
								copy_cstring(_intent,prompts[prompt],sizeof(_intent));
								_intent_size=unsigned(Genode::strlen(_intent));
								_intent_focused=true;
								_submit();
								redraw=false;
								return;
							}
						}
						redraw = true;
						return;
					}
					if (!_key_seen) {
						_key_seen = true;
						log("HermOS shell received key event; codepoint valid=", cp.valid());
					}
					if (key == Input::KEY_ENTER) { _submit(); redraw = false; return; }
					if (!_intent_focused) return;
					if (key == Input::KEY_BACKSPACE && _intent_size) {
						_intent[--_intent_size] = 0; redraw = true; return;
					}
					if (cp.valid() && cp.value >= 32 && cp.value < 127 &&
					    _intent_size + 1 < sizeof(_intent)) {
						_intent[_intent_size++] = char(cp.value);
						_intent[_intent_size] = 0;
						redraw = true;
					}
				});
			});
			if (redraw) _draw();
		}

		Main(Env &env) : _env(env)
		{
			_pointer_x = int(_buffer.area.w) / 2;
			_pointer_y = int(_buffer.area.h) / 2;
			_view.area(_buffer.area);
			_view.at({ 0, 0 });
			_input.sigh(_input_handler);
			_draw();
			log("HermOS intent UI connected to Hermes; health=",
			    _director_ready ? "READY" : "UNAVAILABLE");
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Hermos::Shell::Main main(env);
}
