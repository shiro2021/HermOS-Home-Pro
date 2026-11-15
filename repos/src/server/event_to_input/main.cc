/*
 * HermOS input distributor
 *
 * Genode's PC device stack publishes filtered key and pointer events through
 * Event sessions (drivers/event_filter). Nitpicker and GUI clients consume
 * Input sessions. This component is the single place where the two meet.
 *
 * It serves *several* Input clients at once, each with its own event queue,
 * and mirrors every event into all of them:
 *
 *   drivers (usb_hid, ps2) --Event--> distributor --Input--> nitpicker
 *                                                \--Input--> hermes_shell
 *
 * Nitpicker needs the stream to implement its click-to-focus policy; the shell
 * keeps a direct session because it draws its own pointer and text field. Any
 * further client - an application window - is served without changing this
 * component.
 */

#include <base/component.h>
#include <base/heap.h>
#include <base/attached_ram_dataspace.h>
#include <base/rpc_server.h>
#include <base/session_object.h>
#include <event_session/event_session.h>
#include <input/component.h>
#include <input/root.h>
#include <root/component.h>
#include <util/misc_math.h>

using namespace Genode;

namespace Hermos::Event_to_input {

/* US keyboard: map Linux keycodes to ASCII codepoints, honouring Shift.
 * The PC USB-HID stack emits raw keycodes with no codepoint, so the terminal
 * (and any text field) would otherwise never see typed characters. */
static Genode::Codepoint _codepoint_for(Input::Keycode key, bool shift)
{
	switch (key) {
		case Input::KEY_A: return Codepoint { unsigned(shift ? 'A' : 'a') };
		case Input::KEY_B: return Codepoint { unsigned(shift ? 'B' : 'b') };
		case Input::KEY_C: return Codepoint { unsigned(shift ? 'C' : 'c') };
		case Input::KEY_D: return Codepoint { unsigned(shift ? 'D' : 'd') };
		case Input::KEY_E: return Codepoint { unsigned(shift ? 'E' : 'e') };
		case Input::KEY_F: return Codepoint { unsigned(shift ? 'F' : 'f') };
		case Input::KEY_G: return Codepoint { unsigned(shift ? 'G' : 'g') };
		case Input::KEY_H: return Codepoint { unsigned(shift ? 'H' : 'h') };
		case Input::KEY_I: return Codepoint { unsigned(shift ? 'I' : 'i') };
		case Input::KEY_J: return Codepoint { unsigned(shift ? 'J' : 'j') };
		case Input::KEY_K: return Codepoint { unsigned(shift ? 'K' : 'k') };
		case Input::KEY_L: return Codepoint { unsigned(shift ? 'L' : 'l') };
		case Input::KEY_M: return Codepoint { unsigned(shift ? 'M' : 'm') };
		case Input::KEY_N: return Codepoint { unsigned(shift ? 'N' : 'n') };
		case Input::KEY_O: return Codepoint { unsigned(shift ? 'O' : 'o') };
		case Input::KEY_P: return Codepoint { unsigned(shift ? 'P' : 'p') };
		case Input::KEY_Q: return Codepoint { unsigned(shift ? 'Q' : 'q') };
		case Input::KEY_R: return Codepoint { unsigned(shift ? 'R' : 'r') };
		case Input::KEY_S: return Codepoint { unsigned(shift ? 'S' : 's') };
		case Input::KEY_T: return Codepoint { unsigned(shift ? 'T' : 't') };
		case Input::KEY_U: return Codepoint { unsigned(shift ? 'U' : 'u') };
		case Input::KEY_V: return Codepoint { unsigned(shift ? 'V' : 'v') };
		case Input::KEY_W: return Codepoint { unsigned(shift ? 'W' : 'w') };
		case Input::KEY_X: return Codepoint { unsigned(shift ? 'X' : 'x') };
		case Input::KEY_Y: return Codepoint { unsigned(shift ? 'Y' : 'y') };
		case Input::KEY_Z: return Codepoint { unsigned(shift ? 'Z' : 'z') };
		case Input::KEY_0: return Codepoint { unsigned(shift ? ')' : '0') };
		case Input::KEY_1: return Codepoint { unsigned(shift ? '!' : '1') };
		case Input::KEY_2: return Codepoint { unsigned(shift ? '@' : '2') };
		case Input::KEY_3: return Codepoint { unsigned(shift ? '#' : '3') };
		case Input::KEY_4: return Codepoint { unsigned(shift ? '$' : '4') };
		case Input::KEY_5: return Codepoint { unsigned(shift ? '%' : '5') };
		case Input::KEY_6: return Codepoint { unsigned(shift ? '^' : '6') };
		case Input::KEY_7: return Codepoint { unsigned(shift ? '&' : '7') };
		case Input::KEY_8: return Codepoint { unsigned(shift ? '*' : '8') };
		case Input::KEY_9: return Codepoint { unsigned(shift ? '(' : '9') };
		case Input::KEY_MINUS:      return Codepoint { unsigned(shift ? '_' : '-') };
		case Input::KEY_EQUAL:      return Codepoint { unsigned(shift ? '+' : '=') };
		case Input::KEY_LEFTBRACE:  return Codepoint { unsigned(shift ? '{' : '[') };
		case Input::KEY_RIGHTBRACE: return Codepoint { unsigned(shift ? '}' : ']') };
		case Input::KEY_SEMICOLON:  return Codepoint { unsigned(shift ? ':' : ';') };
		case Input::KEY_APOSTROPHE: return Codepoint { unsigned(shift ? '"' : '\'') };
		case Input::KEY_GRAVE:      return Codepoint { unsigned(shift ? '~' : '`') };
		case Input::KEY_BACKSLASH:  return Codepoint { unsigned(shift ? '|' : '\\') };
		case Input::KEY_COMMA:      return Codepoint { unsigned(shift ? '<' : ',') };
		case Input::KEY_DOT:        return Codepoint { unsigned(shift ? '>' : '.') };
		case Input::KEY_SLASH:      return Codepoint { unsigned(shift ? '?' : '/') };
		case Input::KEY_ENTER:     return Codepoint { '\n' };
		case Input::KEY_SPACE:     return Codepoint { ' ' };
		case Input::KEY_TAB:       return Codepoint { '	' };
		case Input::KEY_BACKSPACE: return Codepoint { '\b' };
		default: return Codepoint { Codepoint::INVALID };
	}
}

struct Distributor;

/**
 * One Input client (nitpicker, the shell, or an application)
 */
struct Input_session : Session_object<Input::Session>
{
	Input::Session_component _component;

