/*
 * HermOS agents_sidebar — a standalone GUI cell component.
 *
 * Step 5a/5b: the isolated sidebar panel. It opens a Gui session, creates a
 * view, sets its buffer mode, fills the framebuffer with the dark translucent
 * panel color and placeholder rows, and idles in the component's event loop.
 * The desktop compositor pins it to the theme's <sidebar> rectangle.
 */

#include <base/component.h>
#include <base/attached_dataspace.h>
#include <base/log.h>
#include <base/signal.h>
#include <gui_session/connection.h>
#include <os/pixel_rgb888.h>
#include <util/reconstructible.h>

namespace Agents_sidebar {

	using namespace Genode;

	struct Main
	{
		Env &_env;

		/* the panel geometry (the right sidebar column) */
		enum { PANEL_W = 240, PANEL_H = 600 };

		Gui::Connection _gui { _env, "agents_sidebar" };

		/* a direct session client for the RPCs the Connection does not
		 * expose (the buffer-mode request) */
		Gui::Session_client _client { _gui.cap() };

		Gui::View_id const _view_id { 1 };

		Constructible<Attached_dataspace> _fb_ds { };

		Signal_handler<Main> _sigh { _env.ep(), *this, &Main::_handle_input };

		void _paint()
		{
			Pixel_rgb888 *pixels = _fb_ds->local_addr<Pixel_rgb888>();

			Pixel_rgb888 const panel  { 19,  29,  42 };   /* the glass body  */
			Pixel_rgb888 const header { 11,  17,  26 };   /* the header band */
			Pixel_rgb888 const row    { 15,  23,  34 };   /* the item rows   */
			Pixel_rgb888 const accent { 255, 140, 40 };   /* the orange edge */

			for (int y = 0; y < PANEL_H; ++y) {
				for (int x = 0; x < PANEL_W; ++x) {

					Pixel_rgb888 c = panel;

					if (x < 4)                                    c = accent;  /* the accent edge */
					if (y < 52)                                   c = header;  /* the header     */
					if (y % 72 >= 52 && x > 16 && x < PANEL_W - 8) c = row;    /* the item rows  */

					pixels[y * PANEL_W + x] = c;
				}
			}

			_gui.framebuffer.refresh(Framebuffer::Rect(
				Framebuffer::Point(0, 0), Framebuffer::Area(PANEL_W, PANEL_H)));
		}

		void _handle_input() { }

		Main(Env &env) : _env(env)
		{
			/* set the buffer mode, then fetch the dataspace sized by it. The Connection's
			 * buffer() first upgrades this session's quota: the server charges the buffer to
			 * the client, so a bare Session_client::buffer() would be refused (OUT_OF_RAM). */
			_gui.buffer({ { PANEL_W, PANEL_H }, false });

			_gui.view(_view_id,
			          { .title = Gui::Title("Agents & Workers"),
			            .rect  = Gui::Rect(Gui::Point(0, 0),
			                               Gui::Area(PANEL_W, PANEL_H)),
			            .front = false });

			_fb_ds.construct(_env.rm(), _gui.framebuffer.dataspace());

			_gui.input.sigh(_sigh);
			_paint();
			log("HermOS agents_sidebar: panel ", (int)PANEL_W, "x",
			    (int)PANEL_H, " ready");
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Agents_sidebar::Main main(env);
}
