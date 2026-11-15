/*
 * HermOS bottom_dock
 *
 * Tracks the pointer's Y coordinate on its own Input session. When the
 * pointer enters the bottom trigger zone, it publishes the dock_state
 * report <dock visible="true" height="H"/>; when it leaves, the report
 * becomes <dock visible="false" height="0"/>. desktop_builder consumes the
 * report as a ROM and shifts every slot up by H while the dock is visible.
 *
 * The height and the trigger zone come from the config (percent of the
 * vertical range 0-32767) with the defaults height=100, trigger=2.
 */

#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <base/signal.h>
#include <input/component.h>
#include <input_session/connection.h>
#include <os/reporter.h>
#include <util/xml_generator.h>

using namespace Genode;

namespace Hermos::Bottom_dock {

	struct Main
	{
		Env &_env;

		Input::Connection    _input    { _env, "bottom_dock" };
		Signal_handler<Main> _input_sigh { _env.ep(), *this, &Main::_handle_input };

		Reporter _dock { _env, "dock_state", "dock_state", 256 };

		Attached_rom_dataspace _config { _env, "config" };

		/* the dock geometry: the height in pixels + the trigger zone as a
		 * percentage of the vertical input range (0-32767) */
		int _height      { 100 };
		int _trigger_pct {   2 };

		bool _visible { false };

		Main(Env &env) : _env(env)
		{
			_apply_config();
			_input.sigh(_input_sigh);
			log("HermOS bottom_dock: READY (height=", _height,
			    ", trigger=", _trigger_pct, "%)");
			_publish();   /* the initial hidden state */
		}

		void _apply_config()
		{
			_config.xml().with_optional_sub_node("dock", [&] (Xml_node const &dock) {
				_height      = (int)dock.attribute_value("height",  100UL);
				_trigger_pct = (int)dock.attribute_value("trigger",   2UL);
			});
		}

		void _publish()
		{
			_dock.enabled(true);
			/* the report carries the state as ROOT attributes:
			 * <dock_state visible="..." height="..."/> */
			(void)_dock.generate_xml([&] (Xml_generator &xml) {
				xml.attribute("visible", _visible);
				xml.attribute("height",  _visible ? _height : 0);
			});
			log("HermOS bottom_dock: dock_state visible=", _visible,
			    " height=", _visible ? _height : 0);
		}

		void _handle_input()
		{
			_input.for_each_event([&] (Input::Event const &ev) {
				ev.handle_absolute_motion([&] (int, int y) {

					/* the bottom trigger zone: the last trigger_pct % of the range */
					int const threshold = 32767 - 32767 * _trigger_pct / 100;
					bool const in_zone = (y >= threshold);

					/* publish only on the transition, not on every motion event */
					if (in_zone != _visible) {
						_visible = in_zone;
						_publish();
					}
				});
			});
		}
	};
}

void Component::construct(Genode::Env &env)
{
	static Hermos::Bottom_dock::Main main(env);
}