	Input_session(Env &env, Resources const &resources, Label const &label,
	              Input::Session_component::Action &action)
	:
		Session_object<Input::Session>(env.ep(), resources, label),
		_component(env.ep(), env.ram(), env.rm(), action)
	{
		/* a client that never enabled its queue would silently lose events */
		_component.event_queue().enabled(true);
		log("HermOS input client connected: '", label.string(), "'");
	}

	Dataspace_capability dataspace() override { return _component.dataspace(); }
	bool pending() const override             { return _component.pending(); }
	int  flush() override                     { return _component.flush(); }
	void sigh(Signal_context_capability s) override { _component.sigh(s); }
	void exclusive(bool enabled) override     { _component.exclusive(enabled); }

	void submit(Input::Event const &event) { _component.submit(event); }
};


/**
 * Fan-out of one event stream to every connected Input client
 */
struct Distributor
{
	enum { MAX_CLIENTS = 8 };

	Input_session *_client[MAX_CLIENTS] { };

	unsigned num_clients() const
	{
		unsigned n = 0;
		for (Input_session *c : _client) if (c) n++;
		return n;
	}

	void add(Input_session &session)
	{
		for (Input_session *&c : _client)
			if (!c) { c = &session; return; }

		warning("input distributor is full; client dropped");
	}

	void remove(Input_session &session)
	{
		for (Input_session *&c : _client)
			if (c == &session) { c = nullptr; return; }
	}

	void submit(Input::Event const &event)
	{
		for (Input_session *c : _client)
			if (c) c->submit(event);
	}
};


struct Event_session : Session_object<Event::Session, Event_session>
{
	Attached_ram_dataspace _events;
	Distributor           &_distributor;
	bool _keyboard_seen { false };
	bool _pointer_seen  { false };
	bool _shift         { false };

	Event_session(Env &env, Distributor &distributor)
	:
		Session_object(env.ep(),
		               { .ram_quota = Ram_quota { 128 * 1024 },
		                 .cap_quota = Cap_quota { 8 } },
		               Label()),
		_events(env.ram(), env.rm(), 4096), _distributor(distributor) { }

	Dataspace_capability dataspace() { return _events.cap(); }

	void submit_batch(unsigned count)
	{
		Input::Event const *events = _events.local_addr<Input::Event const>();
		unsigned const capacity = unsigned(_events.size() / sizeof(Input::Event));
		for (unsigned i = 0; i < min(count, capacity); ++i) {

			Input::Event ev = events[i];

			/* track the Shift modifiers before translating */
			ev.handle_press([&] (Input::Keycode key, Input::Codepoint) {
				if (key == Input::KEY_LEFTSHIFT || key == Input::KEY_RIGHTSHIFT)
					_shift = true;
			});
			ev.handle_release([&] (Input::Keycode key) {
				if (key == Input::KEY_LEFTSHIFT || key == Input::KEY_RIGHTSHIFT)
					_shift = false;
			});

			/* fill in the missing codepoint (shift-aware) for keyboard presses */
			ev.handle_press([&] (Input::Keycode key, Input::Codepoint cp) {
				if (!cp.valid())
					ev = Input::Event(Input::Press_char { key, _codepoint_for(key, _shift) });
			});

			ev.handle_press([&] (Input::Keycode key, Input::Codepoint) {
				bool const pointer = key == Input::BTN_LEFT || key == Input::BTN_RIGHT ||
				                     key == Input::BTN_MIDDLE || key == Input::BTN_SIDE ||
				                     key == Input::BTN_EXTRA || key == Input::BTN_TOUCH;
				if (pointer && !_pointer_seen) {
					_pointer_seen = true;
					log("HermOS input bridge received pointer event");
				}
				if (!pointer && !_keyboard_seen) {
					_keyboard_seen = true;
					log("HermOS input bridge received keyboard event");
				}
			});

			ev.handle_release([&] (Input::Keycode key) {
				bool const pointer = key == Input::BTN_LEFT || key == Input::BTN_RIGHT ||
				                     key == Input::BTN_MIDDLE || key == Input::BTN_SIDE ||
				                     key == Input::BTN_EXTRA || key == Input::BTN_TOUCH;
				if (pointer && !_pointer_seen) {
					_pointer_seen = true;
					log("HermOS input bridge received pointer event");
				}
				if (!pointer && !_keyboard_seen) {
					_keyboard_seen = true;
					log("HermOS input bridge received keyboard event");
				}
			});

			ev.handle_absolute_motion([&] (int, int) {
				if (!_pointer_seen) {
					_pointer_seen = true;
					log("HermOS input bridge received pointer event; clients=",
					    _distributor.num_clients());
				}
			});

			_distributor.submit(ev);
		}
	}
};

struct Event_root : Static_root<Event::Session>
{
	Event_root(Event_session &session)
	: Static_root<Event::Session>(session.cap()) { }
};


struct Input_root : Root_component<Input_session>
{
	Env                              &_env;
	Distributor                      &_distributor;
	Input::Session_component::Action &_action;

	Input_root(Env &env, Allocator &alloc, Distributor &distributor,
	           Input::Session_component::Action &action)
	:
		Root_component<Input_session>(env.ep(), alloc), _env(env),
		_distributor(distributor), _action(action) { }

	Create_result _create_session(char const *args) override
	{
		Input_session &session = *new (md_alloc()) Input_session {
			_env, session_resources_from_args(args),
			session_label_from_args(args), _action };

		_distributor.add(session);
		return session;
	}

	void _destroy_session(Input_session &session) override
	{
		_distributor.remove(session);
		Root_component<Input_session>::_destroy_session(session);
	}
};


struct Main : Input::Session_component::Action
{
	Env &_env;

	Distributor _distributor { };

	Sliced_heap _heap { _env.ram(), _env.rm() };

	Input_root    _input_root    { _env, _heap, _distributor, *this };
	Event_session _event_session { _env, _distributor };
	Event_root    _event_root    { _event_session };

	void exclusive_input_requested(bool) override { }

	Main(Env &env) : _env(env)
	{
		_env.parent().announce(_env.ep().manage(_input_root));
		_env.parent().announce(_env.ep().manage(_event_root));
		log("HermOS keyboard and pointer event bridge READY");
	}
};

} /* namespace Hermos::Event_to_input */

void Component::construct(Genode::Env &env)
{
	static Hermos::Event_to_input::Main main(env);
}
