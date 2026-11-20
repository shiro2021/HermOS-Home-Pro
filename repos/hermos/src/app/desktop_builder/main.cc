/*
 * HermOS desktop_builder — the v18 "monolithic desktop".
 *
 * Renders the HermOS desktop directly to the framebuffer with NO Nitpicker:
 * wallpaper background, a header with the golden-compass emblem in the corner,
 * a left icon sidebar, wide OCTAGONAL windows with window controls (minimize,
 * resize, close) and cell controls (voice, pause), and a bottom dock.
 *
 * Phase 1b: input. The desktop owns an Input session (fed by event_to_input),
 * draws a live pointer, and hit-tests the interactive Cells: the pause icon
 * suspends the Cell (releases CPU/RAM, keeps state), the mic icon starts a
 * voice command, and each window's close button destroys that Cell.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/child.h>
#include <base/component.h>
#include <base/allocator_avl.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/sleep.h>
#include <os/dynamic_rom_session.h>
#include <framebuffer_session/connection.h>
#include <input_session/connection.h>
#include <block_session/connection.h>
#include <input/event.h>
#include <rtc_session/connection.h>
#include <timer_session/connection.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <nitpicker_gfx/box_painter.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <os/vfs.h>

#include <hermos_ui/window_geometry.h>
#include <hermos_ui/cell_session.h>

#include "scene.h"
#include "font_renderer.h"
#include "png_renderer.h"

extern char _binary_emblem_rgba_start[];
extern char _binary_wallpaper_rgb_start[];

namespace Hermos {

	using namespace Genode;
	using Pixel = Pixel_rgb888;

	/*
	 * Read a whole file from the native VFS into a heap allocation.
	 * Returns true and sets data/size on success.
	 */
	static bool _read_file(Root_directory &vfs, char const *path,
	                       Allocator &alloc, void *&data, size_t &size)
	{
		try {
			size = (size_t)vfs.file_size(path);
			if (size == 0) return false;

			data = alloc.alloc(size);
			if (!data) return false;

			Readonly_file file(vfs, path);
			if (file.read(Byte_range_ptr((char *)data, size)) != size)
				return false;
			return true;
		} catch (...) {
			log("HermOS read failed: ", path);
			return false;
		}
	}

	struct Icon
	{
		char const *name;
		char const *path;
		void       *data { nullptr };
		size_t      size { 0 };
	};

	/* app kinds for the dynamic Cell launcher (spawned via the nested init) */
	enum App_kind { APP_NONE = 0, APP_TESTNIT, APP_TERMINAL, APP_FILES };
	static constexpr int MAX_APPS = 6;

	/*
	 * Step 1: the minimal Child_policy skeleton for spawning theme_engine
	 * as a CHILD of the desktop_builder. It forwards the child's environment
	 * sessions (RAM, PD, CPU, ROM, LOG, ...) to our parent and transfers the
	 * remaining PD quota to the child.
	 *
	 * Step 2: the child's "config" ROM is generated locally — the exact
	 * screen size is handed over as <config width height> root attributes.
	 *
	 * Pattern: repos/os/src/app/shim (the Genode child-relay template).
	 */
	struct Theme_config_producer : Dynamic_rom_session::Xml_producer
	{
		int const _w, _h;

		void produce_xml(Xml_generator &xml) override
		{
			/* the screen-size hand-off: root attributes of <config> */
			xml.attribute("width",  _w);
			xml.attribute("height", _h);

			xml.node("libc", [&] () {
				xml.attribute("stdout", "/dev/log");
				xml.attribute("stderr", "/dev/log");
			});
			xml.node("vfs", [&] () {
				xml.node("dir", [&] () {
					xml.attribute("name", "dev");
					xml.node("log", [&] () { });
				});
				xml.node("dir", [&] () {
					xml.attribute("name", "themes");
					xml.node("dir", [&] () {
						xml.attribute("name", "default");
						xml.node("rom", [&] () {
							xml.attribute("name", "theme.hid");
						});
					});
				});
			});
		}

		Theme_config_producer(int w, int h) : Xml_producer("config"), _w(w), _h(h) { }
	};

	struct Theme_child_policy : Genode::Child_policy
	{
		using Parent_service  = Registered<Genode::Parent_service>;
		using Parent_services = Registry<Parent_service>;

		Genode::Env  &_env;
		Genode::Heap  _heap { _env.ram(), _env.rm() };

		/* Step 2: the screen size handed to the child at spawn time */
		int const _w, _h;

		/* fixed quotas: the theme engine needs only its env + the libc.
		 * (The shim's forward-everything pattern starves a second child.) */
		Cap_quota const _cap_quota { 200 };
		Ram_quota const _ram_quota { 8 * 1024 * 1024 };

		Parent_services _parent_services { };
		Id_space<Parent::Server> _server_ids { };

		/* Step 2: the locally generated config ROM (the screen-size hand-off) */
		Theme_config_producer _config_producer { _w, _h };
		Dynamic_rom_session   _config_session { _env.ep().rpc_ep(), _env.ram(),
		                                        _env.rm(), _config_producer };
		Local_service<Dynamic_rom_session>::Single_session_factory
			_config_factory { _config_session };
		Local_service<Dynamic_rom_session> _config_service { _config_factory };

		Genode::Child _child { _env.rm(), _env.ep().rpc_ep(), *this };

		Service &_matching_service(Service::Name const &name)
		{
			/* populate the session-local parent service registry on demand */
			Service *service_ptr = nullptr;
			_parent_services.for_each([&] (Parent_service &s) {
				if (s.name() == name)
					service_ptr = &s; });

			if (service_ptr)
				return *service_ptr;

			return *new (_heap) Parent_service(_parent_services, _env, name);
		}

		char const *skip_name_prefix(char const *label, size_t const len)
		{
			/* strip the "<child name> -> " prefix */
			static char const prefix[] = "theme_engine -> ";
			size_t const plen = sizeof(prefix) - 1;
			if (len > plen && Genode::strcmp(prefix, label, plen) == 0)
				return label + plen;
			return label;
		}

		Theme_child_policy(Genode::Env &env, int w, int h)
		: _env(env), _w(w), _h(h) { }

		/****************************
		 ** Child-policy interface **
		 ****************************/

		Name name() const override { return "theme_engine"; }

		Binary_name binary_name() const override { return "theme_engine"; }

		Ram_allocator &session_md_ram() override { return _env.ram(); }

		Pd_account            &ref_account()           override { return _env.pd(); }
		Capability<Pd_account> ref_account_cap() const override { return _env.pd_session_cap(); }

		void init(Pd_session &pd, Pd_session_capability pd_cap) override
		{
			pd.ref_account(_env.pd_session_cap());
			ref_account().transfer_quota(pd_cap, _cap_quota);
			ref_account().transfer_quota(pd_cap, _ram_quota);
		}

		void _with_route(Service::Name     const &name,
		                 Session_label     const &label,
		                 With_route::Ft    const &fn,
		                 With_no_route::Ft const &) override
		{
			char const *const stripped = skip_name_prefix(label.string(),
			                                              label.length());

			/* Step 2: the child's "config" ROM is generated locally with the
			 * exact screen size; everything else relays to our parent */
			if (name == "ROM" && Genode::strcmp(stripped, "config") == 0) {
				fn(Route { .service = _config_service,
				           .label   = Session_label("config") });
				return;
			}

			fn(Route { .service = _matching_service(name),
			           .label   = Session_label(stripped) });
		}

		Id_space<Parent::Server> &server_id_space() override { return _server_ids; }
	};

	/*
	 * Step 5b: the agents_sidebar child policy. The same relay pattern as the
	 * theme child, with the explicit quotas ram=32M / caps=200. The child's
	 * Gui session also relays to the parent: the outer init routes it back
	 * to our own Gui service (the self-route added in the run script), where
	 * the Cell_root pins it to the theme's <sidebar> rectangle.
	 */
	struct Sidebar_child_policy : Genode::Child_policy
	{
		using Parent_service  = Registered<Genode::Parent_service>;
		using Parent_services = Registry<Parent_service>;

		Genode::Env  &_env;
		Genode::Heap  _heap { _env.ram(), _env.rm() };

		Cap_quota const _cap_quota { 200 };
		Ram_quota const _ram_quota { 32 * 1024 * 1024 };

		/* the child's Gui session is served LOCALLY by our own Cell_root
		 * (relaying it through the parent init deadlocks the entrypoint) */
		Local_service<Cell_session> _gui_service;

		Parent_services _parent_services { };
		Id_space<Parent::Server> _server_ids { };

		Genode::Child _child { _env.rm(), _env.ep().rpc_ep(), *this };

		Service &_matching_service(Service::Name const &name)
		{
			Service *service_ptr = nullptr;
			_parent_services.for_each([&] (Parent_service &s) {
				if (s.name() == name)
					service_ptr = &s; });

			if (service_ptr)
				return *service_ptr;

			return *new (_heap) Parent_service(_parent_services, _env, name);
		}

		char const *skip_name_prefix(char const *label, size_t const len)
		{
			static char const prefix[] = "agents_sidebar -> ";
			size_t const plen = sizeof(prefix) - 1;
			if (len > plen && Genode::strcmp(prefix, label, plen) == 0)
				return label + plen;
			return label;
		}

		Sidebar_child_policy(Genode::Env &env, Cell_root &gui_root)
		: _env(env), _gui_service(gui_root) { }

		/****************************
		 ** Child-policy interface **
		 ****************************/

		Name name() const override { return "agents_sidebar"; }

		Binary_name binary_name() const override { return "agents_sidebar"; }

		Ram_allocator &session_md_ram() override { return _env.ram(); }

		Pd_account            &ref_account()           override { return _env.pd(); }
		Capability<Pd_account> ref_account_cap() const override { return _env.pd_session_cap(); }

		void init(Pd_session &pd, Pd_session_capability pd_cap) override
		{
			pd.ref_account(_env.pd_session_cap());
			ref_account().transfer_quota(pd_cap, _cap_quota);
			ref_account().transfer_quota(pd_cap, _ram_quota);
		}

		void _with_route(Service::Name     const &name,
		                 Session_label     const &label,
		                 With_route::Ft    const &fn,
		                 With_no_route::Ft const &) override
		{
			char const *const stripped = skip_name_prefix(label.string(),
			                                              label.length());

			/* the Gui session is our own service (no parent round-trip);
			 * everything else relays to the parent */
			if (name == "Gui")
				fn(Route { .service = _gui_service,
				           .label   = Session_label(stripped) });
			else
				fn(Route { .service = _matching_service(name),
				           .label   = Session_label(stripped) });
		}

		Id_space<Parent::Server> &server_id_space() override { return _server_ids; }
	};

	/* ---- ext2 on-disk format (little-endian helpers + constants) ---- */
	namespace Ext2 {
		static void put16(char *p, unsigned v) { p[0] = (char)v; p[1] = (char)(v >> 8); }
		static void put32(char *p, uint32_t v) { p[0]=(char)v; p[1]=(char)(v>>8); p[2]=(char)(v>>16); p[3]=(char)(v>>24); }
		static void put64(char *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (char)(v >> (8*i)); }
		static uint16_t get16(char const *p) { return (uint16_t)((uint8_t)p[0] | ((uint8_t)p[1] << 8)); }
		static uint32_t get32(char const *p) { return (uint32_t)((uint8_t)p[0] | ((uint8_t)p[1] << 8) | ((uint8_t)p[2] << 16) | ((uint8_t)p[3] << 24)); }
		static uint64_t get64(char const *p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= (uint64_t)(uint8_t)p[i] << (8*i); return v; }
		static uint32_t crc32(uint8_t const *buf, size_t len)
		{
			uint32_t crc = 0xFFFFFFFFu;
			for (size_t i = 0; i < len; ++i) {
				crc ^= buf[i];
				for (int j = 0; j < 8; ++j)
					crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
			}
			return ~crc;
		}
		enum : uint32_t {
			BS = 1024, NB = 8192, INO = 1024, INO_SZ = 128,  /* 8 MiB image, 1 KiB blocks */
			MAGIC = 0xEF53, ROOT_INO = 2,
			S_IFDIR = 0x4000, S_IFREG = 0x8000, PERM755 = 0x1ED,
			ITABLE = (INO * INO_SZ + BS - 1) / BS,           /* 128 inode-table blocks */
			FIRST  = 1 + 1 + 1 + 1 + 1 + ITABLE               /* 133 = first data block */
		};
	}

	struct Main
	{
		Main(Main const &)              = delete;
		Main &operator = (Main const &) = delete;

		/* Step 2: the per-slot decoration state. ACTIVE follows the focused
		 * slot; the PAUSED state overrides the border (see Step 3). Declared
		 * early because _win()'s signature takes it. */
		enum Cell_state { CELL_ACTIVE, CELL_INACTIVE, CELL_PAUSED };

		Env &_env;

		Framebuffer::Connection _fb { _env, Framebuffer::Mode { { 1024, 768 }, false } };

		Attached_dataspace _fb_ds { _env.rm(), _fb.dataspace() };

		Pixel *_px { _fb_ds.local_addr<Pixel>() };
		int    _w   { 0 };
		int    _h   { 0 };

		/* modern stb renderers: font + PNG icons loaded from the /assets VFS */
		Genode::Heap  _asset_heap { _env.ram(), _env.rm() };

		/* the native VFS front-end (the <vfs> node in our config) replaces
		 * the libc open/read path. The config is applied in the constructor
		 * body (Node cannot be built from Xml_node directly). */
		Attached_rom_dataspace _config { _env, "config" };
		Root_directory _vfs { _env, _asset_heap };
		void         *_font_data { nullptr };
		size_t        _font_size { 0 };
		FontRenderer _font { };

		/* PNG icons (loaded from /assets) */
		Icon _icons[6] {
			{ "search", "/assets/icon_search.png" },
			{ "mic",    "/assets/icon_mic.png"    },
			{ "folder", "/assets/icon_folder.png" },
			{ "user",   "/assets/icon_user.png"   },
			{ "logo",   "/assets/logo.png"        },
			{ "leaf",   "/assets/icon_leaf.png"   },
		};

		/* Step 4: the cell-icon paths from the theme (the icons node) + the
		 * raw PNG bytes loaded from those paths */
		char _icon_close[64]    { "/assets/icon_close.png" };
		char _icon_minimize[64] { "/assets/icon_minimize.png" };
		char _icon_mic[64]      { "/assets/icon_mic.png" };
		char _icon_pause[64]    { "/assets/icon_pause.png" };

		struct Theme_icon { void *data { nullptr }; size_t size { 0 }; };
		Theme_icon _ti_close { }, _ti_minimize { }, _ti_mic { }, _ti_pause { };

		Icon *_find_icon(char const *name)
		{
			for (Icon &ic : _icons)
				if (Genode::strcmp(ic.name, name) == 0)
					return &ic;
			return nullptr;
		}

		/* the real wallpaper image, loaded from /assets/wallpaper.png and
		 * scaled to the screen at boot. Kept pristine in this buffer so the
		 * glassmorphism blur can sample the untouched wallpaper behind cells. */
		uint8_t *_wall_rgb { nullptr };   /* _w * _h * 3 bytes */

		/* ---- the immediate-mode scene (see scene.h) ---- */
		Scene::Fonts  _sfonts  { };
		Scene::Assets _sassets { };
		Scene::State  _sstate  { };
		Scene::Hits   _shits   { };
		uint32_t     *_scene_px   { nullptr };   /* cached composed frame */
		bool          _scene_ready { false };
		bool          _scene_valid { false };
		unsigned      _scene_hash  { 0 };
		Genode::String<48> _sclock { };
		unsigned long _last_tick_ms { 0 };

		void _draw_icon(char const *name, int cx, int cy)
		{
			/* geometric placeholders — no asset pipeline for complex icons yet */
			if (Genode::strcmp(name, "folder") == 0)
				_fill_rrect(cx - 12, cy - 8, 24, 16, 3, _accent_r, _accent_g, _accent_b);
			else if (Genode::strcmp(name, "leaf") == 0)
				_disc(cx, cy, 10, 80, 200, 100);
			else
				_disc(cx, cy, 9, 120, 140, 160);
		}

		/* input */
		Input::Connection _input { _env, "desktop_builder" };
		Signal_handler<Main> _input_sigh { _env.ep(), *this, &Main::_handle_input };

		/* Gui server (hosted Cells) */
		Signal_handler<Main> _render_sigh { _env.ep(), *this, &Main::_handle_render };
		Sliced_heap          _heap { _env.ram(), _env.rm() };
		Cell_root            _gui_root { _env, _heap, _render_sigh };

		/* installer: Block session to read the target disk's geometry */
		Allocator_avl                   _block_alloc { &_heap };
		Constructible<Block::Connection<>> _block { };
		void _block_signal() { }
		Io_signal_handler<Main> _block_sigh { _env.ep(), *this, &Main::_block_signal };
		bool _block_connected { false };

		/* Connect the Block session lazily (on installer launch). A bare ISO
		   with no disk would hang here at boot — the base-lib maps DENIED to
		   sleep_forever(), not a throw — so connect only when the installer is
		   actually launched. */
		void _connect_block()
		{
			if (_block_connected) return;
			_block_connected = true;
			log("HermOS block: connecting to Block session...");
			try {
				_block.construct(_env, &_block_alloc, 4 * 1024 * 1024);
				_block->sigh(_block_sigh);
				log("HermOS block: connected, reading info...");
				_blk_info = _block->info();
				uint64_t const bytes = (uint64_t)_blk_info.block_size * _blk_info.block_count;
				unsigned const gb = (unsigned)(bytes / (1024ULL * 1024ULL * 1024ULL));
				_disk_size = Genode::String<32>(gb, " GB");
				size_t const bs = _blk_info.block_size;
				uint64_t const bc = _blk_info.block_count;
				uint64_t const gb_sectors  = (1024ull * 1024 * 1024) / bs;
				uint64_t const efi_sectors = (200ull * 1024 * 1024) / bs;
				uint64_t const overhead    = 67 + efi_sectors;
				_max_gb = (bc > overhead) ? (bc - overhead) / gb_sectors : 0;
				if (_max_gb < 1) _max_gb = 1;
				_alloc_gb = _max_gb;
				log("HermOS block: block_size=", bs, " block_count=", bc, " => ", _disk_size.string(),
				    " (max alloc ", (unsigned)_max_gb, " GB)");
			} catch (...) {
				_disk_size = "20 GB";
				log("HermOS block: no Block session available (fallback 20 GB)");
			}
		}

		/* app launcher: report_rom-driven config for the nested "apps" init */
		Reporter _apps_report { _env, "config", "apps", 4096 };

		/* AI engine bridge: ui_intent report -> Rust agent, ai_command ROM <- agent */
		Reporter _ui_intent { _env, "ui_intent", "ui_intent", 4096 };
		Reporter _screen_state { _env, "screen_state", "screen_state", 256 };
		Constructible<Attached_rom_dataspace> _ai_command { };
		Signal_handler<Main> _ai_sigh { _env.ep(), *this, &Main::_handle_ai_command };
		Genode::String<2048> _ai_response { };

		/* animation timer: drives the bottom-panel slide + periodic re-render */
		Timer::Connection     _timer     { _env, "desktop_builder" };
		Signal_handler<Main>  _timer_sigh { _env.ep(), *this, &Main::_handle_timer };

		/* command bar text input */
		bool   _bar_focused { false };
		char   _prompt[256] { 0 };
		size_t _prompt_len  { 0 };

		int  _pointer_x { 0 }, _pointer_y { 0 };
		bool _dirty     { false };
		bool _paused     { false };
		bool _panel_open { true };
	bool _installer_mode { false };   /* boot into the live desktop; installer launched on demand */
	bool _server_mode { false };     /* boot into a minimal server status GUI */
	int  _installer_step { 0 };
	bool _installer_confirm { false };
	Genode::String<32> _disk_size { "20 GB" };
	Genode::String<48> _mykee_key { "MK-V3-9F2C-7A4B" };   /* MyKee key for the AI router */
	unsigned _mykee_seq { 1 };
	/* installer: block-I/O state for the ext2 format + file copy */
	Block::Session::Info _blk_info { };
	enum { INST_IDLE = 0, INST_FORMATTING = 1, INST_COPYING = 2, INST_VERIFYING = 3, INST_DONE = 4 };
	int      _install_phase { INST_IDLE };
	unsigned _install_pct { 0 };
	Attached_rom_dataspace _system_img { _env, "hermos-system.img" };
	/* Step 3: dynamic ext2 extraction state (geometry + staged writer buffers) */
	uint32_t _e2_bs { 0 };
	uint32_t _e2_root_blk { 0 };
	uint32_t _e2_it0 { 0 };
	uint32_t _e2_it_blks { 0 };
	uint32_t _e2_next_ino { 3 };
	uint32_t _e2_next_blk { 0 };
	char    *_e2_itable { nullptr };
	char    *_e2_rootdir { nullptr };
	size_t   _e2_dir_used { 0 };
	char    *_e2_bootdir { nullptr };
	size_t   _e2_boot_used { 0 };
	char    *_e2_grubdir { nullptr };
	size_t   _e2_grub_used { 0 };
	uint64_t _alloc_gb { 19 };   /* user-chosen HermOS data-partition size (GiB) */
	uint64_t _max_gb   { 19 };   /* largest data partition that fits on the disk (GiB) */
	uint64_t _data_off { 0 };   /* byte offset of the HermOS data partition */
		bool _win_open[6] { true, true, true, true, true, true };
		App_kind _app_kind[MAX_APPS] { };

		/* app registry, populated from /apps/registry at startup */
		struct App_entry {
			Genode::String<32> name;
			App_kind           kind { APP_NONE };
			void              *icon_data { nullptr };
			size_t             icon_size { 0 };
		};
		App_entry _registry[MAX_APPS];
		int       _registry_count { 0 };

		/* command bar geometry: HARDCODED (X=212, Y=100, W=600, H=50) */
		int _bar_x()  const { return _w / 2 - _bar_w() / 2; }
		int _bar_y()  const { return 280; }
		int _bar_w()  const { return 600; }
		int _bar_h()  const { return  50; }
		int _bar_cy() const { return _bar_y() + _bar_h() / 2; }

		/* palette */
		static Color _glass() { return Color::rgb(19, 29, 42); }
		static Color _muted() { return Color::rgb(187, 202, 216); }
		static Color _white() { return Color::rgb(245, 248, 251); }
		Color _solar() const { return Color::rgb(_accent_r, _accent_g, _accent_b); }

		void _set(int x, int y, uint8_t r, uint8_t g, uint8_t b)
		{
			if (x < 0 || y < 0 || x >= _w || y >= _h) return;
			_px[y * _w + x] = Pixel(r, g, b);
		}

		/* ---- octagonal geometry (8 sides) ---- */

		void _fill_oct(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
		{
			int const c = Ui::oct_corner_inset(w, h);
			for (int row = 0; row < h; ++row) {
				int const ins = Ui::oct_inset_at(row, h, c);
				for (int col = ins; col < w - ins; ++col)
					_set(x + col, y + row, r, g, b);
			}
		}

		void _fill_oct_glass(int x, int y, int w, int h,
		                     uint8_t tr, uint8_t tg, uint8_t tb, unsigned alpha)
		{
			int const c = Ui::oct_corner_inset(w, h);
			for (int row = 0; row < h; ++row) {
				int const ins = Ui::oct_inset_at(row, h, c);
				for (int col = ins; col < w - ins; ++col) {
					int const px = x + col, py = y + row;
					if (px < 0 || py < 0 || px >= _w || py >= _h) continue;
					Pixel const dst = _px[py * _w + px];
					_set(px, py,
					     uint8_t((unsigned(dst.r()) * (255 - alpha) + unsigned(tr) * alpha) / 255),
					     uint8_t((unsigned(dst.g()) * (255 - alpha) + unsigned(tg) * alpha) / 255),
					     uint8_t((unsigned(dst.b()) * (255 - alpha) + unsigned(tb) * alpha) / 255));
				}
			}
		}

		void _stroke_oct(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
		{
			int const c = Ui::oct_corner_inset(w, h);
			for (int row = 0; row < h; ++row) {
				int const ins = Ui::oct_inset_at(row, h, c);
				for (int t = 0; t < 2; ++t) {
					_set(x + ins + t, y + row, r, g, b);
					_set(x + w - 1 - ins - t, y + row, r, g, b);
				}
			}
			for (int col = c; col < w - c; ++col)
				for (int t = 0; t < 2; ++t) {
					_set(x + col, y + t, r, g, b);
					_set(x + col, y + h - 1 - t, r, g, b);
				}
		}

		void _glass(int x, int y, int w, int h, uint8_t tr, uint8_t tg, uint8_t tb, unsigned alpha)
		{
			for (int row = 0; row < h; ++row)
				for (int col = 0; col < w; ++col) {
					int const px = x + col, py = y + row;
					if (px < 0 || py < 0 || px >= _w || py >= _h) continue;
					Pixel const dst = _px[py * _w + px];
					_set(px, py,
					     uint8_t((unsigned(dst.r()) * (255 - alpha) + unsigned(tr) * alpha) / 255),
					     uint8_t((unsigned(dst.g()) * (255 - alpha) + unsigned(tg) * alpha) / 255),
					     uint8_t((unsigned(dst.b()) * (255 - alpha) + unsigned(tb) * alpha) / 255));
				}
		}

		void _stroke_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
		{
			for (int t = 0; t < 2; ++t) {
				_h_line(x, y + t, w, r, g, b);
				_h_line(x, y + h - 1 - t, w, r, g, b);
				_v_line(x + t, y, h, r, g, b);
				_v_line(x + w - 1 - t, y, h, r, g, b);
			}
		}

		/* ---- rounded-rectangle geometry ---- */

		bool _in_rrect(int x, int y, int w, int h, int r)
		{
			if (x < 0 || y < 0 || x >= w || y >= h) return false;
			int dx = r - x;          if (dx < 0) dx = 0;
			int ex = x - (w - 1 - r); if (ex < 0) ex = 0;
			if (ex > dx) dx = ex;
			int dy = r - y;          if (dy < 0) dy = 0;
			int ey = y - (h - 1 - r); if (ey < 0) ey = 0;
			if (ey > dy) dy = ey;
			return dx*dx + dy*dy <= r*r;
		}

		void _fill_rrect(int x, int y, int w, int h, int r, uint8_t cr, uint8_t cg, uint8_t cb)
		{
			for (int py = 0; py < h; ++py)
				for (int px = 0; px < w; ++px)
					if (_in_rrect(px, py, w, h, r))
						_set(x + px, y + py, cr, cg, cb);
		}

		void _fill_rrect_glass(int x, int y, int w, int h, int r,
		                       uint8_t cr, uint8_t cg, uint8_t cb, unsigned alpha)
		{
			for (int py = 0; py < h; ++py)
				for (int px = 0; px < w; ++px)
					if (_in_rrect(px, py, w, h, r)) {
						int const sx = x + px, sy = y + py;
						if (sx < 0 || sy < 0 || sx >= _w || sy >= _h) continue;
						Pixel const dst = _px[sy * _w + sx];
						_set(sx, sy,
						     uint8_t((unsigned(dst.r())*(255-alpha) + unsigned(cr)*alpha)/255),
						     uint8_t((unsigned(dst.g())*(255-alpha) + unsigned(cg)*alpha)/255),
						     uint8_t((unsigned(dst.b())*(255-alpha) + unsigned(cb)*alpha)/255));
					}
		}

		void _stroke_rrect(int x, int y, int w, int h, int r, uint8_t cr, uint8_t cg, uint8_t cb)
		{
			for (int py = 0; py < h; ++py)
				for (int px = 0; px < w; ++px) {
					if (!_in_rrect(px, py, w, h, r)) continue;
					if (_in_rrect(px - 2, py - 2, w - 4, h - 4, r - 2)) continue;
					_set(x + px, y + py, cr, cg, cb);
				}
		}

		/* ---- concept window shape: chamfered (45 deg) top-left corner,
		 *      rounded 12px corners everywhere else ---- */

		bool _in_win_shape(int x, int y, int w, int h, int c, int r)
		{
			if (x < 0 || y < 0 || x >= w || y >= h) return false;
			if (x + y < c) return false;               /* top-left chamfer   */
			if (x >= w - r && y < r) {                 /* top-right corner   */
				int dx = x - (w - r), dy = r - y;
				if (dx*dx + dy*dy > r*r) return false;
			}
			if (x >= w - r && y >= h - r) {            /* bottom-right corner */
				int dx = x - (w - r), dy = y - (h - r);
				if (dx*dx + dy*dy > r*r) return false;
			}
			if (x < r && y >= h - r) {                 /* bottom-left corner  */
				int dx = r - x, dy = y - (h - r);
				if (dx*dx + dy*dy > r*r) return false;
			}
			return true;
		}

		/* ---- dynamic octagon: the convex-polygon approach. The chamfer is a
		 * percentage of min(w, h), computed live per frame, so the shape
		 * survives arbitrary resizing without degenerating. ---- */

		struct Poly_pt { int x, y; };

		/* min helpers (the toolchain has no <algorithm>) */
		static int _min2(int a, int b) { return a < b ? a : b; }
		static int _min3(int a, int b, int c) { return _min2(_min2(a, b), c); }

		/* 1. The 8 vertices of the chamfered rectangle (clockwise).
		 *    The chamfer is clamped so it can never exceed half the width
		 *    or half the height. */
		static void get_octagon_vertices(int x, int y, int w, int h, int requested_chamfer,
		                                 Poly_pt pts[8])
		{
			int c = _min3(requested_chamfer, w / 2, h / 2);

			pts[0] = { x + c,     y         };  /* top edge, left       */
			pts[1] = { x + w - c, y         };  /* top edge, right      */
			pts[2] = { x + w,     y + c     };  /* right edge, top      */
			pts[3] = { x + w,     y + h - c };  /* right edge, bottom   */
			pts[4] = { x + w - c, y + h     };  /* bottom edge, right   */
			pts[5] = { x + c,     y + h     };  /* bottom edge, left    */
			pts[6] = { x,         y + h - c };  /* left edge, bottom    */
			pts[7] = { x,         y + c     };  /* left edge, top       */
		}

		/* 2. Scanline fill of the octagon (no gaps, fast) */
		void draw_filled_octagon(int x, int y, int w, int h, int chamfer,
		                         Pixel_rgb888 fill_color, Pixel_rgb888 *pixels,
		                         int screen_w, int screen_h)
		{
			Poly_pt vertices[8];
			get_octagon_vertices(x, y, w, h, chamfer, vertices);

			int const min_y = y, max_y = y + h;

			for (int py = min_y; py <= max_y; ++py) {
				int intersections[8];
				int n = 0;

				/* the scanline's intersections with every edge */
				for (int i = 0; i < 8; ++i) {
					Poly_pt p1 = vertices[i];
					Poly_pt p2 = vertices[(i + 1) % 8];

					if ((p1.y <= py && p2.y > py) || (p2.y <= py && p1.y > py)) {
						int const px = p1.x + (py - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);
						intersections[n++] = px;
					}
				}

				/* insertion sort (<= 8 values) */
				for (int i = 1; i < n; ++i) {
					int const v = intersections[i];
					int j = i - 1;
					while (j >= 0 && intersections[j] > v) { intersections[j + 1] = intersections[j]; --j; }
					intersections[j + 1] = v;
				}

				/* fill between the pairs */
				for (int i = 0; i + 1 < n; i += 2) {
					int const x_start = intersections[i];
					int const x_end   = intersections[i + 1];

					for (int px = x_start; px <= x_end; ++px)
						if (px >= 0 && px < screen_w && py >= 0 && py < screen_h)
							pixels[py * screen_w + px] = fill_color;
				}
			}
		}

		/* 3. Hit-testing: point-in-polygon via the even-odd (ray casting) rule */
		bool is_inside_octagon(int px, int py, int x, int y, int w, int h, int chamfer) const
		{
			Poly_pt vertices[8];
			get_octagon_vertices(x, y, w, h, chamfer, vertices);
			bool inside = false;

			for (int i = 0, j = 7; i < 8; j = i++) {
				if (((vertices[i].y > py) != (vertices[j].y > py)) &&
				    (px < (vertices[j].x - vertices[i].x) * (py - vertices[i].y) /
				          (vertices[j].y - vertices[i].y) + vertices[i].x)) {
					inside = !inside;
				}
			}
			return inside;
		}

		void _fill_win_glass(int x, int y, int w, int h,
		                     uint8_t tr, uint8_t tg, uint8_t tb, unsigned alpha)
		{
			for (int py = 0; py < h; ++py)
				for (int px = 0; px < w; ++px)
					if (_in_win_shape(px, py, w, h, 16, 12)) {
						int const sx = x + px, sy = y + py;
						if (sx < 0 || sy < 0 || sx >= _w || sy >= _h) continue;
						Pixel const dst = _px[sy * _w + sx];
						_set(sx, sy,
						     uint8_t((unsigned(dst.r())*(255-alpha) + unsigned(tr)*alpha)/255),
						     uint8_t((unsigned(dst.g())*(255-alpha) + unsigned(tg)*alpha)/255),
						     uint8_t((unsigned(dst.b())*(255-alpha) + unsigned(tb)*alpha)/255));
					}
		}

		/* window border stroke: `th` pixels thick, drawn inside the window edge.
		 * The active cell uses a thick orange border (3px), inactive cells a
		 * dark slate border (2px). */
		void _stroke_win(int x, int y, int w, int h, uint8_t cr, uint8_t cg, uint8_t cb, int th)
		{
			for (int py = 0; py < h; ++py)
				for (int px = 0; px < w; ++px) {
					if (!_in_win_shape(px, py, w, h, 16, 12)) continue;
					bool edge = false;
					for (int d = 1; d <= th; ++d)
						if (!_in_win_shape(px - d, py, w, h, 16, 12) ||
						    !_in_win_shape(px + d, py, w, h, 16, 12) ||
						    !_in_win_shape(px, py - d, w, h, 16, 12) ||
						    !_in_win_shape(px, py + d, w, h, 16, 12)) { edge = true; break; }
					if (edge) _set(x + px, y + py, cr, cg, cb);
				}
		}

		/* glassmorphism: box-blur the pristine wallpaper behind the cell,
		 * then tint it toward the glass colour (real background blur) */
		void _glass_blur_cell(int x, int y, int w, int h,
		                      uint8_t tr, uint8_t tg, uint8_t tb, unsigned alpha)
		{
			if (!_wall_rgb) { _fill_win_glass(x, y, w, h, tr, tg, tb, alpha); return; }

			for (int py = 0; py < h; ++py)
				for (int px = 0; px < w; ++px) {
					if (!_in_win_shape(px, py, w, h, 16, 12)) continue;
					int r = 0, g = 0, b = 0, n = 0;
					/* tight 3x3 box blur of the wallpaper (keeps the glass dark, not milky) */
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx) {
							int const sx = x + px + dx, sy = y + py + dy;
							if (sx < 0 || sy < 0 || sx >= _w || sy >= _h) continue;
							int const i = (sy * _w + sx) * 3;
							r += _wall_rgb[i]; g += _wall_rgb[i + 1]; b += _wall_rgb[i + 2];
							++n;
						}
					if (!n) continue;
					r /= n; g /= n; b /= n;
					r = (r * (255 - alpha) + tr * alpha) / 255;
					g = (g * (255 - alpha) + tg * alpha) / 255;
					b = (b * (255 - alpha) + tb * alpha) / 255;
					_set(x + px, y + py, uint8_t(r), uint8_t(g), uint8_t(b));
				}
		}

		/* ---- glyph primitives ---- */

		void _h_line(int x, int y, int len, uint8_t r, uint8_t g, uint8_t b)
		{ for (int i = 0; i < len; ++i) _set(x + i, y, r, g, b); }

		void _v_line(int x, int y, int len, uint8_t r, uint8_t g, uint8_t b)
		{ for (int i = 0; i < len; ++i) _set(x, y + i, r, g, b); }

		void _disc(int cx, int cy, int rad, uint8_t r, uint8_t g, uint8_t b)
		{
			for (int dy = -rad; dy <= rad; ++dy)
				for (int dx = -rad; dx <= rad; ++dx)
					if (dx * dx + dy * dy <= rad * rad)
						_set(cx + dx, cy + dy, r, g, b);
		}

		void _glyph_close(int cx, int cy)
		{
			for (int i = -4; i <= 4; ++i) {
				_set(cx + i, cy + i, 20, 26, 34);
				_set(cx + i, cy - i, 20, 26, 34);
			}
		}

		void _glyph_min(int cx, int cy)
		{ _h_line(cx - 5, cy, 11, 20, 26, 34); }

		void _glyph_resize(int cx, int cy)
		{
			_h_line(cx - 4, cy - 4, 9, 20, 26, 34);
			_h_line(cx - 4, cy + 4, 9, 20, 26, 34);
			_v_line(cx - 4, cy - 4, 9, 20, 26, 34);
			_v_line(cx + 4, cy - 4, 9, 20, 26, 34);
		}

		/* install = downward arrow into a tray (classic download/install glyph) */
		void _glyph_install(int cx, int cy)
		{
			_v_line(cx, cy - 13, 11, 255, 255, 255);
			for (int i = 0; i <= 6; ++i)
				_h_line(cx - (6 - i), cy - 2 + i, 2 * (6 - i) + 1, 255, 255, 255);
			_h_line(cx - 10, cy + 10, 21, 255, 255, 255);
		}

		/* pause = suspend the Cell (release CPU/RAM, keep state) */
		void _glyph_pause(int cx, int cy)
		{
			_v_line(cx - 4, cy - 4, 9, 245, 248, 251);
			_v_line(cx + 2, cy - 4, 9, 245, 248, 251);
		}

		/* microphone = voice commands */
		void _glyph_mic(int cx, int cy)
		{
			_v_line(cx - 1, cy - 4, 4, 245, 248, 251);
			_v_line(cx,     cy - 4, 4, 245, 248, 251);
			_v_line(cx + 1, cy - 4, 4, 245, 248, 251);
			_disc(cx, cy - 4, 2, 245, 248, 251);
			_v_line(cx, cy, 3, 245, 248, 251);
			_h_line(cx - 4, cy + 3, 9, 245, 248, 251);
		}

		void _line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
		{
			int const dx = x1 > x0 ? x1 - x0 : x0 - x1;
			int const dy = y1 > y0 ? y1 - y0 : y0 - y1;
			int const sx = x0 < x1 ? 1 : -1;
			int const sy = y0 < y1 ? 1 : -1;
			int err = dx - dy;
			for (;;) {
				_set(x0, y0, r, g, b);
				if (x0 == x1 && y0 == y1) break;
				int const e2 = 2 * err;
				if (e2 > -dy) { err -= dy; x0 += sx; }
				if (e2 <  dx) { err += dx; y0 += sy; }
			}
		}

		/* soft outward glow drawn behind a cell */
		void _glow_oct(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
		{
			for (int i = 14; i >= 2; i -= 2) {
				unsigned const alpha = unsigned(16 + 68 * (15 - i) / 14);
				_fill_oct_glass(x - i, y - i, w + 2 * i, h + 2 * i, r, g, b, alpha);
			}
		}

		/* icon pinned OUTSIDE the cell, on its left or right side */
		void _icon_pin(int cx, int cy, bool mic)
		{
			_disc(cx, cy, 9, 52, 70, 90);
			if (mic) _draw_icon("mic", cx, cy); else _glyph_pause(cx, cy);
		}

		void _ring(int cx, int cy, int r, uint8_t r_, uint8_t g_, uint8_t b_)
		{
			int const rr = r * r, r1 = (r - 1) * (r - 1);
			for (int dy = -r; dy <= r; ++dy)
				for (int dx = -r; dx <= r; ++dx) {
					int const d = dx * dx + dy * dy;
					if (d >= r1 && d <= rr) _set(cx + dx, cy + dy, r_, g_, b_);
				}
		}

		void _glyph_mag(int cx, int cy)
		{
			_ring(cx - 2, cy - 2, 5, 245, 248, 251);
			_line(cx + 2, cy + 2, cx + 7, cy + 7, 245, 248, 251);
		}

		void _glyph_chev(int cx, int cy)
		{
			_line(cx - 2, cy - 5, cx + 3, cy, 255, 160, 70);
			_line(cx + 3, cy, cx - 2, cy + 5, 255, 160, 70);
		}

		void _glyph_chev_left(int cx, int cy)
		{
			_line(cx + 2, cy - 5, cx - 3, cy, 255, 160, 70);
			_line(cx - 3, cy, cx + 2, cy + 5, 255, 160, 70);
		}

		/* right-pointing triangle */
		void _glyph_play(int cx, int cy)
		{
			for (int y = -4; y <= 4; ++y) {
				int const lx = (y <= 0) ? (cx - 4 + 2 * (y + 4)) : (cx - 4 + 2 * (4 - y));
				_h_line(lx, cy + y, (cx + 4) - lx + 1, 245, 248, 251);
			}
		}

		void _glyph_ellipsis(int cx, int cy)
		{
			_disc(cx - 6, cy, 1, 245, 248, 251);
			_disc(cx,     cy, 1, 245, 248, 251);
			_disc(cx + 6, cy, 1, 245, 248, 251);
		}

		void _badge(int cx, int cy)
		{
			_disc(cx, cy, 6, 255, 160, 70);
			_disc(cx, cy, 1, 255, 255, 255);
		}

		void _glyph_gear(int cx, int cy)
		{
			_ring(cx, cy, 5, 245, 248, 251);
			_disc(cx, cy, 2, 245, 248, 251);
		}

		void _glyph_folder(int cx, int cy)
		{
			_h_line(cx - 6, cy - 5, 5, 245, 248, 251);
			_h_line(cx - 6, cy - 2, 12, 245, 248, 251);
			_h_line(cx - 6, cy + 4, 12, 245, 248, 251);
			_v_line(cx - 6, cy - 5, 10, 245, 248, 251);
			_v_line(cx + 5, cy - 2, 7, 245, 248, 251);
		}

		void _glyph_globe(int cx, int cy)
		{
			_ring(cx, cy, 6, 245, 248, 251);
			_h_line(cx - 5, cy, 11, 245, 248, 251);
			_v_line(cx, cy - 6, 13, 245, 248, 251);
		}

		void _glyph_monitor(int cx, int cy)
		{
			_h_line(cx - 6, cy - 5, 13, 245, 248, 251);
			_h_line(cx - 6, cy + 5, 13, 245, 248, 251);
			_v_line(cx - 6, cy - 5, 11, 245, 248, 251);
			_v_line(cx + 6, cy - 5, 11, 245, 248, 251);
			_v_line(cx, cy + 6, 3, 245, 248, 251);
			_h_line(cx - 3, cy + 9, 7, 245, 248, 251);
		}

		void _glyph_flame(int cx, int cy)
		{
			_disc(cx, cy - 2, 3, 255, 160, 70);
			_disc(cx, cy + 1, 4, 255, 160, 70);
			_disc(cx, cy + 4, 3, 255, 160, 70);
		}

		void _glyph_check(int cx, int cy)
		{
			_line(cx - 5, cy, cx - 1, cy + 4, 245, 248, 251);
			_line(cx - 1, cy + 4, cx + 5, cy - 4, 245, 248, 251);
		}

		void _glyph_plus(int cx, int cy)
		{
			_h_line(cx - 5, cy, 11, 245, 248, 251);
			_v_line(cx, cy - 5, 11, 245, 248, 251);
		}

		/* a floating window: the dynamic octagon fill + border (from the theme),
		 * plus the header bar with the title and window controls */
		void _win(Surface<Pixel> &s, int x, int y, int w, int h, int chamfer,
		          char const *title, Cell_state state)
		{
			/* Step 3: the border color + thickness from the slot's CellState.
			 * All colors arrive via the theme_state report. */
			Pixel_rgb888 border;
			int t;
			switch (state) {
			case CELL_PAUSED:
				border = Pixel_rgb888(_paused_r, _paused_g, _paused_b);
				t = 5;
				break;
			case CELL_ACTIVE:
				border = Pixel_rgb888(_active_r, _active_g, _active_b);
				t = 5;
				break;
			case CELL_INACTIVE:
			default:
				border = Pixel_rgb888(_inactive_r, _inactive_g, _inactive_b);
				t = 5;
				break;
			}
			draw_filled_octagon(x, y, w, h, chamfer, border, _px, _w, _h);
			draw_filled_octagon(x + t, y + t, w - 2 * t, h - 2 * t,
			                    chamfer > t ? chamfer - t : 0,
			                    Pixel_rgb888(_glass_r, _glass_g, _glass_b),
			                    _px, _w, _h);

			/* the title bar (matching the mockup): a thin peach top border,
			 * the dark navy fill, a white divider, and the title text inside.
			 * The mockup's title bars carry no window controls. */
			_h_line(x, y, w, _accent_r, _accent_g, _accent_b);
			_glass(x, y + 1, w, 36, 7, 10, 11, 220);
			_h_line(x, y + 37, w, 255, 255, 255);
			_text(s, x + 20, y + 11, 11.0f, _white(), title);
		}

		/* the hideable bottom panel: a glass strip with branding + running apps */
		void _panel(Surface<Pixel> &s, int y)
		{
			_glass(0, y, _w, _panel_h, 15, 22, 33, 235);
			_h_line(0, y,     _w, _accent_r, _accent_g, _accent_b);   /* accent top edge */
			_h_line(0, y + 1, _w, _accent_r, _accent_g, _accent_b);
			_draw_emblem(28, y + 30, 26);
			_draw_text(s, 60, y + 14, _white(), "HermOS");
			_draw_text(s, 60, y + 40, _muted(), "A More Human Computer");

			/* running cells as chips */
			int chip_x = 260;
			for (int i = 0; i < NUM_CELLS; ++i) {
				Cell_session *hosted = _gui_root.cells().at(i);
				if (!hosted) continue;
				_fill_rrect(chip_x, y + 22, 110, 24, 8, 25, 35, 50);
				_draw_text(s, chip_x + 10, y + 27, _white(),
				           _short_label(hosted->label().string()));
				chip_x += 122;
			}

			_draw_text(s, _w - 230, y + 14, _muted(), _clock());
			_draw_text(s, _w - 230, y + 40, _solar(), "move cursor away to hide");
		}

		/* window content lines, drawn below the title bar */
		void _win_lines(Surface<Pixel> &s, int x, int y, char const *lines[], int n)
		{
			for (int i = 0; i < n; ++i)
				_draw_text(s, x + 14, y + 38 + i * 18, _muted(), lines[i]);
		}

		/* a single agent entry in the right panel */
		void _agent_cell(Surface<Pixel> &s, int x, int y, int w, int h,
		                 char const *name, char const *status, bool running)
		{
			_fill_rrect(x, y, w, h, 8, running ? 40 : 46, running ? 54 : 50, running ? 66 : 56);
			_stroke_rrect(x, y, w, h, 8, 120, 135, 155);
			_disc(x + 20, y + h / 2, 7, running ? 255 : 90, running ? 140 : 110, running ? 40 : 120);
			_draw_text(s, x + 36, y + h / 2 - 8, _white(), name);
			_draw_text(s, x + 36, y + h / 2 + 8, _muted(), status);
			if (running)
				_glyph_ellipsis(x + w - 16, y + h / 2);
			else {
				_glyph_play(x + w - 38, y + h / 2);
				_glyph_ellipsis(x + w - 16, y + h / 2);
			}
		}

		/* ---- Phase 1d UI primitives ---- */

		void _text(Surface<Pixel> &s, int x, int y, float px, Color c, char const *t)
		{
			px *= 1.2f;   /* +20% base font size */
			_font.draw_text(s, x, y, px, c, t);
			if (px < 20.0f) _font.draw_text(s, x, y, px, c, t);  /* thicken tiny glyphs */
		}

		/* draw text, truncating with a trailing "..." to fit max_w pixels */
		void _clip_text(Surface<Pixel> &s, int x, int y, float px, int max_w,
		                Color c, char const *text)
		{
			px *= 1.2f;   /* +20% base font size */
			if (_font.text_width(px, text) <= max_w) {
				_font.draw_text(s, x, y, px, c, text);
				if (px < 20.0f) _font.draw_text(s, x, y, px, c, text);
				return;
			}
			size_t const n = Genode::strlen(text);
			size_t const cap = n < 63 ? n : 63;
			char buf[66];
			for (size_t len = cap; len > 0; --len) {
				Genode::memcpy(buf, text, len);
				buf[len] = '.'; buf[len + 1] = '.'; buf[len + 2] = '.'; buf[len + 3] = 0;
				if (_font.text_width(px, buf) <= max_w) {
					_font.draw_text(s, x, y, px, c, buf);
					if (px < 20.0f) _font.draw_text(s, x, y, px, c, buf);
					return;
				}
			}
		}

		void _button(Surface<Pixel> &s, int x, int y, int w, int h,
		             char const *label, bool accent)
		{
			_fill_rrect(x, y, w, h, h / 2, accent ? 255 : 40, accent ? 150 : 48, accent ? 40 : 60);
			_stroke_rrect(x, y, w, h, h / 2, accent ? 255 : 110, accent ? 170 : 135, accent ? 60 : 155);
			_text(s, x + 10, y + h / 2 - 8, 16.0f, accent ? _white() : _muted(), label);
		}

		/* a round accent button with a crisp, centred +/- glyph (text glyphs are
		 * too faint at this size and the button label is left-aligned) */
		void _stepper_btn(Surface<Pixel> &s, int x, int y, int w, int h, bool plus)
		{
			_fill_rrect(x, y, w, h, h / 2, 255, 150, 40);
			_stroke_rrect(x, y, w, h, h / 2, 255, 170, 60);
			int const cx = x + w / 2, cy = y + h / 2;
			int const len = 14, bar = 3;
			_fill_rrect(cx - len / 2, cy - bar / 2, len, bar, bar / 2, 245, 248, 251);
			if (plus) _fill_rrect(cx - bar / 2, cy - len / 2, bar, len, bar / 2, 245, 248, 251);
		}

		void _progress(int x, int y, int w, int h, int pct)
		{
			_fill_rrect(x, y, w, h, h / 2, 46, 52, 58);
			_stroke_rrect(x, y, w, h, h / 2, 120, 135, 155);
			int const fw = w * pct / 100;
			if (fw >= 4) _fill_rrect(x + 2, y + 2, fw - 4, h - 4, (h - 4) / 2, 255, 150, 40);
		}

		void _check_row(Surface<Pixel> &s, int x, int y, int max_w, char const *label, bool done)
		{
			_disc(x + 6, y + 9, 7, done ? 255 : 52, done ? 150 : 60, done ? 40 : 70);
			if (done) _glyph_check(x + 6, y + 9);
			_clip_text(s, x + 22, y, 18.0f, max_w, done ? _white() : _muted(), label);
		}

		/* left sidebar menu inside a window */
		void _menu(Surface<Pixel> &s, int x, int y, int w, char const *items[], int n, int sel)
		{
			for (int i = 0; i < n; ++i) {
				int const iy = y + 36 + i * 20;
				if (i == sel) {
					_fill_rrect_glass(x + 8, iy - 15, w - 16, 23, 6, 255, 150, 40, 40);
					_clip_text(s, x + 12, iy - 9, 13.0f, w - 22, _white(), items[i]);
				} else {
					_clip_text(s, x + 12, iy - 9, 13.0f, w - 22, _muted(), items[i]);
				}
			}
		}

		/* a single agent card with an icon dot, status, optional progress bar */
		void _agent_card(Surface<Pixel> &s, int x, int y, int w, int h,
		                 char const *name, char const *status,
		                 uint8_t ir, uint8_t ig, uint8_t ib, int pct)
		{
			_fill_rrect(x, y, w, h, 8, 40, 54, 66);
			_stroke_rrect(x, y, w, h, 8, 120, 135, 155);
			_disc(x + 16, y + 16, 8, ir, ig, ib);
			_clip_text(s, x + 34, y + 8, 9.5f, w - 62, _white(), name);
			_clip_text(s, x + 34, y + 26, 8.0f, w - 62, _muted(), status);
			if (pct >= 0) _progress(x + 34, y + 44, w - 56, 6, pct);
			_glyph_play(x + w - 32, y + h - 14);
			_glyph_ellipsis(x + w - 14, y + h - 14);
		}

		/* ===================================================================
		 * ImGui-derived widget toolkit (REA-extracted from imgui v1.91.5).
		 *   GetFrameHeight() = FontSize + 2*FramePadding.y     (imgui.cpp:11038)
		 *   ButtonEx size    = text_size + 2*FramePadding      (widgets:731)
		 *   Text align       = min + (avail - text_size)*align (imgui.cpp:3576)
		 *   Checkbox square  = GetFrameHeight(); label_x = square + ItemInnerSpacing.x;
		 *   Checkmark pad    = square/6                        (widgets:1149/1199/1203)
		 *   Vertical rhythm  = next_y = y + h + ItemSpacing.y  (imgui.cpp:10805)
		 * =================================================================== */
		static constexpr int kFramePaddingX   = 4;   /* FramePadding.x     */
		static constexpr int kFramePaddingY   = 3;   /* FramePadding.y     */
		static constexpr int kItemSpacingY    = 4;   /* ItemSpacing.y      */
		static constexpr int kItemInnerSpaceX = 4;   /* ItemInnerSpacing.x */

		struct Widget { int x, y, w, h; };

		static int im_next_y(int y, int h) { return y + h + kItemSpacingY; }

		/* draw_rect: the RenderFrame primitive (filled rounded rect) */
		void draw_rect(Widget const &w, int radius, uint8_t r, uint8_t g, uint8_t b)
		{
			_fill_rrect(w.x, w.y, w.w, w.h, radius, r, g, b);
		}

		/* draw_text: RenderTextClippedEx's align step, verbatim:
		 *   pos = pos_min + (pos_max - pos_min - text_size) * align */
		void draw_text(Surface<Pixel> &s, Widget const &w, int px, Color c,
		               char const *text, float ax, float ay)
		{
			float const sp = (float)px * 1.2f;   /* +20% base font size */
			int const tw = _font.text_width(sp, text);
			int const tx = w.x + int((w.w - tw) * ax);
			int const ty = w.y + int((w.h - (int)sp) * ay);
			_text(s, tx, ty, (float)px, c, text);
		}

		/* draw_button: ButtonEx math — size = text + 2*FramePadding, text centered */
		Widget draw_button(Surface<Pixel> &s, char const *label, int x, int y,
		                   int px, bool accent)
		{
			int const bw = _font.text_width((float)px, label) + 2 * kFramePaddingX;
			int const bh = px + 2 * kFramePaddingY;      /* GetFrameHeight */
			Widget bb { x, y, bw, bh };
			draw_rect(bb, bh / 2, accent ? 255 : 40, accent ? 150 : 48, accent ? 40 : 60);
			draw_text(s, bb, px, _white(), label, 0.5f, 0.5f);   /* ButtonTextAlign */
			return bb;
		}

		/* draw_checklist_item: Checkbox math — square=FrameHeight, label at
		 * square + ItemInnerSpacing.x, checkmark pad = square/6 */
		Widget draw_checklist_item(Surface<Pixel> &s, char const *label, int x, int y,
		                           int px, bool checked)
		{
			int const square = px + 2 * kFramePaddingY;      /* GetFrameHeight */
			_fill_rrect(x, y, square, square, 4, 40, 48, 60);
			_stroke_rrect(x, y, square, square, 4, 120, 135, 155);
			if (checked)
				_glyph_check(x + square / 2, y + square / 2);
			int const lx = x + square + kItemInnerSpaceX;
			_text(s, lx, y + (square - px) / 2, (float)px, _white(), label);
			return Widget { x, y, square + kItemInnerSpaceX + _font.text_width((float)px, label), square };
		}

		/* ---- the two spec'd cell bodies (Welcome, System) ---- */

		void draw_welcome_cell(int x, int y, int w, int h)
		{
			Surface<Pixel> s(_px, Area(unsigned(_w), unsigned(_h)));

			/* the HermOS logo (PNG from /assets/logo.png) */
			_draw_icon("logo", x + 28, y + 58);

			/* the heading + subtitle */
			_text(s, x + 20, y + 42, 12.0f, _white(), "Welcome to HermOS");
			_text(s, x + 20, y + 58, 9.0f, _muted(), "A More Human Computer");

			/* the checklist via the ImGui checkbox math + ItemSize rhythm */
			char const *steps[5] = { "Detect your computer", "Choose installation target",
			                         "Review installation plan", "Install HermOS", "Complete" };
			int iy = y + 78;
			for (int i = 0; i < 5; ++i) {
				Widget it = draw_checklist_item(s, steps[i], x + 50, iy, 11, i < 1);
				iy = im_next_y(iy, it.h);
			}

			/* the Continue button (ButtonEx math) */
			draw_button(s, "Continue +", x + w - 96, y + h - 31, 11, true);
		}

		void draw_system_cell(int x, int y, int w, int h)
		{
			Surface<Pixel> s(_px, Area(unsigned(_w), unsigned(_h)));

			/* the left navigation list */
			char const *menu[10] = { "System", "Hardware", "Storage", "Network", "Displays",
			                         "Audio", "Users", "Updates", "Security", "About" };
			_menu(s, x + 8, y + 26, 76, menu, 10, 0);

			/* the vertical separator between the list and the content */
			_v_line(x + 84, y + 32, h - 74, 90, 105, 125);

			/* the right column via the ImGui text-align math (left-aligned) */
			char const *lines[7] = { "HermOS 0.1 (Prototype)", "Architecture  x86_64 (NOVA)",
			                         "Memory  6.1 GB / 16 GB", "CPU  8 cores (QEMU)",
			                         "Graphics  Virtio-GPU", "Disk  20 GB",
			                         "Uptime  2 hours 14 min" };
			int ly = y + 44;
			for (int i = 0; i < 7; ++i) {
				Widget line { x + 96, ly, w - 108, 16 };
				draw_text(s, line, 11, _white(), lines[i], 0.0f, 0.0f);
				ly = im_next_y(ly, 16);
			}
		}

		/* blit the pristine wallpaper (aspect-fill, centered crop) into a rect */
		void _blit_wallpaper_region(int dx, int dy, int dw, int dh)
		{
			if (!_wall_rgb || dw <= 0 || dh <= 0) return;
			int sw = _w, sh = _h, sx = 0, sy = 0;
			if (sw * dh > sh * dw) { int const nw = sh * dw / dh; sx = (sw - nw) / 2; sw = nw; }
			else                  { int const nh = sw * dh / dw; sy = (sh - nh) / 2; sh = nh; }
			for (int py = 0; py < dh; ++py) {
				int const wy = sy + py * sh / dh;
				for (int px = 0; px < dw; ++px) {
					int const wx = sx + px * sw / dw;
					uint8_t const *src = _wall_rgb + (wy * _w + wx) * 3;
					_set(dx + px, dy + py, src[0], src[1], src[2]);
				}
			}
		}

		/* ---- the six concept windows (2x3 grid) ---- */

		void _win_welcome(Surface<Pixel> &s, int x, int y, int w, int h)
		{
			/* LEFT pane (visual half): the scenic background + big emblem + wordmark */
			int const pw = w * 46 / 100;
			int const pcx = x + pw / 2;
			_blit_wallpaper_region(x + 4, y + 4, pw - 6, h - 8);
			_draw_emblem(pcx, y + h * 40 / 100, 42);
			_text(s, pcx - _font.text_width(22.0f, "HermOS") / 2, y + h * 58 / 100, 22.0f, _white(), "HermOS");
			_text(s, pcx - _font.text_width(11.0f, "A MORE HUMAN COMPUTER") / 2, y + h * 70 / 100, 11.0f, _muted(), "A MORE HUMAN COMPUTER");

			/* RIGHT pane: the title + the checklist + the Continue button */
			int const rx = x + pw + 10;
			_text(s, rx, y + 20, 24.0f, _white(), "Welcome to HermOS");
			char const *steps[5] = { "Detect your computer", "Choose installation target",
			                         "Review installation plan", "Install HermOS", "Complete" };
			for (int i = 0; i < 5; ++i)
				_check_row(s, rx, y + 52 + i * 26, w - pw - 22, steps[i], i < 1);

			_button(s, x + w - 100, y + h - 36, 90, 24, "Continue", true);
		}

		void _win_system(Surface<Pixel> &s, int x, int y, int w, int h)
		{
			char const *menu[10] = { "Overview", "Hardware", "Storage", "Network", "Displays",
			                         "Audio", "Users", "Updates", "Security", "About" };
			_menu(s, x + 8, y + 26, 76, menu, 10, 0);
			_v_line(x + 84, y + 32, h - 74, 90, 105, 125);

			int const cx = x + 96;
			char const *specs[7] = { "Version 0.1", "Arch  x86_64", "Uptime  2h 14m",
			                         "CPU  6.1/16 GB", "Cores  8", "GPU  VirtIO", "Disk  20 GB" };
			for (int i = 0; i < 7; ++i)
				_clip_text(s, cx, y + 42 + i * 20, 14.0f, w - 108, _white(), specs[i]);
		}

		void _win_files(Surface<Pixel> &s, int x, int y, int w, int h)
		{
			char const *menu[8] = { "Home", "Documents", "Pictures", "Videos", "Music",
			                        "Downloads", "Agents", "Shared" };
			_menu(s, x + 8, y + 26, 60, menu, 8, 0);
			_v_line(x + 68, y + 32, h - 74, 90, 105, 125);

			_text(s, x + 82, y + 40, 14.0f, _white(), "Home");
			char const *folders[6] = { "Documents", "Pictures", "Videos", "Music", "Agents", "Projects" };
			for (int i = 0; i < 6; ++i) {
				int const cx = x + 84 + (i % 3) * 34;
				int const cy = y + 54 + (i / 3) * 44;
				_draw_icon("folder", cx, cy);
				_clip_text(s, cx - 16, cy + 13, 13.0f, 32, _white(), folders[i]);
			}
		}

		void _win_hydro(Surface<Pixel> &s, int x, int y, int w, int h)
		{
			char const *menu[6] = { "Overview", "Live View", "Schedule", "Data",
			                        "Permissions", "Logs" };
			_menu(s, x + 8, y + 26, 76, menu, 6, 0);
			_v_line(x + 84, y + 32, h - 74, 90, 105, 125);

			int const cx = x + 96;
			_disc(x + w - 40, y + 34, 5, 60, 200, 90);
			_text(s, x + w - 33, y + 38, 13.0f, _white(), "Active");
			_draw_icon("leaf", x + w - 60, y + 64);

			_text(s, cx, y + 36, 16.0f, _white(), "Hydroponics Agent");
			_clip_text(s, cx, y + 52, 13.0f, w - 108, _white(), "Capturing image in 2 min...");
			_clip_text(s, cx, y + 66, 13.0f, w - 108, _white(), "Connected...");
			_clip_text(s, cx, y + 84, 13.0f, w - 108, _white(), "Current Task  Capture image");
			_clip_text(s, cx, y + 100, 13.0f, w - 108, _white(), "Next Run  07:00 (in 2 min)");
			_clip_text(s, cx, y + 116, 13.0f, w - 108, _white(), "Schedule  07:00/19:00 daily");
			_clip_text(s, cx, y + 132, 13.0f, w - 108, _white(), "Output  + Hydroponics Agent");
			_clip_text(s, cx, y + 148, 13.0f, w - 108, _white(), "Last Run  10 Dec 2024 19:00");

			_button(s, x + 12,   y + h - 31, 56, 15, "Pause", false);
			_button(s, x + 74,   y + h - 31, 62, 15, "Run Now", true);
			_button(s, x + 142,  y + h - 31, 66, 15, "Settings", false);
		}

		void _win_worker(Surface<Pixel> &s, int x, int y, int w, int h)
		{
			/* left: a cardboard box icon (as in the mockup) */
			_fill_rrect(x + 14, y + 46, 30, 22, 3, 200, 150, 90);
			_stroke_rrect(x + 14, y + 46, 30, 22, 3, 255, 180, 100);
			_fill_rrect(x + 14, y + 38, 30, 10, 3, 170, 120, 70);
			_stroke_rrect(x + 14, y + 38, 30, 10, 3, 230, 160, 90);
			_v_line(x + 28, y + 38, 30, 240, 210, 160);
			_v_line(x + 29, y + 38, 30, 240, 210, 160);

			/* right: the form */
			int const cx = x + 56;
			_text(s, cx, y + 32, 13.0f, _muted(), "Task");
			_clip_text(s, cx, y + 48, 14.0f, w - 64, _white(), "Take a photo of my seedling");
			_clip_text(s, cx, y + 72, 13.0f, w - 64, _muted(), "Twice daily (07:00 and 19:00)");
			_clip_text(s, cx, y + 88, 13.0f, w - 64, _muted(), "Requires  Camera Capture");
			_clip_text(s, cx, y + 104, 13.0f, w - 64, _muted(), "+ Add");
			_clip_text(s, cx, y + 120, 13.0f, w - 64, _muted(), "Resource Budget  Minimal");

			_button(s, cx,      y + h - 31, 56, 15, "Cancel", false);
			_button(s, cx + 62, y + h - 31, 104, 15, "Create Worker", true);
		}

		void _win_updates(Surface<Pixel> &s, int x, int y, int w, int h)
		{
			char const *menu[4] = { "Updates", "Installed", "Sources", "Recovery" };
			_menu(s, x + 8, y + 26, 60, menu, 4, 0);
			_v_line(x + 68, y + 32, h - 74, 90, 105, 125);

			_disc(x + 76, y + 40, 6, 60, 200, 90);
			_glyph_check(x + 76, y + 40);
			_clip_text(s, x + 88, y + 36, 14.0f, w - 96, _white(), "System is up to date");

			_clip_text(s, x + 74, y + 64, 13.0f, w - 82, _white(), "HermOS 0.1 (Prototype)");
			_clip_text(s, x + 74, y + 82, 13.0f, w - 82, _white(), "Last checked: 10 Dec 07:40");
			_clip_text(s, x + 74, y + 100, 13.0f, w - 82, _white(), "99 Installed");
			_clip_text(s, x + 74, y + 118, 13.0f, w - 82, _white(), "Sources  Recovery");
			_clip_text(s, x + 74, y + 140, 13.0f, w - 82, _white(), "Independent System Evolution");

			_button(s, x + 12, y + h - 31, 100, 15, "Check for Updates", false);
		}

		/* draw the real wallpaper image (or a dark fallback if it failed to load) */
		void _blit_wallpaper()
		{
			if (!_wall_rgb) {
				for (int y = 0; y < _h; ++y) {
					uint8_t const v = uint8_t(12 + y * 40 / _h);
					for (int x = 0; x < _w; ++x) _set(x, y, v, uint8_t(v + 8), uint8_t(v + 16));
				}
				return;
			}
			for (int y = 0; y < _h; ++y)
				for (int x = 0; x < _w; ++x) {
					int const i = (y * _w + x) * 3;
					_set(x, y, _wall_rgb[i], _wall_rgb[i + 1], _wall_rgb[i + 2]);
				}
		}

		void _draw_text(Surface<Pixel> &s, int x, int y, Color c, char const *text)
		{
			_font.draw_text(s, x, y, 16.0f, c, text);
		}

		/* per-cell geometry: HARDCODED (theme engine bypassed).
		 * Exact pixel values for a 1920x1080 layout. */
		static constexpr int NUM_CELLS = 6;
		static constexpr int MIN_CELL_W = 120, MIN_CELL_H = 90;

		struct Cell_geom { int x, y, w, h; };
		Cell_geom _geom[NUM_CELLS] {
			{  150, 300, 450, 300 },  /* 0: Welcome          */
			{  630, 300, 450, 300 },  /* 1: System           */
			{ 1110, 300, 380, 300 },  /* 2: Files            */
			{  150, 630, 450, 300 },  /* 3: Hydroponics      */
			{  630, 630, 450, 300 },  /* 4: Create Worker    */
			{ 1110, 630, 380, 300 },  /* 5: Updates          */
		};
		int _chamfer[NUM_CELLS] { 15, 15, 15, 15, 15, 15 };  /* % of min(w,h) */

		/* Step 2: the per-slot decoration state (see the enum below). */
		Cell_state _cell_state[NUM_CELLS] {
			CELL_ACTIVE, CELL_INACTIVE, CELL_INACTIVE,
			CELL_INACTIVE, CELL_INACTIVE, CELL_INACTIVE };

		/* Phase A: the dynamic Cell tiling. The Cells stay the octagonal,
		 * resizable, dynamic units; the tiling is one managed arrangement of
		 * them (the master-stack), toggled with Meta+T. The region comes from
		 * the theme's slot grid — no hardcoded geometry. */
		bool _tiled { false };
		bool _meta_down { false };
		bool _tile_snapshot { false };
		Cell_geom _tile_orig[NUM_CELLS] { };
		int _tile_rx { 0 }, _tile_ry { 0 }, _tile_rw { 0 }, _tile_rh { 0 };

		/* KDE-style quick-tile: configurable split ratios + a stable work-area
		 * snapshot so Meta+R restores the theme grid. */
		int  _quick_hsplit { 50 }, _quick_vsplit { 50 };
		bool _quick_snapped { false };
		Cell_geom _quick_orig[NUM_CELLS] { };
		int  _quick_rx { 0 }, _quick_ry { 0 }, _quick_rw { 0 }, _quick_rh { 0 };

		/* the tiling region = the bounding box of the theme's slot grid */
		void _tile_snapshot_region()
		{
			int x0 = 1 << 20, y0 = 1 << 20, x1 = 0, y1 = 0;
			for (int i = 0; i < NUM_CELLS; ++i) {
				Cell_geom const &g = _geom[i];
				if (g.x < x0) x0 = g.x;
				if (g.y < y0) y0 = g.y;
				if (g.x + g.w > x1) x1 = g.x + g.w;
				if (g.y + g.h > y1) y1 = g.y + g.h;
			}
			_tile_rx = x0; _tile_ry = y0;
			_tile_rw = x1 - x0; _tile_rh = y1 - y0;
		}

		/* the master-stack: the focused Cell owns the master (the left 55%),
		 * the remaining Cells stack on the right, heights divided evenly */
		void _apply_tile_layout()
		{
			if (!_tile_snapshot) {
				for (int i = 0; i < NUM_CELLS; ++i) _tile_orig[i] = _geom[i];
				_tile_snapshot_region();
				_tile_snapshot = true;
			}

			int const gap    = 10;
			int const master = _focus_idx();
			int const mw     = _tile_rw * 55 / 100 - gap / 2;

			_geom[master].x = _tile_rx;
			_geom[master].y = _tile_ry;
			_geom[master].w = mw;
			_geom[master].h = _tile_rh;

			int n = 0;
			for (int i = 0; i < NUM_CELLS; ++i) if (i != master) ++n;
			if (!n) return;

			int const sx = _tile_rx + mw + gap;
			int const sw = _tile_rw - mw - gap;
			int const sh = (_tile_rh - (n - 1) * gap) / n;

			int y = _tile_ry;
			for (int i = 0; i < NUM_CELLS; ++i) {
				if (i == master) continue;
				_geom[i].x = sx;
				_geom[i].y = y;
				_geom[i].w = sw;
				_geom[i].h = sh;
				y += sh + gap;
			}
		}

		void _toggle_tile()
		{
			_tiled = !_tiled;
			if (_tiled) {
				_apply_tile_layout();
				log("HermOS: Cells tiled (master-stack)");
			} else if (_tile_snapshot) {
				for (int i = 0; i < NUM_CELLS; ++i) _geom[i] = _tile_orig[i];
				_tile_snapshot = false;
				log("HermOS: Cells restored to the theme grid");
			}
			_dirty = true;
		}

		/* KDE quick-tile: snap the focused Cell to a fraction of the work area.
		 * modes: 0 left, 1 right, 2 top, 3 bottom, 4 tl, 5 tr, 6 bl, 7 br,
		 *        8 maximize, 9 restore. Split ratios are % of the work area. */
		void _quick_tile(int mode)
		{
			int const idx = _focus_idx();
			if (idx < 0 || idx >= NUM_CELLS) return;

			if (mode == 9) {   /* restore the theme grid */
				if (_quick_snapped) {
					for (int i = 0; i < NUM_CELLS; ++i) _geom[i] = _quick_orig[i];
					_quick_snapped = false;
					_dirty = true;
					log("HermOS: quick-tile restore");
				}
				return;
			}

			if (!_quick_snapped) {
				for (int i = 0; i < NUM_CELLS; ++i) _quick_orig[i] = _geom[i];
				_tile_snapshot_region();   /* the work area = the slot-grid bbox */
				_quick_rx = _tile_rx; _quick_ry = _tile_ry;
				_quick_rw = _tile_rw; _quick_rh = _tile_rh;
				_quick_snapped = true;
			}

			int const rx = _quick_rx, ry = _quick_ry, rw = _quick_rw, rh = _quick_rh;
			if (rw <= 0 || rh <= 0) return;
			int const gap = 10;
			int const hs  = _quick_hsplit, vs = _quick_vsplit;   /* % */

			Cell_geom g { rx, ry, rw, rh };
			switch (mode) {
			case 0: g.w = rw*hs/100 - gap/2; break;                                    /* left   */
			case 1: g.x = rx + rw*hs/100 + gap/2; g.w = rw - rw*hs/100 - gap/2; break; /* right  */
			case 2: g.h = rh*vs/100 - gap/2; break;                                    /* top    */
			case 3: g.y = ry + rh*vs/100 + gap/2; g.h = rh - rh*vs/100 - gap/2; break; /* bottom */
			case 4: g.w = rw*hs/100 - gap/2; g.h = rh*vs/100 - gap/2; break;           /* tl     */
			case 5: g.x = rx + rw*hs/100 + gap/2; g.w = rw - rw*hs/100 - gap/2;
			        g.h = rh*vs/100 - gap/2; break;                                    /* tr     */
			case 6: g.w = rw*hs/100 - gap/2;
			        g.y = ry + rh*vs/100 + gap/2; g.h = rh - rh*vs/100 - gap/2; break; /* bl     */
			case 7: g.x = rx + rw*hs/100 + gap/2; g.w = rw - rw*hs/100 - gap/2;
			        g.y = ry + rh*vs/100 + gap/2; g.h = rh - rh*vs/100 - gap/2; break; /* br     */
			case 8: break;   /* maximize = the full work area */
			default: return;
			}
			_geom[idx] = g;
			_dirty = true;
			log("HermOS: quick-tile mode ", mode, " -> (", g.x, ",", g.y, " ",
			    g.w, "x", g.h, ")");
		}

		int _draw_order[NUM_CELLS] { 0, 1, 2, 3, 4, 5 };  /* bottom -> top */

		/* drag / resize state */
		enum Drag_mode { DRAG_NONE = 0, DRAG_MOVE, DRAG_RESIZE };
		int       _drag_mode { DRAG_NONE };
		int       _drag_cell { -1 };
		int       _drag_edge { 0 };   /* bitmask: 1=left 2=right 4=top 8=bottom */
		int       _drag_off_x { 0 }, _drag_off_y { 0 };  /* move grab offset */
		int       _drag_sx { 0 }, _drag_sy { 0 };        /* resize grab origin */
		Cell_geom _drag_orig { };

		/* hideable bottom panel (slides up when the cursor hits the bottom edge) */
		bool _bottom_panel_visible { false };
		bool _chrome_hidden { false };   /* F11 toggles the right sidebar (Agents & Workers) */
		int  _panel_y { 0 };          /* top Y of the panel; init to _h in the ctor */

		/* theme state: BYPASSED. These are the hardcoded colors.
		 * Glass: RGB(13, 18, 30) at alpha 220. Active border: RGB(253, 173, 85).
		 * Inactive border: RGB(252, 252, 253). */
		uint8_t _active_r   { 254 }, _active_g   { 186 }, _active_b   { 121 };
		unsigned _active_a  { 255 };
		uint8_t _inactive_r { 252 }, _inactive_g { 252 }, _inactive_b { 253 };
		unsigned _inactive_a { 255 };
		uint8_t _paused_r   { 150 }, _paused_g   { 150 }, _paused_b   { 150 };
		unsigned _paused_a  { 255 };
		uint8_t _glass_r    {  13 }, _glass_g    {  18 }, _glass_b    {  30 };
		unsigned _glass_a   { 220 };
		uint8_t _dock_r     {  46 }, _dock_g     {  50 }, _dock_b     {  56 };
		int _sb_x { 901 }, _sb_y { 77 }, _sb_w { 102 }, _sb_h { 614 };

		/* Step 3: the normalized snap slots from the theme_state report */
		enum { MAX_SNAPS = 4 };
		struct Snap_def { char id[16]; int x_pct, w_pct; bool valid; };
		Snap_def _snaps[MAX_SNAPS] { };
		int      _snap_count { 0 };
		uint8_t _sidebar_r  {  19 }, _sidebar_g  {  29 }, _sidebar_b  {  42 };
		uint8_t _accent_r   { 254 }, _accent_g   { 186 }, _accent_b   { 121 };
		int     _panel_h    { 150 };
		int     _theme_font {  12 };

		Constructible<Attached_rom_dataspace> _theme_state { };
		Signal_handler<Main> _theme_sigh { _env.ep(), *this, &Main::_handle_theme };

		/* Step 1: theme_engine as a child of the desktop (the spawn policy) */
		Constructible<Theme_child_policy> _theme_child { };

		/* Step 5b: agents_sidebar as a child of the desktop */
		Constructible<Sidebar_child_policy> _sidebar_child { };

		/* the dock: the bottom_dock's dock_state ROM drives the Y-offset.
		 * When visible (height H), every slot renders H pixels higher:
		 * final_y = cell.y - _dock_y_offset */
		Attached_rom_dataspace _dock_state_rom { _env, "dock_state" };
		Signal_handler<Main> _dock_sigh { _env.ep(), *this, &Main::_handle_dock };
		int _dock_y_offset { 0 };

		int _cell_w(int i) const { return _geom[i].w; }
		int _cell_h(int i) const { return _geom[i].h; }
		int _win_x(int i) const { return _geom[i].x; }
		int _win_y(int i) const { return _geom[i].y - _dock_y_offset; }

		/* dynamic grid math: 3 cols x 2 rows in the bottom band.
		 * cell_w = (W - padding) / 3 ; cell_h = (available_h - padding) / 2 */
		void _compute_grid()
		{
			int const pad = 24, gap = 14;
			int const cell_w = (_w - pad * 2 - gap * 2) / 3;
			int const top    = _h * 54 / 100;
			int const bot    = _h - 8;
			int const cell_h = (bot - top - gap) / 2;
			for (int i = 0; i < NUM_CELLS; ++i) {
				int const c = i % 3, r = i / 3;
				_geom[i].x = pad + c * (cell_w + gap);
				_geom[i].y = top + r * (cell_h + gap);
				_geom[i].w = cell_w;
				_geom[i].h = cell_h;
				_chamfer[i] = 10;
			}
		}
		int _focus_idx() const { int i = _gui_root.cells().focused_index(); return i < 0 ? 0 : i; }
		int _focus_cy() const { return _win_y(_focus_idx()) + _cell_h(_focus_idx()) / 2; }
		int _ctrl_lx()  const { return _win_x(_focus_idx()) + 16; }
		int _ctrl_rx()  const { return _win_x(_focus_idx()) + _cell_w(_focus_idx()) - 16; }

		/* RTC-backed wall clock for the taskbar */
		Constructible<Rtc::Connection> _rtc { };
		Genode::String<48> _clock_buf { };

		static char const *_month_name(unsigned m)
		{
			static char const *names[12] = {
				"Jan","Feb","Mar","Apr","May","Jun",
				"Jul","Aug","Sep","Oct","Nov","Dec" };
			return (m >= 1 && m <= 12) ? names[m - 1] : "---";
		}

		char const *_clock()
		{
			if (_rtc.constructed()) {
				Rtc::Timestamp const ts = _rtc->current_time();
				_clock_buf = Genode::String<48>(
					ts.day, " ", _month_name(ts.month), " ", ts.year, "  ",
					ts.hour, ":", (ts.minute < 10 ? "0" : ""), ts.minute);
			} else {
				_clock_buf = Genode::String<48>("-- --- ---- --:--");
			}
			return _clock_buf.string();
		}

		static char const *_short_label(char const *l)
		{
			char const *last = l;
			for (char const *p = l; *p; ++p)
				if (p[0] == '-' && p[1] == '>' && p[2] == ' ') last = p + 3;
			return last;
		}

		void _cursor()
		{
			int const x = _pointer_x, y = _pointer_y;
			for (int i = 0; i < 12; ++i)
				_h_line(x + i, y + i, 12 - i, 255, 160, 70);
			_set(x, y, 255, 255, 255);
		}

		/* composite one hosted app's framebuffer into Cell i */
		void _blit_cell(int i, Cell_session *hosted)
		{
			if (!hosted || !hosted->fb_valid()) return;

			Pixel const *src = hosted->fb_pixels();
			int const sw = hosted->fb_w(), sh = hosted->fb_h();
			if (sw <= 0 || sh <= 0) return;

			int const cw = _cell_w(i), ch = _cell_h(i);
			int const x = _win_x(i), y = _win_y(i);
			int const left = x + 14,  right = x + cw - 14;
			int const top  = y + 38, bottom = y + ch - 8;
			if (right <= left || bottom <= top) return;

			for (int py = top; py < bottom; ++py) {
				int const sy = (py - top) * sh / (bottom - top);
				for (int px = left; px < right; ++px) {
					int const sx = (px - left) * sw / (right - left);
					Pixel const &p = src[sy * sw + sx];
					_set(px, py, uint8_t(p.r()), uint8_t(p.g()), uint8_t(p.b()));
				}
			}
		}

		/* composite every hosted app into its Cell */
		void _blit_all()
		{
			for (int i = 0; i < NUM_CELLS; ++i)
				_blit_cell(i, _gui_root.cells().at(i));
		}

		/* map a screen point into the hosted app's framebuffer coordinate space */
		void _cell_point(int sx, int sy, int idx, Cell_session *h, int &ax, int &ay)
		{
			int const cw = _cell_w(idx), ch = _cell_h(idx);
			int const x = _win_x(idx), y = _win_y(idx);
			int const left = x + 14,  right = x + cw - 14;
			int const top  = y + 38, bottom = y + ch - 8;
			int const sw = h->fb_w(), sh = h->fb_h();
			if (right <= left || bottom <= top || sw <= 0 || sh <= 0) { ax = ay = 0; return; }
			int const cx = sx < left ? left : (sx >= right ? right - 1 : sx);
			int const cy = sy < top  ? top  : (sy >= bottom ? bottom - 1 : sy);
			ax = (cx - left) * sw / (right - left);
			ay = (cy - top)  * sh / (bottom - top);
		}

		/* forward a desktop input event into the hosted Cell (translated) */
		void _forward_input(Input::Event const &ev)
		{
			Cell_session *h = _gui_root.cells().focused();
			if (!h || _paused || !h->fb_valid() || _bar_focused) return;
			int const idx = _gui_root.cells().focused_index();

			ev.handle_absolute_motion([&] (int x, int y) {
				int const sx = x * (_w - 1) / 32767;
				int const sy = y * (_h - 1) / 32767;
				int ax, ay; _cell_point(sx, sy, idx, h, ax, ay);
				h->submit_input(Input::Event(Input::Absolute_motion { ax, ay }));
			});
			ev.handle_press([&] (Input::Keycode key, Input::Codepoint cp) {
				h->submit_input(Input::Event(Input::Press_char { key, cp }));
			});
			ev.handle_release([&] (Input::Keycode key) {
				h->submit_input(Input::Event(Input::Release { key }));
			});
			ev.handle_relative_motion([&] (int x, int y) {
				h->submit_input(Input::Event(Input::Relative_motion { x, y }));
			});
		}

		/* map an App_kind to the binary the nested init should spawn */
		static char const *_app_binary(App_kind k)
		{
			switch (k) {
			case APP_TERMINAL: return "terminal";
			case APP_FILES:    return "hermos_files";
			case APP_TESTNIT:  return "testnit";
			default:           return "testnit";
			}
		}

		/* spawn an app into the first free Cell slot via the nested init */
		void _launch_app(App_kind kind)
		{
			int slot = -1;
			for (int i = 0; i < MAX_APPS; ++i)
				if (_app_kind[i] == APP_NONE) { slot = i; break; }
			if (slot < 0) { log("HermOS: all Cell slots busy"); return; }
			_app_kind[slot] = kind;
			log("HermOS: launching '", _app_binary(kind), "' into slot ", slot);
			_emit_apps_config();
			_dirty = true;
		}

		/* destroy the app hosted in a Cell slot, returning its RAM/CPU quota */
		void _close_app(int slot)
		{
			if (slot < 0 || slot >= MAX_APPS) return;
			if (_app_kind[slot] == APP_NONE) return;
			_app_kind[slot] = APP_NONE;
			log("HermOS: slot ", slot, " released, init reconfigured (quota returned)");
			_emit_apps_config();
		}

		/* map a registry stack name to an App_kind */
		App_kind _kind_from_stack(char const *stack)
		{
			if (Genode::strcmp(stack, "terminal") == 0) return APP_TERMINAL;
			if (Genode::strcmp(stack, "files")    == 0) return APP_FILES;
			if (Genode::strcmp(stack, "testnit")  == 0) return APP_TESTNIT;
			return APP_NONE;
		}

		/* load the app registry from /apps/registry (lines: name|stack|icon) */
		void _load_registry()
		{
			void *data = nullptr; size_t size = 0;
			if (!_read_file(_vfs, "/apps/registry", _asset_heap, data, size)) {
				log("HermOS: no /apps/registry (dock empty)");
				return;
			}

			char const *p = (char const *)data;
			char const *const end = p + size;

			while (p < end && _registry_count < MAX_APPS) {
				char const *le = p;
				while (le < end && *le != '\n' && *le != '\r') ++le;

				char const *b = p;                    /* name end (|) */
				while (b < le && *b != '|') ++b;
				if (b >= le) { p = le + 1; continue; }
				char const *d = b + 1;                /* stack end (|) */
				while (d < le && *d != '|') ++d;
				if (d >= le) { p = le + 1; continue; }
				char const *e = d + 1;                /* icon start */

				char stack[16];
				size_t const slen = (size_t)(d - b - 1);
				if (slen < 15) {
					Genode::memcpy(stack, b + 1, slen);
					stack[slen] = 0;
					App_kind const kind = _kind_from_stack(stack);

					char icon[64];
					size_t const ilen = (size_t)(le - e);
					if (kind != APP_NONE && ilen < 63) {
						Genode::memcpy(icon, e, ilen);
						icon[ilen] = 0;
						App_entry &entry = _registry[_registry_count];
						entry.name = Genode::String<32>(Cstring(p, (size_t)(b - p)));
						entry.kind = kind;
						if (_read_file(_vfs, icon, _asset_heap, entry.icon_data, entry.icon_size)) {
							log("HermOS: registry app '", entry.name, "' icon ",
							    static_cast<char const *>(icon));
							++_registry_count;
						} else {
							log("HermOS: registry icon missing: ",
							    static_cast<char const *>(icon));
						}
					}
				}
				p = (le < end) ? le + 1 : end;
			}
			log("HermOS: loaded ", _registry_count, " apps from /apps/registry");
		}

		/* blit a pre-loaded PNG icon centred at (cx, cy) */
		void _draw_icon_png(void *data, size_t size, int cx, int cy)
		{
			if (!data || !size) return;
			Surface<Pixel> s(_px, Area(unsigned(_w), unsigned(_h)));
			draw_png(s, cx - 10, cy - 10, data, size);
		}

		/* Step 4: copy a theme icon path (keep the default when empty) */
		static void _copy_icon_path(char *dst, char const *src)
		{
			if (!src || !src[0]) return;
			size_t const l = Genode::strlen(src);
			size_t const cp = l < 63 ? l : 63;
			Genode::memcpy(dst, src, cp);
			dst[cp] = 0;
		}

		/* Step 4: load one theme icon PNG from its path (keeps the previous
		 * data when the file is absent, so the glyph fallback stays usable) */
		void _load_theme_icon(char const *path, Theme_icon &ti)
		{
			void *data = nullptr; size_t size = 0;
			if (_read_file(_vfs, path, _asset_heap, data, size)) {
				if (ti.data) _asset_heap.free(ti.data, ti.size);
				ti.data = data;
				ti.size = size;
			}
		}

		/* emit a full terminal sandbox (terminal + vfs + fs_rom + bash) for slot i */
		void _emit_terminal_stack(Xml_generator &g, int i)
		{
			Genode::String<16> const term  { "term", i };
			Genode::String<16> const vfs   { "vfs", i };
			Genode::String<16> const fsrom { "fs_rom", i };
			Genode::String<16> const bash  { "bash", i };

			/* terminal: GUI terminal, routed to the desktop's Gui root */
			g.node("start", [&] {
				g.attribute("name", term.string());
				g.attribute("caps", 110);
				g.attribute("ram", "12M");
				g.node("provides", [&] {
					g.node("service", [&] { g.attribute("name", "Terminal"); });
				});
				g.node("binary", [&] { g.attribute("name", "terminal"); });
				g.node("route", [&] {
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.attribute("label", "config");
						g.node("parent", [&] { g.attribute("label", "hermos_terminal.config"); });
					});
					g.node("service", [&] { g.attribute("name", "Gui"); g.node("parent"); });
					g.node("any-service", [&] { g.node("parent"); });
				});
			});

			/* vfs: bash + coreutils filesystem served to the shell */
			g.node("start", [&] {
				g.attribute("name", vfs.string());
				g.attribute("caps", 200);
				g.attribute("ram", "48M");
				g.node("provides", [&] {
					g.node("service", [&] { g.attribute("name", "File_system"); });
				});
				g.node("binary", [&] { g.attribute("name", "vfs"); });
				g.node("config", [&] {
					g.node("default-policy", [&] {
						g.attribute("root", "/");
						g.attribute("writeable", "yes");
					});
					g.node("vfs", [&] {
						g.node("tar", [&] { g.attribute("name", "bash.tar"); });
						g.node("tar", [&] { g.attribute("name", "coreutils.tar"); });
						g.node("dir", [&] {
							g.attribute("name", "dev");
							g.node("terminal");
							g.node("log");
							g.node("rtc");
						});
						g.node("dir", [&] {
							g.attribute("name", "pipe");
							g.node("pipe");
						});
						g.node("dir", [&] {
							g.attribute("name", "tmp");
							g.node("ram");
						});
						g.node("dir", [&] {
							g.attribute("name", "home");
							g.node("ram");
						});
					});
				});
				g.node("route", [&] {
					g.node("service", [&] {
						g.attribute("name", "Terminal");
						g.node("child", [&] { g.attribute("name", term.string()); });
					});
					g.node("any-service", [&] { g.node("parent"); });
				});
			});

			/* fs_rom: expose the shell's /bin (from the vfs) as ROMs */
			g.node("start", [&] {
				g.attribute("name", fsrom.string());
				g.attribute("caps", 200);
				g.attribute("ram", "32M");
				g.node("provides", [&] {
					g.node("service", [&] { g.attribute("name", "ROM"); });
				});
				g.node("binary", [&] { g.attribute("name", "fs_rom"); });
				g.node("config");
				g.node("route", [&] {
					g.node("service", [&] {
						g.attribute("name", "File_system");
						g.node("child", [&] { g.attribute("name", vfs.string()); });
					});
					g.node("any-service", [&] { g.node("parent"); });
				});
			});

			/* bash: the shell, stdin/stdout wired to the terminal */
			g.node("start", [&] {
				g.attribute("name", bash.string());
				g.attribute("caps", 1000);
				g.attribute("ram", "32M");
				g.node("binary", [&] { g.attribute("name", "/bin/bash"); });
				g.node("config", [&] {
					g.node("libc", [&] {
						g.attribute("pipe", "/pipe");
						g.attribute("stdin", "/dev/terminal");
						g.attribute("stdout", "/dev/terminal");
						g.attribute("stderr", "/dev/terminal");
						g.attribute("rtc", "/dev/rtc");
					});
					g.node("vfs", [&] { g.node("fs"); });
					g.node("arg", [&] { g.attribute("value", "bash"); });
					g.node("env", [&] { g.attribute("key", "TERM"); g.attribute("value", "screen"); });
					g.node("env", [&] { g.attribute("key", "PATH"); g.attribute("value", "/bin"); });
				});
				g.node("route", [&] {
					g.node("service", [&] {
						g.attribute("name", "File_system");
						g.node("child", [&] { g.attribute("name", vfs.string()); });
					});
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.attribute("label_suffix", ".lib.so");
						g.node("parent");
					});
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.attribute("label_last", "/bin/bash");
						g.node("child", [&] { g.attribute("name", fsrom.string()); });
					});
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.attribute("label_prefix", "/bin");
						g.node("child", [&] { g.attribute("name", fsrom.string()); });
					});
					g.node("any-service", [&] { g.node("parent"); });
				});
			});
		}

		/* emit the Files app: hermos_files (the universal data engine) */
		void _emit_files_stack(Xml_generator &g, int i)
		{
			Genode::String<16> const name { "files", i };
			g.node("start", [&] {
				g.attribute("name", name.string());
				g.attribute("caps", 400);
				g.attribute("ram", "24M");
				g.node("binary", [&] { g.attribute("name", "hermos_files"); });
				g.node("config", [&] {
					g.node("vfs", [&] {
						g.node("dir", [&] {
							g.attribute("name", "assets");
							g.node("rom", [&] { g.attribute("name", "Roboto-Regular.ttf"); });
						});
						g.node("dir", [&] {
							g.attribute("name", "Home");
							g.node("inline", [&] { g.attribute("name", "README.txt"); g.append("Welcome to HermOS Files!"); });
							g.node("inline", [&] { g.attribute("name", "notes.md");   g.append("# A markdown note"); });
							g.node("inline", [&] { g.attribute("name", "todo.txt");   g.append("1. ship the OS"); });
							g.node("dir", [&] { g.attribute("name", "Documents"); g.node("ram"); });
							g.node("dir", [&] { g.attribute("name", "Downloads"); g.node("ram"); });
							g.node("dir", [&] { g.attribute("name", "Pictures");  g.node("ram"); });
						});
					});
				});
				g.node("route", [&] {
					g.node("service", [&] { g.attribute("name", "Gui"); g.node("parent"); });
					g.node("any-service", [&] { g.node("parent"); });
				});
			});
		}

		/* emit the nested "apps" init config (one <start> per running app) */
		void _emit_apps_config()
		{
			(void)_apps_report.generate_xml([&] (Xml_generator &g) {
				g.node("parent-provides", [&] {
					g.node("service", [&] { g.attribute("name", "ROM"); });
					g.node("service", [&] { g.attribute("name", "CPU"); });
					g.node("service", [&] { g.attribute("name", "PD"); });
					g.node("service", [&] { g.attribute("name", "RM"); });
					g.node("service", [&] { g.attribute("name", "LOG"); });
					g.node("service", [&] { g.attribute("name", "Gui"); });
					g.node("service", [&] { g.attribute("name", "Timer"); });
					g.node("service", [&] { g.attribute("name", "Rtc"); });
				});
				g.node("default-route", [&] {
					g.node("any-service", [&] { g.node("parent"); });
				});
				g.node("default", [&] {
					g.attribute("caps", 100);
					g.attribute("ram", "2M");
				});
				g.node("report", [&] { g.attribute("child_ram", "yes"); });
				for (int i = 0; i < MAX_APPS; ++i) {
					if (_app_kind[i] == APP_NONE) continue;
					if (_app_kind[i] == APP_TERMINAL) { _emit_terminal_stack(g, i); continue; }
					if (_app_kind[i] == APP_FILES)    { _emit_files_stack(g, i);    continue; }
					g.node("start", [&] {
						Genode::String<8> const name { "app", i };
						g.attribute("name", name.string());
						g.attribute("caps", 300);
						g.attribute("ram", "8M");
						g.node("binary", [&] { g.attribute("name", _app_binary(_app_kind[i])); });
						g.node("route", [&] {
							g.node("service", [&] {
								g.attribute("name", "Gui");
								g.node("parent");
							});
							g.node("any-service", [&] { g.node("parent"); });
						});
					});
				}
			});
		}

		void _handle_ai_command()
		{
			_ai_command->update();
			char const *const p = _ai_command->local_addr<char>();
			size_t const n = _ai_command->size();
			_ai_response = Genode::String<2048>(Cstring(p, n < 2047 ? n : 2047));
			log("HermOS ai_command: ", n, " bytes: '", _ai_response, "'");
			_render();
		}

		void _submit_prompt()
		{
			if (_prompt_len == 0) return;
			log("HermOS submitting prompt: '", static_cast<char const *>(_prompt), "'");
			(void)_ui_intent.generate_xml([&] (Xml_generator &g) {
				g.attribute("prompt", _prompt);
			});
			_prompt_len = 0;
			_prompt[0] = 0;
			_dirty = true;
		}

		/* render the AI reply as a small panel at the bottom of the screen.
		 * Only drawn when there is actual response text. */
		void _draw_ai_response(Surface<Pixel> &surface)
		{
			if (!_ai_response.valid()) return;
			if (!_ai_response.string()[0]) return;   /* no text -> nothing to draw */

			/* small panel: W=400, H=150, pinned to the bottom centre */
			int const pw = 400, ph = 150;
			int const px = _w / 2 - pw / 2;
			int const py = _h - ph - 20;

			_fill_rrect_glass(px, py, pw, ph, 12, 22, 32, 44, 228);
			_stroke_rrect(px, py, pw, ph, 12, _accent_r, _accent_g, _accent_b);
			_draw_text(surface, px + 16, py + 16, _solar(), "Hermes AI");

			char const *src = _ai_response.string();
			char        line[64];
			int         y = py + 40;

			while (*src && y < py + ph - 14) {
				int i = 0, last = -1;
				while (src[i] && i < 58) { if (src[i] == ' ') last = i; ++i; }
				int const cut = (src[i] && last > 0) ? last : i;
				if (cut <= 0) break;
				for (int j = 0; j < cut; ++j) line[j] = src[j];
				line[cut] = 0;
				_draw_text(surface, px + 16, y, _white(), line);
				src += cut;
				while (*src == ' ') ++src;
				y += 18;
			}
		}

		/* ---- HermOS installer: a 9-step full-screen wizard ---- */

	int _icx(float px, char const *t)
	{ return _w / 2 - _font.text_width(px * 1.2f, t) / 2; }

	void _installer_nav(Surface<Pixel> &s, bool show_back, char const *next, bool next_accent)
	{
		if (show_back) _button(s, _w / 2 - 360, _iby(), 120, 36, "Back", false);
		_button(s, _w / 2 + 240, _iby(), 180, 36, next, next_accent);
	}

	bool _ihit_back(int x, int y) const
	{ return x >= _w/2 - 360 && x < _w/2 - 240 && y >= _iby() && y < _iby() + 36; }
	bool _ihit_next(int x, int y) const
	{ return x >= _w/2 + 240 && x < _w/2 + 420 && y >= _iby() && y < _iby() + 36; }
	bool _ihit_confirm(int x, int y) const
	{ return x >= _w/2 - 280 && x < _w/2 - 262 && y >= 540 && y < 558; }
	bool _ihit_minus(int x, int y) const
	{ return x >= _w/2 - 240 && x < _w/2 - 196 && y >= 342 && y < 378; }
	bool _ihit_plus(int x, int y) const
	{ return x >= _w/2 + 196 && x < _w/2 + 240 && y >= 342 && y < 378; }
	bool _ihit_mykee(int x, int y) const
	{ return x >= _w/2 - 280 && x < _w/2 + 280 && y >= 450 && y < 498; }

	void _regenerate_mykee()
	{
		_mykee_seq += 47;
		Genode::String<48> k { "MK-V3-", (unsigned)_mykee_seq, "-7A4B" };
		_mykee_key = k;
	}

	void _installer_click(int x, int y)
	{
		/* V3 3-step installer: 0 = INST_SETUP, 1 = INST_ACTION, 2 = INST_COMPLETE */
		if (_installer_step == 0) {
			if (_ihit_minus(x, y)) { if (_alloc_gb > 1)      { --_alloc_gb; _dirty = true; } return; }
			if (_ihit_plus(x, y))  { if (_alloc_gb < _max_gb) { ++_alloc_gb; _dirty = true; } return; }
			if (_ihit_mykee(x, y)) { _regenerate_mykee(); _dirty = true; return; }
			if (_ihit_confirm(x, y)) { _installer_confirm = !_installer_confirm; _dirty = true; return; }
			if (_ihit_next(x, y) && _installer_confirm) {
				_connect_block(); _install_start(); _installer_step = 1; _dirty = true;
				log("HermOS installer: INST_ACTION"); return;
			}
			if (_ihit_back(x, y)) { _installer_mode = false; _dirty = true;
			                        log("HermOS installer: cancelled -> desktop"); return; }
			return;
		}
		if (_installer_step == 1) return;   /* installing: timer-driven */
		if (_installer_step == 2) {
			if (_ihit_next(x, y) || _ihit_back(x, y)) { _installer_mode = false; _dirty = true;
			                                            log("HermOS installer: INST_COMPLETE -> desktop"); }
			return;
		}
	}

	/* ---- installer Cell geometry: 80% of the screen, centred ---- */
	int _iw()  const { return _w * 80 / 100; }
	int _ih()  const { return _h * 80 / 100; }
	int _ix()  const { return (_w - _iw()) / 2; }
	int _iy()  const { return (_h - _ih()) / 2; }
	int _iby() const { return _iy() + _ih() - 96; }   /* nav button row, inside the Cell */

	/* synchronous block I/O: read/write one byte range (must be block-aligned) */
	bool _blk_io(Block::Packet_descriptor::Opcode op, uint64_t byte_off, char *buf, size_t len)
	{
		size_t const bs = _blk_info.block_size;
		if (!bs || byte_off % bs || len % bs) return false;
		size_t const count = len / bs;
		Block::Packet_descriptor p = _block->alloc_packet(len);
		p = Block::Packet_descriptor(p, op, byte_off / bs, count);
		if (op == Block::Packet_descriptor::WRITE)
			Genode::memcpy(_block->tx()->packet_content(p), buf, len);
		_block->tx()->submit_packet(p);
		while (!_block->tx()->ack_avail())
			_env.ep().wait_and_dispatch_one_io_signal();
		p = _block->tx()->get_acked_packet();
		bool const ok = p.succeeded();
		if (ok && op == Block::Packet_descriptor::READ)
			Genode::memcpy(buf, _block->tx()->packet_content(p), len);
		_block->tx()->release_packet(p);
		return ok;
	}

	/* format the HermOS data partition (of `sectors` 512-byte sectors) as ext2.
	 * Computes block size, block groups, bitmaps, and inode tables dynamically so
	 * it works for any size from MiB up to TiB. */
	bool _ext2_format(uint64_t sectors)
	{
		using namespace Ext2;
		uint64_t const bytes = sectors * 512;

		uint32_t block_size; uint32_t log_bs;
		if      (bytes < 512ull*1024*1024)          { block_size = 1024; log_bs = 0; }
		else if (bytes < 4ull*1024*1024*1024)       { block_size = 2048; log_bs = 1; }
		else                                        { block_size = 4096; log_bs = 2; }

		uint64_t const total_blocks = bytes / block_size;
		uint32_t const bpg      = 8 * block_size;                       /* blocks per group */
		uint64_t const groups   = (total_blocks + bpg - 1) / bpg;       /* group count */
		uint32_t const inode_sz = 256;
		uint32_t const ipg      = bpg / 4;                              /* inodes per group */
		uint32_t const ib_bmap  = (ipg + 8*block_size - 1) / (8*block_size);   /* inode-bitmap blocks */
		uint32_t const it_blks  = (ipg * inode_sz + block_size - 1) / block_size; /* inode-table blocks */
		uint32_t const gdt_blks = (uint32_t)((groups * 32 + block_size - 1) / block_size);

		/* group-0 metadata layout (superblock at byte 1024; GDT at block 1 for >=2KiB, block 2 for 1KiB) */
		uint32_t const gdt_blk = (block_size == 1024) ? 2 : 1;
		uint32_t const bb0  = gdt_blk + gdt_blks;
		uint32_t const ib0  = bb0 + 1;
		uint32_t const it0  = ib0 + ib_bmap;
		uint32_t const root_blk = it0 + it_blks;   /* first data block = root directory */

		/* per-group metadata blocks (group 0 carries superblock + GDT + root too) */
		uint32_t const meta = 1 + ib_bmap + it_blks;
		uint64_t const used_blocks = (root_blk + 1) + (groups - 1) * meta;

		/* ---- 1. superblock (block 1) ---- */
		char *sb = (char *)_asset_heap.alloc(block_size);
		if (!sb) return false;
		memset(sb, 0, block_size);
		put32(sb + 0,  (uint32_t)(groups * ipg));      /* s_inodes_count */
		put32(sb + 4,  (uint32_t)total_blocks);        /* s_blocks_count */
		put32(sb + 8,  (uint32_t)(total_blocks / 20)); /* s_r_blocks_count (5%) */
		put32(sb + 12, (uint32_t)(total_blocks - used_blocks));          /* s_free_blocks_count */
		put32(sb + 16, (uint32_t)(groups * ipg - 2));                    /* s_free_inodes_count */
		put32(sb + 20, (block_size == 1024) ? 1 : 0);  /* s_first_data_block */
		put32(sb + 24, log_bs);                        /* s_log_block_size */
		put32(sb + 28, log_bs);                        /* s_log_frag_size */
		put32(sb + 32, bpg);                           /* s_blocks_per_group */
		put32(sb + 36, bpg);                           /* s_frags_per_group */
		put32(sb + 40, ipg);                           /* s_inodes_per_group */
		put16(sb + 54, 0xFFFF);                        /* s_max_mnt_count */
		put16(sb + 56, Ext2::MAGIC);                   /* s_magic */
		put16(sb + 58, 1);                             /* s_state = clean */
		put16(sb + 60, 1);                             /* s_errors = continue */
		put32(sb + 72, 0);                             /* s_creator_os = Linux */
		put32(sb + 76, 1);                             /* s_rev_level = dynamic */
		put32(sb + 84, 11);                            /* s_first_ino */
		put16(sb + 88, inode_sz);                      /* s_inode_size */
		put32(sb + 96, 0x2);                           /* s_feature_incompat = FILETYPE */
		if (!_blk_io(Block::Packet_descriptor::WRITE, _data_off + 1024, sb, 1024)) { _asset_heap.free(sb, block_size); return false; }
		_asset_heap.free(sb, block_size);

		/* ---- 2. group descriptor table (block 2) ---- */
		char *gdt = (char *)_asset_heap.alloc(gdt_blks * block_size);
		if (!gdt) return false;
		memset(gdt, 0, gdt_blks * block_size);
		for (uint64_t g = 0; g < groups; ++g) {
			uint64_t const gstart = g * bpg;
			uint32_t const bb  = (g == 0) ? bb0 : (uint32_t)gstart;
			uint32_t const ib  = bb + 1;
			uint32_t const it  = ib + ib_bmap;
			uint64_t const gblocks = (gstart + bpg < total_blocks) ? bpg : (total_blocks - gstart);
			uint64_t const gused   = (g == 0) ? (root_blk + 1) : meta;
			uint32_t const gfree   = (uint32_t)(gblocks - gused);
			uint16_t const ifree   = (uint16_t)((g == 0) ? (ipg - 2) : ipg);
			char *d = gdt + g * 32;
			put32(d + 0, bb);  put32(d + 4, ib);  put32(d + 8, it);
			put16(d + 12, gfree);  put16(d + 14, ifree);  put16(d + 16, (g == 0) ? 1 : 0);
		}
		if (!_blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)gdt_blk * block_size, gdt, gdt_blks * block_size)) { _asset_heap.free(gdt, gdt_blks * block_size); return false; }
		_asset_heap.free(gdt, gdt_blks * block_size);

		/* ---- 3. per-group block bitmaps + inode bitmaps ---- */
		char *bb = (char *)_asset_heap.alloc(block_size);
		char *ib = (char *)_asset_heap.alloc(block_size);
		if (!bb || !ib) { if (bb) _asset_heap.free(bb, block_size); if (ib) _asset_heap.free(ib, block_size); return false; }

		for (uint64_t g = 0; g < groups; ++g) {
			uint64_t const gstart = g * bpg;
			uint32_t const bb_blk = (g == 0) ? bb0 : (uint32_t)gstart;
			uint32_t const ib_blk = bb_blk + 1;

			uint64_t const gblocks = (gstart + bpg < total_blocks) ? bpg : (total_blocks - gstart);
			uint64_t const gused   = (g == 0) ? (root_blk + 1) : meta;

			/* block bitmap: mark the metadata blocks as used */
			memset(bb, 0, block_size);
			for (uint64_t b = 0; b < gused && b < gblocks; ++b)
				bb[b >> 3] |= (char)(1 << (b & 7));
			if (!_blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)bb_blk * block_size, bb, block_size)) { _asset_heap.free(bb, block_size); _asset_heap.free(ib, block_size); return false; }

			/* inode bitmap: group 0 uses inodes 1 (bad) + 2 (root) */
			memset(ib, 0, block_size);
			if (g == 0) ib[0] = 0x06;
			if (!_blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)ib_blk * block_size, ib, block_size)) { _asset_heap.free(bb, block_size); _asset_heap.free(ib, block_size); return false; }
		}
		_asset_heap.free(bb, block_size);
		_asset_heap.free(ib, block_size);

		/* ---- 4. group-0 inode table: inode 2 = root directory ---- */
		char *it = (char *)_asset_heap.alloc(it_blks * block_size);
		if (!it) return false;
		memset(it, 0, it_blks * block_size);
		char *root = it + (Ext2::ROOT_INO - 1) * inode_sz;
		put16(root + 0, Ext2::S_IFDIR | Ext2::PERM755);  put32(root + 4, block_size);
		put16(root + 26, 2);                              put32(root + 28, 2);
		put32(root + 40, root_blk);
		bool ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)it0 * block_size, it, it_blks * block_size);
		_asset_heap.free(it, it_blks * block_size);
		if (!ok) return false;

		/* ---- 5. root directory ('.' + '..') at block root_blk ---- */
		char *dir = (char *)_asset_heap.alloc(block_size);
		if (!dir) return false;
		memset(dir, 0, block_size);
		put32(dir + 0,  Ext2::ROOT_INO);  put16(dir + 4, 12);  dir[6] = 1;  dir[7] = 2;  dir[8] = '.';
		put32(dir + 12, Ext2::ROOT_INO);  put16(dir + 16, (unsigned)block_size - 12);
		dir[18] = 2;  dir[19] = 2;  dir[20] = '.';  dir[21] = '.';
		ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)root_blk * block_size, dir, block_size);
		_asset_heap.free(dir, block_size);
		if (!ok) return false;

		log("HermOS installer: ext2 formatted (", (unsigned)total_blocks, " blocks, ",
		    (unsigned)groups, " groups, ", (unsigned)block_size, "-byte blocks)");
		return true;
	}

	/* ---- Step 3: raw file extraction ---- */

	/* read `len` bytes from the source image ROM at `off` */
	bool _src_read(void *dst, uint64_t off, size_t len)
	{
		if (off + len > _system_img.size()) return false;
		Genode::memcpy(dst, _system_img.local_addr<char>() + off, len);
		return true;
	}

	/* locate the source image's ext2 partition (its GPT 0x8300 entry) */
	bool _src_ext2_offset(uint64_t &ext2_off)
	{
		using namespace Ext2;
		char hdr[512];
		if (!_src_read(hdr, 512, 512)) return false;
		char const sig[8] = { 'E','F','I',' ','P','A','R','T' };
		for (int i = 0; i < 8; ++i) if (hdr[i] != sig[i]) return false;
		uint64_t const entries_lba = get64(hdr + 72);
		char entries[128 * 128];
		if (!_src_read(entries, entries_lba * 512, 128 * 128)) return false;
		uint8_t const data_type[16] = { 0xaf,0x3d,0xc6,0x0f, 0x83,0x84, 0x72,0x47,
		                                0x8e,0x79, 0x3d,0x69, 0xd8,0x47, 0x7d,0xe4 };
		for (int e = 0; e < 128; ++e) {
			char const *en = entries + e * 128;
			bool match = true;
			for (int i = 0; i < 16 && match; ++i) match = ((uint8_t)en[i] == data_type[i]);
			if (match) { ext2_off = get64(en + 32) * 512; return true; }
		}
		return false;
	}

	/* read an ext2 block from the source image */
	bool _e2_read_src_block(uint64_t ext2_off, uint32_t bs, uint32_t blk, char *out)
	{
		return _src_read(out, ext2_off + (uint64_t)blk * bs, bs);
	}

	/* find `name` in directory `dir_ino`, returning its inode */
	bool _e2_find_in_dir(uint64_t ext2_off, uint32_t bs, uint32_t itable_blk, uint32_t ino_sz,
	                     uint32_t dir_ino, char const *name, size_t name_len, uint32_t *out_ino)
	{
		using namespace Ext2;
		char ino[256];
		if (!_src_read(ino, ext2_off + (uint64_t)itable_blk * bs + (uint64_t)(dir_ino - 1) * ino_sz, ino_sz)) return false;
		uint32_t const dir_blk = get32(ino + 40);
		char *dir = (char *)_asset_heap.alloc(bs);
		if (!dir) return false;
		bool found = false;
		if (_e2_read_src_block(ext2_off, bs, dir_blk, dir)) {
			uint32_t off = 0;
			while (off + 8 <= bs) {
				uint32_t const i = get32(dir + off);
				uint16_t const rl = get16(dir + off + 4);
				uint8_t  const nl = (uint8_t)dir[off + 6];
				if (rl == 0 || off + rl > bs) break;
				bool nm = (nl == name_len);
				for (int k = 0; k < (int)nl && nm; ++k) nm = (dir[off + 8 + k] == name[k]);
				if (nm && i) { *out_ino = i; found = true; break; }
				off += rl;
			}
		}
		_asset_heap.free(dir, bs);
		return found;
	}

	/* read a file (a '/' path within the source image's ext2) into a fresh heap buffer */
	bool _e2_read_path(char const *path, void **data, size_t *size)
	{
		using namespace Ext2;
		uint64_t ext2_off;
		if (!_src_ext2_offset(ext2_off)) { log("HermOS installer: read_path: no ext2 partition"); return false; }

		char sb[1024];
		if (!_src_read(sb, ext2_off + 1024, 1024)) { log("HermOS installer: read_path: superblock read failed"); return false; }
		if (get16(sb + 56) != MAGIC) { log("HermOS installer: read_path: bad magic ", (unsigned)get16(sb + 56)); return false; }
		uint32_t const bs = 1024u << get32(sb + 24);
		uint32_t const ino_sz = get16(sb + 88);

		uint32_t const gdt_blk = (bs == 1024) ? 2 : 1;
		char gd[32];
		if (!_src_read(gd, ext2_off + (uint64_t)gdt_blk * bs, 32)) { log("HermOS installer: read_path: gd read failed"); return false; }
		uint32_t const itable_blk = get32(gd + 8);
		log("HermOS installer: read_path ", path, " off=", (unsigned)ext2_off, " bs=", bs, " ino_sz=", ino_sz, " itable=", itable_blk);

		uint32_t ino = ROOT_INO;
		char const *p = path;
		while (*p) {
			char const *slash = p;
			while (*slash && *slash != '/') ++slash;
			size_t const clen = (size_t)(slash - p);
			uint32_t next = 0;
			if (!_e2_find_in_dir(ext2_off, bs, itable_blk, ino_sz, ino, p, clen, &next)) {
				log("HermOS installer: read_path: component not found in ino ", ino);
				return false;
			}
			ino = next;
			p = (*slash == '/') ? slash + 1 : slash;
		}

		char fi[256];
		if (!_src_read(fi, ext2_off + (uint64_t)itable_blk * bs + (uint64_t)(ino - 1) * ino_sz, ino_sz)) return false;
		uint32_t const fsize = get32(fi + 4);
		char *out = (char *)_asset_heap.alloc(fsize ? fsize : 1);
		if (!out) return false;

		/* copy one source block into `out`, advancing `copied` */
		auto copy_blk = [&](uint32_t blk, uint32_t *copied) -> bool {
			if (*copied >= fsize) return true;
			char *buf = (char *)_asset_heap.alloc(bs);
			if (!buf) return false;
			bool const ok = _e2_read_src_block(ext2_off, bs, blk, buf);
			if (ok) {
				uint32_t const n = (fsize - *copied < bs) ? (fsize - *copied) : bs;
				Genode::memcpy(out + *copied, buf, n);
				*copied += n;
			}
			_asset_heap.free(buf, bs);
			return ok;
		};

		uint32_t copied = 0;
		/* direct blocks */
		for (int b = 0; b < 12 && copied < fsize; ++b) {
			uint32_t const blk = get32(fi + 40 + 4*b);
			if (!blk) break;
			if (!copy_blk(blk, &copied)) { _asset_heap.free(out, fsize ? fsize : 1); return false; }
		}
		/* single indirect */
		uint32_t const sin = get32(fi + 40 + 48);
		if (sin && copied < fsize) {
			char *sd = (char *)_asset_heap.alloc(bs);
			if (!sd) { _asset_heap.free(out, fsize ? fsize : 1); return false; }
			bool ok = _e2_read_src_block(ext2_off, bs, sin, sd);
			if (ok) {
				for (uint32_t i = 0; i < bs / 4 && copied < fsize; ++i) {
					uint32_t const blk = get32(sd + 4*i);
					if (!blk) break;
					if (!copy_blk(blk, &copied)) { ok = false; break; }
				}
			}
			_asset_heap.free(sd, bs);
			if (!ok) { _asset_heap.free(out, fsize ? fsize : 1); return false; }
		}
		/* double indirect */
		uint32_t const din = get32(fi + 40 + 52);
		if (din && copied < fsize) {
			char *dd = (char *)_asset_heap.alloc(bs);
			if (!dd) { _asset_heap.free(out, fsize ? fsize : 1); return false; }
			bool ok = _e2_read_src_block(ext2_off, bs, din, dd);
			if (ok) {
				for (uint32_t j = 0; j < bs / 4 && copied < fsize && ok; ++j) {
					uint32_t const sb = get32(dd + 4*j);
					if (!sb) break;
					char *sd = (char *)_asset_heap.alloc(bs);
					if (!sd) { ok = false; break; }
					bool ok2 = _e2_read_src_block(ext2_off, bs, sb, sd);
					if (ok2) {
						for (uint32_t i = 0; i < bs / 4 && copied < fsize; ++i) {
							uint32_t const blk = get32(sd + 4*i);
							if (!blk) break;
							if (!copy_blk(blk, &copied)) { ok2 = false; break; }
						}
					}
					_asset_heap.free(sd, bs);
					if (!ok2) ok = false;
				}
			}
			_asset_heap.free(dd, bs);
			if (!ok) { _asset_heap.free(out, fsize ? fsize : 1); return false; }
		}
		*data = out;
		*size = fsize;
		return true;
	}

	/* append a directory entry to an in-memory directory */
	void _e2_add_dir_entry(char *dir, size_t *used, uint32_t ino, uint8_t ftype, char const *name)
	{
		using namespace Ext2;
		size_t const name_len = Genode::strlen(name);
		size_t const entry_len = (8 + name_len + 3) & ~(size_t)3;
		put32(dir + *used, ino);
		put16(dir + *used + 4, (unsigned)entry_len);
		dir[*used + 6] = (char)name_len;
		dir[*used + 7] = (char)ftype;
		Genode::memcpy(dir + *used + 8, name, name_len);
		*used += entry_len;
	}

	/* extend a directory's last entry rec_len to the end of the block */
	void _e2_finalize_dir(char *dir, size_t used)
	{
		using namespace Ext2;
		size_t off = 0, last = 0;
		while (off < used) {
			last = off;
			size_t const nl = (uint8_t)dir[off + 6];
			off += (8 + nl + 3) & ~(size_t)3;
		}
		put16(dir + last + 4, (unsigned)(_e2_bs - last));
	}

	/* create a sub-directory (inode + dir block + parent entry) */
	bool _e2_mkdir(uint32_t child_ino, uint32_t child_blk, uint32_t parent_ino,
	               char *parent, size_t *parent_used, char **child_out, size_t *child_used_out,
	               char const *name)
	{
		using namespace Ext2;
		uint32_t const bs = _e2_bs;
		char *cino = _e2_itable + (child_ino - 1) * 256;
		put16(cino + 0, S_IFDIR | PERM755);  put32(cino + 4, bs);
		put16(cino + 26, 2);                 put32(cino + 28, 2);
		put32(cino + 40, child_blk);

		char *child = (char *)_asset_heap.alloc(bs);
		if (!child) return false;
		memset(child, 0, bs);
		put32(child + 0,  child_ino);  put16(child + 4, 12);  child[6] = 1;  child[7] = 2;  child[8] = '.';
		put32(child + 12, parent_ino); put16(child + 16, 12); child[18] = 2; child[19] = 2; child[20] = '.'; child[21] = '.';
		*child_out = child;
		*child_used_out = 24;

		_e2_add_dir_entry(parent, parent_used, child_ino, 2, name);
		return true;
	}

	/* compute the target ext2 geometry + stage the root + boot + grub directories */
	bool _e2_prepare(uint64_t sectors)
	{
		using namespace Ext2;
		uint64_t const bytes = sectors * 512;
		if      (bytes < 512ull*1024*1024)    _e2_bs = 1024;
		else if (bytes < 4ull*1024*1024*1024) _e2_bs = 2048;
		else                                  _e2_bs = 4096;
		uint32_t const bs = _e2_bs;
		uint32_t const bpg = 8 * bs;
		uint64_t const groups = (bytes / bs + bpg - 1) / bpg;
		uint32_t const ipg = bpg / 4;
		uint32_t const ib_bmap = (ipg + 8*bs - 1) / (8*bs);
		_e2_it_blks = (ipg * 256 + bs - 1) / bs;
		uint32_t const gdt_blks = (uint32_t)((groups * 32 + bs - 1) / bs);
		uint32_t const gdt_blk = (bs == 1024) ? 2 : 1;
		_e2_it0 = gdt_blk + gdt_blks + 1 + ib_bmap;
		_e2_root_blk = _e2_it0 + _e2_it_blks;

		_e2_itable = (char *)_asset_heap.alloc(_e2_it_blks * bs);
		if (!_e2_itable) return false;
		memset(_e2_itable, 0, _e2_it_blks * bs);

		/* root directory (inode 2, block _e2_root_blk) */
		char *root = _e2_itable + (ROOT_INO - 1) * 256;
		put16(root + 0, S_IFDIR | PERM755);  put32(root + 4, bs);
		put16(root + 26, 2);                 put32(root + 28, 2);
		put32(root + 40, _e2_root_blk);
		_e2_rootdir = (char *)_asset_heap.alloc(bs);
		if (!_e2_rootdir) return false;
		memset(_e2_rootdir, 0, bs);
		put32(_e2_rootdir + 0,  ROOT_INO);  put16(_e2_rootdir + 4, 12);
		_e2_rootdir[6] = 1;  _e2_rootdir[7] = 2;  _e2_rootdir[8] = '.';
		put32(_e2_rootdir + 12, ROOT_INO);  put16(_e2_rootdir + 16, 12);
		_e2_rootdir[18] = 2;  _e2_rootdir[19] = 2;  _e2_rootdir[20] = '.';  _e2_rootdir[21] = '.';
		_e2_dir_used = 24;

		/* boot directory (inode 3, block _e2_root_blk + 1) */
		if (!_e2_mkdir(3, _e2_root_blk + 1, ROOT_INO, _e2_rootdir, &_e2_dir_used,
		               &_e2_bootdir, &_e2_boot_used, "boot")) return false;

		/* grub directory (inode 4, block _e2_root_blk + 2) inside boot */
		if (!_e2_mkdir(4, _e2_root_blk + 2, 3, _e2_bootdir, &_e2_boot_used,
		               &_e2_grubdir, &_e2_grub_used, "grub")) return false;

		_e2_next_ino = 5;
		_e2_next_blk = _e2_root_blk + 3;
		return true;
	}

	/* write a file into a target ext2 directory (sequential inode + block allocation) */
	bool _e2_write_file_into(char *dir, size_t *dir_used, char const *name, void const *data, size_t size)
	{
		using namespace Ext2;
		uint32_t const bs = _e2_bs;
		uint32_t const nblk = (uint32_t)((size + bs - 1) / bs);
		uint32_t const base = _e2_next_blk;

		/* metadata block layout for the inode's i_block[] (direct + single + double indirect) */
		uint32_t const n_direct = (nblk < 12) ? nblk : 12;
		uint32_t const n_single = (nblk > 12) ? ((nblk - 12 < 1024) ? (nblk - 12) : 1024) : 0;
		uint32_t const n_double = (nblk > 12 + 1024) ? (nblk - 12 - 1024) : 0;
		uint32_t const n_dblk_single = (n_double + 1023) / 1024;
		uint32_t const single_blk = (n_single) ? (base + nblk) : 0;
		uint32_t const double_blk = (n_double) ? (base + nblk + 1) : 0;
		uint32_t const meta = (n_single ? 1 : 0) + (n_double ? 1 + n_dblk_single : 0);

		/* data blocks (256 KiB batches) */
		size_t const total = (size_t)nblk * bs;
		size_t off = 0;
		while (off < total) {
			size_t const n = (total - off < 256*1024) ? (total - off) : 256*1024;
			char *blk = (char *)_asset_heap.alloc(n);
			if (!blk) return false;
			memset(blk, 0, n);
			size_t const copy = (size - off < n) ? (size - off) : n;
			if (copy) Genode::memcpy(blk, (char const *)data + off, copy);
			bool const ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)base * bs + off, blk, n);
			_asset_heap.free(blk, n);
			if (!ok) return false;
			off += n;
		}

		/* single-indirect block */
		if (n_single) {
			char *s = (char *)_asset_heap.alloc(bs);
			if (!s) return false;
			memset(s, 0, bs);
			for (uint32_t i = 0; i < n_single; ++i)
				put32(s + 4*i, base + 12 + i);
			bool const ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)single_blk * bs, s, bs);
			_asset_heap.free(s, bs);
			if (!ok) return false;
		}

		/* double-indirect block + its single-indirect blocks */
		if (n_double) {
			char *d = (char *)_asset_heap.alloc(bs);
			if (!d) return false;
			memset(d, 0, bs);
			for (uint32_t j = 0; j < n_dblk_single; ++j) {
				uint32_t const sblk = double_blk + 1 + j;
				put32(d + 4*j, sblk);
				char *s = (char *)_asset_heap.alloc(bs);
				if (!s) { _asset_heap.free(d, bs); return false; }
				memset(s, 0, bs);
				uint32_t const in_this = (n_double - j*1024 < 1024) ? (n_double - j*1024) : 1024;
				for (uint32_t i = 0; i < in_this; ++i)
					put32(s + 4*i, base + 12 + 1024 + j*1024 + i);
				bool ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)sblk * bs, s, bs);
				_asset_heap.free(s, bs);
				if (!ok) { _asset_heap.free(d, bs); return false; }
			}
			bool const ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)double_blk * bs, d, bs);
			_asset_heap.free(d, bs);
			if (!ok) return false;
		}

		char *ino = _e2_itable + (_e2_next_ino - 1) * 256;
		put16(ino + 0, S_IFREG | PERM755);
		put32(ino + 4, (uint32_t)size);
		put16(ino + 26, 1);
		put32(ino + 28, (nblk + meta) * (bs / 512));
		for (uint32_t i = 0; i < n_direct; ++i)
			put32(ino + 40 + 4*i, base + i);
		if (n_single) put32(ino + 40 + 4*12, single_blk);
		if (n_double) put32(ino + 40 + 4*13, double_blk);

		_e2_add_dir_entry(dir, dir_used, _e2_next_ino, 1, name);

		_e2_next_ino += 1;
		_e2_next_blk += nblk + meta;
		return true;
	}

	/* flush the staged inode table + root/boot/grub directories to the target */
	bool _e2_flush()
	{
		using namespace Ext2;
		_e2_finalize_dir(_e2_rootdir, _e2_dir_used);
		_e2_finalize_dir(_e2_bootdir, _e2_boot_used);
		_e2_finalize_dir(_e2_grubdir, _e2_grub_used);

		bool ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)_e2_it0 * _e2_bs, _e2_itable, _e2_it_blks * _e2_bs);
		if (ok) ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)_e2_root_blk * _e2_bs, _e2_rootdir, _e2_bs);
		if (ok) ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)(_e2_root_blk + 1) * _e2_bs, _e2_bootdir, _e2_bs);
		if (ok) ok = _blk_io(Block::Packet_descriptor::WRITE, _data_off + (uint64_t)(_e2_root_blk + 2) * _e2_bs, _e2_grubdir, _e2_bs);
		return ok;
	}

	/* extract the boot files + write them into /boot and /boot/grub */
	bool _extract_boot_files()
	{
		using namespace Ext2;
		char const *paths[6] = { "boot/image.elf.gz", "boot/bender", "boot/hypervisor",
		                         "boot/font.pf2", "boot/boot.png", "boot/grub/grub.cfg" };
		char const *names[6] = { "image.elf.gz", "bender", "hypervisor", "font.pf2", "boot.png", "grub.cfg" };
		char   *dirs[6]   = { _e2_bootdir, _e2_bootdir, _e2_bootdir, _e2_bootdir, _e2_bootdir, _e2_grubdir };
		size_t *useds[6]  = { &_e2_boot_used, &_e2_boot_used, &_e2_boot_used, &_e2_boot_used, &_e2_boot_used, &_e2_grub_used };
		for (int i = 0; i < 6; ++i) {
			void *data = nullptr; size_t size = 0;
			if (!_e2_read_path(paths[i], &data, &size)) {
				log("HermOS installer: EXTRACT FAILED ", paths[i]); return false;
			}
			bool const ok = _e2_write_file_into(dirs[i], useds[i], names[i], data, size);
			_asset_heap.free(data, size ? size : 1);
			if (!ok) return false;
			log("HermOS installer: extracted ", names[i], " (", (unsigned)size, " bytes)");
		}
		return _e2_flush();
	}

	/* ---- Step 4.2: EFI partition (FAT32) ---- */

	/* locate the source image's EFI system partition (its GPT EF00 entry) */
	bool _src_efi_offset(uint64_t &efi_off)
	{
		using namespace Ext2;
		char hdr[512];
		if (!_src_read(hdr, 512, 512)) return false;
		char const sig[8] = { 'E','F','I',' ','P','A','R','T' };
		for (int i = 0; i < 8; ++i) if (hdr[i] != sig[i]) return false;
		uint64_t const entries_lba = get64(hdr + 72);
		char entries[128 * 128];
		if (!_src_read(entries, entries_lba * 512, 128 * 128)) return false;
		uint8_t const efi_type[16] = { 0x28,0x73,0x2a,0xc1, 0x1f,0xf8, 0xd2,0x11,
		                               0xba,0x4b, 0x00,0xa0, 0xc9,0x3e, 0xc9,0x3b };
		for (int e = 0; e < 128; ++e) {
			char const *en = entries + e * 128;
			bool match = true;
			for (int i = 0; i < 16 && match; ++i) match = ((uint8_t)en[i] == efi_type[i]);
			if (match) { efi_off = get64(en + 32) * 512; return true; }
		}
		return false;
	}

	/* extract \EFI\boot\bootx64.efi from the source image's FAT12/16 EFI partition */
	bool _read_bootx64(void **data, size_t *size)
	{
		using namespace Ext2;
		uint64_t efi_off;
		if (!_src_efi_offset(efi_off)) { log("HermOS installer: bootx64: no EFI partition"); return false; }

		char bpb[512];
		if (!_src_read(bpb, efi_off, 512)) return false;
		uint32_t const bps     = get16(bpb + 11);
		uint32_t const spc     = (uint8_t)bpb[13];
		uint32_t const reserved = get16(bpb + 14);
		uint32_t const nfats   = (uint8_t)bpb[16];
		uint32_t const root_entries = get16(bpb + 17);
		uint32_t const spf     = get16(bpb + 22);
		uint32_t const root_bytes = root_entries * 32;
		uint32_t const data_start = reserved + nfats * spf + (root_bytes + bps - 1) / bps;

		auto cluster_off = [&](uint32_t cl) { return efi_off + (uint64_t)(data_start + (cl - 2) * spc) * bps; };
		auto fat_next = [&](uint32_t cl) -> uint32_t {
			uint32_t const byte_off = cl * 3 / 2;
			char e[2];
			if (!_src_read(e, efi_off + (uint64_t)reserved * bps + byte_off, 2)) return 0xFFF;
			uint16_t const v = get16(e);
			uint32_t const next = (cl & 1) ? (v >> 4) : (v & 0xFFF);
			return (next >= 0xFF8) ? 0xFFF : next;
		};
		auto find_at = [&](uint64_t dir_off, char const name[11], uint32_t *out_cl, uint32_t *out_size) -> bool {
			char *dir = (char *)_asset_heap.alloc((size_t)bps * spc);
			if (!dir) return false;
			bool found = false;
			if (_src_read(dir, dir_off, (size_t)bps * spc)) {
				for (uint32_t off = 0; off + 32 <= (uint32_t)bps * spc; off += 32) {
					if (dir[off] == 0x00) break;
					if ((uint8_t)dir[off] == 0xE5) continue;
					bool nm = true;
					for (int k = 0; k < 11 && nm; ++k) {
						char a = dir[off + k], b = name[k];
						if (a >= 'a' && a <= 'z') a -= 32;
						if (b >= 'a' && b <= 'z') b -= 32;
						nm = (a == b);
					}
					if (nm) {
						uint32_t const hi = get16(dir + off + 20);
						uint32_t const lo = get16(dir + off + 26);
						*out_cl = (hi << 16) | lo;
						*out_size = get32(dir + off + 28);
						found = true;
						break;
					}
				}
			}
			_asset_heap.free(dir, (size_t)bps * spc);
			return found;
		};

		char const n_efi[11]  = { 'E','F','I',' ',' ',' ',' ',' ',' ',' ',' ' };
		char const n_boot[11] = { 'B','O','O','T',' ',' ',' ',' ',' ',' ',' ' };
		char const n_file[11] = { 'B','O','O','T','X','6','4',' ','E','F','I' };

		uint32_t cl, fsize;
		if (!find_at(efi_off + (uint64_t)(reserved + nfats * spf) * bps, n_efi, &cl, &fsize)) { log("HermOS installer: bootx64: EFI not found"); return false; }
		if (!find_at(cluster_off(cl), n_boot, &cl, &fsize)) { log("HermOS installer: bootx64: BOOT not found"); return false; }
		if (!find_at(cluster_off(cl), n_file, &cl, &fsize)) { log("HermOS installer: bootx64: BOOTX64.EFI not found"); return false; }

		char *out = (char *)_asset_heap.alloc(fsize ? fsize : 1);
		if (!out) return false;
		uint32_t copied = 0;
		uint32_t cur = cl;
		for (int depth = 0; depth < 4096 && copied < fsize && cur >= 2 && cur < 0xFF0; ++depth) {
			char *buf = (char *)_asset_heap.alloc((size_t)bps * spc);
			if (!buf) { _asset_heap.free(out, fsize ? fsize : 1); return false; }
			bool const ok = _src_read(buf, cluster_off(cur), (size_t)bps * spc);
			if (ok) {
				uint32_t const n = (fsize - copied < (uint32_t)bps * spc) ? (fsize - copied) : (uint32_t)bps * spc;
				Genode::memcpy(out + copied, buf, n);
				copied += n;
			}
			_asset_heap.free(buf, (size_t)bps * spc);
			if (!ok) { _asset_heap.free(out, fsize ? fsize : 1); return false; }
			cur = fat_next(cur);
		}
		*data = out;
		*size = fsize;
		return true;
	}

	/* fill a 32-byte FAT32 directory entry */
	void _fat32_dirent(char *d, char const name[11], uint8_t attr, uint32_t cluster, uint32_t size)
	{
		using namespace Ext2;
		memset(d, 0, 32);
		Genode::memcpy(d, name, 11);
		d[11] = (char)attr;
		put16(d + 20, (unsigned)(cluster >> 16));
		put16(d + 26, (unsigned)(cluster & 0xFFFF));
		put32(d + 28, size);
	}

	/* format the EFI partition as FAT32 + place bootx64.efi into \EFI\boot\ */
	bool _fat32_format(uint64_t efi_sector, uint64_t efi_sectors, void const *bootx64, size_t bootx64_size)
	{
		using namespace Ext2;
		uint32_t const bps = 512;
		uint32_t const spc = 1;
		uint32_t const reserved = 32;
		uint32_t const nfats = 2;

		uint32_t spf = 1;
		for (;;) {
			uint32_t const clusters = (uint32_t)((efi_sectors - reserved - 2*spf) / spc);
			uint32_t const new_spf = (clusters * 4 + bps - 1) / bps;
			if (new_spf == spf) break;
			spf = new_spf;
		}
		uint32_t const data_start = reserved + nfats * spf;
		uint32_t const file_clusters = (uint32_t)((bootx64_size + bps*spc - 1) / (bps*spc));

		/* cluster layout: 2=root 3=EFI 4=BOOT 5..=bootx64, then \boot\ (dir), \boot\grub\ (dir), grub.cfg */
		uint32_t const boot_dir_cl = 5 + file_clusters;
		uint32_t const grub_dir_cl = boot_dir_cl + 1;
		uint32_t const cfg_cl      = grub_dir_cl + 1;
		char const grub_cfg[] = "set root=(hd0,gpt2)\nconfigfile /boot/grub/grub.cfg\n";
		uint32_t const grub_cfg_size = (uint32_t)(sizeof(grub_cfg) - 1);

		auto write_at = [&](uint32_t rel_sector, void const *buf, size_t len) -> bool {
			return _blk_io(Block::Packet_descriptor::WRITE, (uint64_t)(efi_sector + rel_sector) * bps, (char *)buf, len);
		};

		/* boot sector (BPB) */
		char bpb[512];
		memset(bpb, 0, 512);
		bpb[0] = 0xEB; bpb[1] = 0x3C; bpb[2] = 0x90;
		Genode::memcpy(bpb + 3, "MSDOS5.0", 8);
		put16(bpb + 11, bps);
		bpb[13] = (char)spc;
		put16(bpb + 14, reserved);
		bpb[16] = (char)nfats;
		bpb[21] = 0xF8;
		put16(bpb + 24, 63);
		put16(bpb + 26, 255);
		put32(bpb + 28, (uint32_t)efi_sector);
		put32(bpb + 32, (uint32_t)efi_sectors);
		put32(bpb + 36, spf);
		put32(bpb + 44, 2);          /* root cluster */
		put16(bpb + 48, 1);          /* FSInfo sector */
		put16(bpb + 50, 6);          /* backup boot sector */
		bpb[64] = 0x80;
		bpb[66] = 0x29;
		put32(bpb + 67, 0x12345678);
		Genode::memcpy(bpb + 71, "NO NAME    ", 11);
		Genode::memcpy(bpb + 82, "FAT32   ", 8);
		bpb[510] = 0x55; bpb[511] = 0xAA;
		if (!write_at(0, bpb, 512)) return false;

		/* FSInfo (sector 1) */
		char fsi[512];
		memset(fsi, 0, 512);
		put32(fsi + 0,   0x41615252);
		put32(fsi + 484, 0x61417272);
		put32(fsi + 488, 0xFFFFFFFF);
		put32(fsi + 492, 0xFFFFFFFF);
		put32(fsi + 508, 0xAA550000);
		if (!write_at(1, fsi, 512)) return false;

		/* FAT (2 copies): root/Efi/Boot dirs EOC, then the file chain */
		char *fat = (char *)_asset_heap.alloc(spf * bps);
		if (!fat) return false;
		memset(fat, 0, spf * bps);
		put32(fat + 0, 0x0FFFFFF8);
		put32(fat + 4, 0x0FFFFFFF);
		put32(fat + 8, 0x0FFFFFFF);
		put32(fat + 12, 0x0FFFFFFF);
		put32(fat + 16, 0x0FFFFFFF);
		for (uint32_t i = 0; i < file_clusters; ++i)
			put32(fat + (5 + i) * 4, (i == file_clusters - 1) ? 0x0FFFFFFF : (6 + i));
		put32(fat + boot_dir_cl * 4, 0x0FFFFFFF);   /* \boot\ dir */
		put32(fat + grub_dir_cl * 4, 0x0FFFFFFF);   /* \boot\grub\ dir */
		put32(fat + cfg_cl      * 4, 0x0FFFFFFF);   /* grub.cfg file (1 cluster) */
		bool ok = write_at(reserved, fat, spf * bps);
		if (ok) ok = write_at(reserved + spf, fat, spf * bps);
		_asset_heap.free(fat, spf * bps);
		if (!ok) return false;

		/* directories: root -> EFI(3) + boot, EFI -> BOOT(4) -> BOOTX64.EFI(5) */
		char const n_efi[11]  = { 'E','F','I',' ',' ',' ',' ',' ',' ',' ',' ' };
		char const n_boot[11] = { 'B','O','O','T',' ',' ',' ',' ',' ',' ',' ' };
		char const n_file[11] = { 'B','O','O','T','X','6','4',' ','E','F','I' };
		char const n_grub[11] = { 'G','R','U','B',' ',' ',' ',' ',' ',' ',' ' };
		char const n_cfg[11]  = { 'G','R','U','B',' ',' ',' ',' ','C','F','G' };

		char *root = (char *)_asset_heap.alloc(bps * spc);
		char *efi  = (char *)_asset_heap.alloc(bps * spc);
		char *boot = (char *)_asset_heap.alloc(bps * spc);
		char *bootdir = (char *)_asset_heap.alloc(bps * spc);
		char *grubdir = (char *)_asset_heap.alloc(bps * spc);
		if (!root || !efi || !boot || !bootdir || !grubdir) {
			if (root) _asset_heap.free(root, bps*spc); if (efi) _asset_heap.free(efi, bps*spc);
			if (boot) _asset_heap.free(boot, bps*spc);
			if (bootdir) _asset_heap.free(bootdir, bps*spc);
			if (grubdir) _asset_heap.free(grubdir, bps*spc);
			return false;
		}
		memset(root, 0, bps * spc);
		memset(efi, 0, bps * spc);
		memset(boot, 0, bps * spc);
		memset(bootdir, 0, bps * spc);
		memset(grubdir, 0, bps * spc);
		_fat32_dirent(root,      n_efi,  0x10, 3, 0);
		_fat32_dirent(root + 32, n_boot, 0x10, boot_dir_cl, 0);
		_fat32_dirent(efi,       n_boot, 0x10, 4, 0);
		_fat32_dirent(boot,      n_file, 0x20, 5, (uint32_t)bootx64_size);
		_fat32_dirent(bootdir,   n_grub, 0x10, grub_dir_cl, 0);
		_fat32_dirent(grubdir,   n_cfg,  0x20, cfg_cl, grub_cfg_size);
		ok = write_at(data_start + 0 * spc, root, bps * spc);      /* cluster 2 */
		if (ok) ok = write_at(data_start + 1 * spc, efi, bps * spc);  /* cluster 3 */
		if (ok) ok = write_at(data_start + 2 * spc, boot, bps * spc); /* cluster 4 */
		if (ok) ok = write_at(data_start + (boot_dir_cl - 2) * spc, bootdir, bps * spc); /* \boot\ */
		if (ok) ok = write_at(data_start + (grub_dir_cl - 2) * spc, grubdir, bps * spc); /* \boot\grub\ */
		_asset_heap.free(root, bps * spc);
		_asset_heap.free(efi, bps * spc);
		_asset_heap.free(boot, bps * spc);
		_asset_heap.free(bootdir, bps * spc);
		_asset_heap.free(grubdir, bps * spc);
		if (!ok) return false;

		/* the redirect grub.cfg (cluster cfg_cl) */
		char *cfg = (char *)_asset_heap.alloc(bps * spc);
		if (!cfg) return false;
		memset(cfg, 0, bps * spc);
		Genode::memcpy(cfg, grub_cfg, grub_cfg_size);
		ok = write_at(data_start + (cfg_cl - 2) * spc, cfg, bps * spc);
		_asset_heap.free(cfg, bps * spc);
		if (!ok) return false;

		/* the bootx64.efi file data (cluster 5 onward) */
		size_t off = 0;
		while (off < bootx64_size) {
			size_t const n = (bootx64_size - off < 256*1024) ? (bootx64_size - off) : 256*1024;
			size_t const padded = (n + bps*spc - 1) / (bps*spc) * (bps*spc);
			char *blk = (char *)_asset_heap.alloc(padded);
			if (!blk) return false;
			memset(blk, 0, padded);
			Genode::memcpy(blk, (char const *)bootx64 + off, n);
			bool const w = write_at(data_start + 3 * spc + (uint32_t)(off / (bps*spc)), blk, padded);
			_asset_heap.free(blk, padded);
			if (!w) return false;
			off += n;
		}
		return true;
	}

	void _install_start()
	{
		_install_phase = INST_FORMATTING;
		_install_pct = 0;
		log("HermOS installer: install started (dynamic ", (unsigned)_alloc_gb, " GB)");
	}

	/* write a fresh GPT from scratch: protective MBR + primary/backup GPT headers
	 * + partition entries for an EFI system partition (200 MiB) and a HermOS data
	 * partition of alloc_gb gigabytes. Returns false if alloc_gb does not fit. */
	bool _write_gpt(uint64_t alloc_gb)
	{
		using namespace Ext2;
		uint64_t const total = _blk_info.block_count;               /* sectors on the disk */
		uint64_t const ss    = _blk_info.block_size;                /* 512 */
		uint64_t const efi_sectors = (200ull * 1024 * 1024) / ss;   /* 200 MiB */
		uint64_t const gb_sectors  = (1024ull * 1024 * 1024) / ss;  /* 1 GiB  */

		uint64_t const last_usable  = total - 34;
		uint64_t const efi_first    = 34;
		uint64_t const efi_last     = efi_first + efi_sectors - 1;
		uint64_t const data_first   = efi_last + 1;
		uint64_t const data_sectors = alloc_gb * gb_sectors;
		uint64_t const data_last    = data_first + data_sectors - 1;

		if (data_last > last_usable) return false;   /* alloc_gb exceeds the disk */

		/* --- protective MBR (LBA 0) --- */
		char mbr[512];
		memset(mbr, 0, 512);
		mbr[446] = 0x00;                                    /* non-bootable */
		mbr[447] = 0x00; mbr[448] = 0x02; mbr[449] = 0x00;  /* CHS start 0/0/2 */
		mbr[450] = 0xEE;                                    /* protective type */
		mbr[451] = 0xFF; mbr[452] = 0xFF; mbr[453] = 0xFF;  /* CHS end */
		put32(mbr + 454, 1);                                /* LBA start = 1 */
		put32(mbr + 458, (uint32_t)(total - 1));            /* sector count (u32) */
		mbr[510] = 0x55; mbr[511] = 0xAA;

		/* --- primary GPT header (LBA 1) --- */
		char hdr[512];
		memset(hdr, 0, 512);
		Genode::memcpy(hdr, "EFI PART", 8);
		put32(hdr + 8,  0x00010000);      /* revision 1.0 */
		put32(hdr + 12, 92);              /* header size */
		put64(hdr + 24, 1);               /* current LBA */
		put64(hdr + 32, total - 1);       /* backup LBA */
		put64(hdr + 40, 34);              /* first usable LBA */
		put64(hdr + 48, last_usable);     /* last usable LBA */
		uint8_t const dguid[16] = { 0xf6,0x97,0x12,0xa5, 0x88,0xa7, 0xc1,0x47,
		                            0xa5,0x5f, 0x4e,0x50, 0x3f,0x90, 0xb2,0xd6 };
		Genode::memcpy(hdr + 56, dguid, 16);
		put64(hdr + 72, 2);               /* entries LBA */
		put32(hdr + 80, 128);             /* number of entries */
		put32(hdr + 84, 128);             /* entry size */

		/* --- partition entries (LBA 2) --- */
		char entries[128 * 128];
		memset(entries, 0, sizeof(entries));

		uint8_t const efi_type[16]  = { 0x28,0x73,0x2a,0xc1, 0x1f,0xf8, 0xd2,0x11,
		                                0xba,0x4b, 0x00,0xa0, 0xc9,0x3e, 0xc9,0x3b };
		uint8_t const data_type[16] = { 0xaf,0x3d,0xc6,0x0f, 0x83,0x84, 0x72,0x47,
		                                0x8e,0x79, 0x3d,0x69, 0xd8,0x47, 0x7d,0xe4 };
		uint8_t const efi_uniq[16]  = { 0x01,0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0 };
		uint8_t const data_uniq[16] = { 0x02,0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0 };

		Genode::memcpy(entries + 0*128 + 0,  efi_type,  16);
		Genode::memcpy(entries + 0*128 + 16, efi_uniq,  16);
		put64(entries + 0*128 + 32, efi_first);
		put64(entries + 0*128 + 40, efi_last);

		Genode::memcpy(entries + 1*128 + 0,  data_type, 16);
		Genode::memcpy(entries + 1*128 + 16, data_uniq, 16);
		put64(entries + 1*128 + 32, data_first);
		put64(entries + 1*128 + 40, data_last);

		/* UTF-16LE partition names */
		char const *name_efi  = "EFI";
		char const *name_data = "GENODE";
		for (int i = 0; name_efi[i];  ++i) entries[0*128 + 56 + 2*i] = name_efi[i];
		for (int i = 0; name_data[i]; ++i) entries[1*128 + 56 + 2*i] = name_data[i];

		/* --- CRCs --- */
		put32(hdr + 88, crc32((uint8_t const *)entries, 128 * 128));
		put32(hdr + 16, 0);
		put32(hdr + 16, crc32((uint8_t const *)hdr, 92));

		/* --- backup header (current/backup/entries LBAs swapped) --- */
		char bhdr[512];
		Genode::memcpy(bhdr, hdr, 92);
		put64(bhdr + 24, total - 1);      /* current LBA -> backup */
		put64(bhdr + 32, 1);              /* backup LBA -> primary */
		put64(bhdr + 72, total - 33);     /* entries LBA -> backup entries */
		put32(bhdr + 16, 0);
		put32(bhdr + 16, crc32((uint8_t const *)bhdr, 92));

		/* --- writes --- */
		if (!_blk_io(Block::Packet_descriptor::WRITE, 0, mbr, 512)) return false;
		if (!_blk_io(Block::Packet_descriptor::WRITE, 512, hdr, 512)) return false;
		if (!_blk_io(Block::Packet_descriptor::WRITE, 1024, entries, 128 * 128)) return false;
		if (!_blk_io(Block::Packet_descriptor::WRITE, (total - 33) * 512, entries, 128 * 128)) return false;
		if (!_blk_io(Block::Packet_descriptor::WRITE, (total - 1) * 512, bhdr, 512)) return false;

		return true;
	}

	void _install_tick()
	{
		using namespace Ext2;
		uint64_t const gb_sectors  = (1024ull * 1024 * 1024) / 512;
		uint64_t const efi_sectors = (200ull * 1024 * 1024) / 512;
		uint64_t const data_sectors = _alloc_gb * gb_sectors;

		switch (_install_phase) {
		case INST_FORMATTING:
			_data_off = (34 + efi_sectors) * 512;
			if (!_write_gpt(_alloc_gb)) { _install_phase = INST_IDLE; log("HermOS installer: GPT FAILED"); return; }
			if (!_ext2_format(data_sectors)) { _install_phase = INST_IDLE; log("HermOS installer: FORMAT FAILED"); return; }
			_install_phase = INST_COPYING; _install_pct = 40;
			log("HermOS installer: GPT + ext2 written (", (unsigned)_alloc_gb, " GB)");
			break;

		case INST_COPYING: {
			if (!_e2_prepare(data_sectors)) { _install_phase = INST_IDLE; log("HermOS installer: PREPARE FAILED"); return; }
			if (!_extract_boot_files()) { _install_phase = INST_IDLE; log("HermOS installer: EXTRACT FAILED"); return; }
			void *bx = nullptr; size_t bx_size = 0;
			if (_read_bootx64(&bx, &bx_size)) {
				bool const ok = _fat32_format(34, efi_sectors, bx, bx_size);
				_asset_heap.free(bx, bx_size ? bx_size : 1);
				if (!ok) { _install_phase = INST_IDLE; log("HermOS installer: FAT32 FAILED"); return; }
				log("HermOS installer: EFI partition written (bootx64 ", (unsigned)bx_size, " bytes)");
			} else {
				log("HermOS installer: bootx64 read failed (continuing without EFI)");
			}
			_install_phase = INST_VERIFYING; _install_pct = 96;
			break;
		}

		case INST_VERIFYING: {
			char gpt[512];
			bool const ok = _blk_io(Block::Packet_descriptor::READ, 512, gpt, 512);
			char const sig[8] = { 'E','F','I',' ','P','A','R','T' };
			bool is_gpt = ok;
			for (int i = 0; i < 8 && is_gpt; ++i) is_gpt = (gpt[i] == sig[i]);
			if (is_gpt) {
				_install_phase = INST_DONE; _install_pct = 100;
				_installer_step = 2;   /* INST_COMPLETE */
				log("HermOS installer: boot image verified (GPT ok) -> INST_COMPLETE");
			} else {
				_install_phase = INST_IDLE; log("HermOS installer: VERIFY FAILED");
			}
			break;
		}

		case INST_DONE:
		default:
			break;
		}
		_dirty = true;
	}

	/* a floating dynamic octagon: soft orange glow + dark glass + orange border */
	void draw_dynamic_octagon(int x, int y, int w, int h)
	{
		_glow_oct(x, y, w, h, _active_r, _active_g, _active_b);
		_fill_oct_glass(x, y, w, h, 13, 18, 30, 220);
		_stroke_oct(x, y, w, h, _accent_r, _accent_g, _accent_b);
	}

	void draw_installer(Surface<Pixel> &s)
	{
		draw_dynamic_octagon(_ix(), _iy(), _iw(), _ih());

		/* V3 3-step installer: 0 = INST_SETUP, 1 = INST_ACTION, 2 = INST_COMPLETE */
		switch (_installer_step) {
		case 0: {
			_text(s, _icx(26.0f, "Install HermOS"), 180, 26.0f, _white(), "Install HermOS");
			_text(s, _icx(13.0f, "Live OS — set up your computer in 3 steps."), 224, 13.0f, _muted(),
			      "Live OS — set up your computer in 3 steps.");

			/* disk size slider */
			_text(s, _w / 2 - 280, 296, 12.0f, _muted(), "Disk size");
			_fill_rrect(_w / 2 - 280, 330, 560, 60, 12, 24, 34, 48);
			_stroke_rrect(_w / 2 - 280, 330, 560, 60, 12, _accent_r, _accent_g, _accent_b);
			_stepper_btn(s, _w / 2 - 240, 342, 44, 36, false);
			Genode::String<16> alloc { (unsigned)_alloc_gb, " GB" };
			_text(s, _icx(15.0f, alloc.string()), 352, 15.0f, _white(), alloc.string());
			_stepper_btn(s, _w / 2 + 196, 342, 44, 36, true);

			/* MyKee key */
			_text(s, _w / 2 - 280, 416, 12.0f, _muted(), "MyKee Key (AI router)");
			_fill_rrect(_w / 2 - 280, 450, 560, 48, 12, 24, 34, 48);
			_stroke_rrect(_w / 2 - 280, 450, 560, 48, 12, _accent_r, _accent_g, _accent_b);
			_text(s, _w / 2 - 260, 464, 14.0f, _solar(), _mykee_key.string());

			/* confirmation checkbox */
			_fill_rrect(_w / 2 - 280, 540, 18, 18, 4, 40, 48, 60);
			_stroke_rrect(_w / 2 - 280, 540, 18, 18, 4, _accent_r, _accent_g, _accent_b);
			if (_installer_confirm) _glyph_check(_w / 2 - 271, 549);
			_text(s, _w / 2 - 254, 542, 12.0f, _white(),
			      "I understand this will format the disk and erase all data");

			_installer_nav(s, true, "Install HermOS", _installer_confirm);
			break;
		}
		case 1: {
			_text(s, _icx(26.0f, "Installing HermOS"), 180, 26.0f, _white(), "Installing HermOS");
			_text(s, _icx(13.0f, "Formatting the disk and writing the system."), 226, 13.0f, _muted(),
			      "Formatting the disk and writing the system.");
			char const *steps[4] = { "Writing partition table...", "Formatting filesystems...",
			                         "Installing core system...", "Verifying installation..." };
			int const done = _install_phase >= INST_DONE ? 4
			               : _install_phase >= INST_VERIFYING ? 3
			               : _install_phase >= INST_COPYING ? 2 : 0;
			int iy = 300;
			for (int i = 0; i < 4; ++i) {
				bool const chk = i < done;
				_disc(_w / 2 - 250, iy + 6, 6, chk ? 60 : 52, chk ? 200 : 70, chk ? 90 : 90);
				if (chk) _glyph_check(_w / 2 - 250, iy + 6);
				_text(s, _w / 2 - 230, iy, 13.0f, chk ? _white() : _muted(), steps[i]);
				iy += 52;
			}
			_progress(_w / 2 - 150, 560, 300, 10, (int)_install_pct);
			Genode::String<8> pct { (int)_install_pct, "%" };
			_text(s, _w / 2 - 150, 582, 12.0f, _muted(), pct.string());
			break;
		}
		case 2: {
			_draw_icon("logo", _w / 2, 320);
			_text(s, _icx(26.0f, "HermOS installation complete"), 400, 26.0f, _white(), "HermOS installation complete");
			_text(s, _icx(14.0f, "Your computer is now ready. Reboot to start HermOS."), 444, 14.0f, _muted(),
			      "Your computer is now ready. Reboot to start HermOS.");
			_button(s, _w / 2 - 360, _iby(), 150, 36, "Explore HermOS", false);
			_button(s, _w / 2 + 240, _iby(), 150, 36, "Reboot Now", true);
			break;
		}
		}
	}

	void _render_server(Surface<Pixel> &s)
	{
		/* dark server backdrop */
		for (int y = 0; y < _h; ++y)
			for (int x = 0; x < _w; ++x)
				_set(x, y, 8, 10, 14);

		/* header band */
		_glass(0, 0, _w, 72, 10, 12, 18, 240);
		_h_line(0, 72, _w, _accent_r, _accent_g, _accent_b);
		_draw_emblem(40, 36, 40);
		_text(s, 92, 24, 16.0f, _white(), "HermOS Server");

		/* version chip (top-right) */
		_fill_rrect(_w - 190, 20, 170, 34, 9, 22, 32, 46);
		_stroke_rrect(_w - 190, 20, 170, 34, 9, _accent_r, _accent_g, _accent_b);
		_text(s, _w - 176, 28, 13.0f, _white(), "HermOS V0.24");

		/* system status panel */
		int const px = 60, py = 110, pw = _w - 120;
		_fill_rrect_glass(px, py, pw, 150, 12, 13, 18, 30, 205);
		_stroke_rrect(px, py, pw, 150, 12, _inactive_r, _inactive_g, _inactive_b);
		_text(s, px + 24, py + 22, 15.0f, _white(), "SYSTEM STATUS");
		_text(s, px + 24, py + 54, 14.0f, _white(), "Status:    RUNNING");
		_text(s, px + 24, py + 78, 14.0f, _white(), "Edition:   Server");
		_text(s, px + 24, py + 102, 14.0f, _white(), "Kernel:    NOVA / Genode 26.08");
		_text(s, px + 24, py + 126, 14.0f, _white(), "Director:  Hermes agent (active)");

		/* services panel */
		int const sy = py + 150 + 20;
		_fill_rrect_glass(px, sy, pw, 260, 12, 13, 18, 30, 205);
		_stroke_rrect(px, sy, pw, 260, 12, _inactive_r, _inactive_g, _inactive_b);
		_text(s, px + 24, sy + 24, 15.0f, _white(), "SERVICES");

		struct Svc { char const *name; char const *port; char const *state; bool up; };
		Svc svcs[4] = {
			{ "hermes-httpd",    ":80",   "running",  true  },
			{ "reverse-proxy",   ":443",  "running",  true  },
			{ "hermes-director", ":9000", "running",  true  },
			{ "metrics",         ":9100", "stopped",  false },
		};
		for (int i = 0; i < 4; ++i) {
			int const ry = sy + 60 + i * 46;
			_disc(px + 28, ry + 10, 7, svcs[i].up ? 80 : 70, svcs[i].up ? 200 : 80, svcs[i].up ? 100 : 85);
			_text(s, px + 48, ry, 14.0f, _white(), svcs[i].name);
			_text(s, px + pw - 110, ry, 14.0f, _muted(), svcs[i].port);
			_text(s, px + pw - 300, ry, 13.0f, svcs[i].up ? _white() : _muted(), svcs[i].state);
		}

		/* footer */
		_glass(12, _h - 40, _w - 24, 28, 19, 29, 42, 230);
		_text(s, 24, _h - 32, 12.0f, _muted(), "HermOS Server — minimal GUI");
	}

	void _render()
		{
			if (_scene_ready && !_server_mode) { _render_scene(); return; }

			_blit_wallpaper();

			Surface<Pixel> surface(_px, Area(unsigned(_w), unsigned(_h)));

			if (_server_mode) { _render_server(surface); return; }

			/* ---- left dock: always drawn (behind the installer Cell) ---- */
			{
				int const dock_w = 80;
				int const dock_x = 20, dock_y = 250;
				for (int i = 0; i < _registry_count; ++i) {
					int const ty = dock_y + i * (dock_w + 12);
					_fill_rrect(dock_x, ty, dock_w, dock_w, dock_w / 4, _dock_r, _dock_g, _dock_b);
					_stroke_rrect(dock_x, ty, dock_w, dock_w, dock_w / 4, _inactive_r, _inactive_g, _inactive_b);
					_draw_icon_png(_registry[i].icon_data, _registry[i].icon_size, dock_x + dock_w / 2, ty + dock_w / 2);
				}
				/* "Install HermOS" launcher — always present, below the registry apps */
				int const inst_ty = dock_y + _registry_count * (dock_w + 12);
				_fill_rrect(dock_x, inst_ty, dock_w, dock_w, dock_w / 4, _accent_r, _accent_g, _accent_b);
				_stroke_rrect(dock_x, inst_ty, dock_w, dock_w, dock_w / 4, 255, 170, 60);
				_glyph_install(dock_x + dock_w / 2, inst_ty + dock_w / 2);
				_text(surface, dock_x + dock_w / 2 - _font.text_width(13.0f * 1.2f, "Install") / 2,
				      inst_ty + dock_w + 6, 13.0f, _muted(), "Install");
			}

			/* the installer wizard is drawn near the end of _render, on top of the Cells */

			/* ---- header: logo + wordmark ---- */
			_draw_emblem(26, 22, 32);
			_draw_text(surface, 62, 20, _white(), "HermOS");
			_draw_text(surface, 62, 40, _muted(), "A MORE HUMAN COMPUTER");

			/* ---- greeting (disabled: was rendering a white block / duplicating) ---- */

			/* ---- search bar (pill, centred 50% x 8%) ---- */
			/* glow removed — it read as a second search bar */
			_fill_rrect_glass(_bar_x(), _bar_y(), _bar_w(), _bar_h(), _bar_h() / 2, 22, 32, 44, 210);
			_stroke_rrect(_bar_x(), _bar_y(), _bar_w(), _bar_h(), _bar_h() / 2, _accent_r, _accent_g, _accent_b);
			_draw_icon("search", _bar_x() + _bar_h() / 2 + 6, _bar_cy());
			_text(surface, _bar_x() + _bar_h(), _bar_y() + _bar_h() / 2 - 8,
			      16.0f, _prompt_len ? _white() : _muted(),
			      _prompt_len ? _prompt : "Ask or describe what you want...");
			_glyph_chev(_bar_x() + _bar_w() - _bar_h() / 2, _bar_cy());

			/* ---- right panel: Agents & Workers (hideable: F11) ---- */
			if (!_chrome_hidden) {
				int const sb_w = _sb_w, sb_h = _sb_h, sb_y = _sb_y;
				int const sb_x = _sb_x;

				_fill_rrect_glass(sb_x, sb_y, sb_w, sb_h, 14, _sidebar_r, _sidebar_g, _sidebar_b, 205);
				_stroke_rrect(sb_x, sb_y, sb_w, sb_h, 14, _inactive_r, _inactive_g, _inactive_b);
			}

			/* sidebar content drawn by _draw_sidebar_content() over the composited panel */
			/* ---- status bar (taskbar, docked at the BOTTOM as in the mockup) ---- */
			int const tb_y = 994;
			_glass(12, tb_y, _w - 24, 26, 19, 29, 42, 230);
			_draw_emblem(26, tb_y + 13, 22);
			_draw_text(surface, 46, tb_y + 6, _white(), "HermOS");
			/* volume / wifi / battery glyphs (centre, as in the mockup) */
			_disc(470, tb_y + 13, 4, 160, 180, 205); _ring(470, tb_y + 13, 7, 160, 180, 205);
			_disc(512, tb_y + 13, 3, 160, 180, 205); _disc(512, tb_y + 13 - 8, 3, 160, 180, 205);
			_disc(512, tb_y + 13 + 8, 3, 160, 180, 205);
			_ring(554, tb_y + 13, 6, 160, 180, 205);
			_draw_text(surface, _w - 210, tb_y + 6, _muted(), _clock());

			/* ---- hideable bottom panel (dock layer: drawn BELOW the Cells) ---- */
			{
				int const target = _bottom_panel_visible ? (_h - _panel_h) : _h;
				if (_panel_y < target)      _panel_y = min(_panel_y + 20, target);
				else if (_panel_y > target) _panel_y = max(_panel_y - 20, target);
				if (_panel_y < _h) _panel(surface, _panel_y);
			}

			/* ---- six floating Cells, drawn in z-order (bottom -> top) ---- */
			{
				char const *titles[6] = { "Welcome to HermOS", "System", "Files",
				                          "Hydroponics Agent", "Create a New Worker",
				                          "Software & Updates" };

				/* Step 5b: the sidebar panel sits BEHIND the cells */
				if (!_chrome_hidden) {
					_blit_sidebar();
					_draw_sidebar_content(surface);
				}

				for (int oi = 0; oi < NUM_CELLS; ++oi) {
					int const i = _draw_order[oi];
					int const wx = _win_x(i), wy = _win_y(i);
					Cell_session *hosted = _gui_root.cells().at(i);
					char const *title = hosted ? _short_label(hosted->label().string())
					                           : titles[i];
					/* the chamfer: the percentage of min(w, h), live per frame */
					int const chamfer_px = _chamfer[i] * _min2(_cell_w(i), _cell_h(i)) / 100;
					_win(surface, wx, wy, _cell_w(i), _cell_h(i), chamfer_px,
					     title, _cell_state[i]);
					if (hosted) {
						/* composite the app's framebuffer into the Cell */
						_blit_cell(i, hosted);
					} else {
						switch (i) {
						case 0: _win_welcome(surface, wx, wy, _cell_w(i), _cell_h(i)); break;
						case 1: _win_system(surface, wx, wy, _cell_w(i), _cell_h(i));  break;
						case 2: _win_files(surface, wx, wy, _cell_w(i), _cell_h(i));   break;
						case 3: _win_hydro(surface, wx, wy, _cell_w(i), _cell_h(i));   break;
						case 4: _win_worker(surface, wx, wy, _cell_w(i), _cell_h(i));  break;
						case 5: _win_updates(surface, wx, wy, _cell_w(i), _cell_h(i)); break;
						}
					}
				}
			}

			/* control icons: inside the focused Cell's edges, vertically centred */
			if (_gui_root.cells().focused()) {
				int const idx = _gui_root.cells().focused_index();
				int const cy  = _focus_cy();
				int const x   = _win_x(idx);
				int const w   = _cell_w(idx);

				/* LEFT: mic (voice) + pause (suspend) */
				_icon_pin(x + 16, cy - 18, true);
				_icon_pin(x + 16, cy + 18, false);

				/* RIGHT: minimize + maximize + close */
				_disc(x + w - 16, cy - 36, 8, 52, 70, 90);  _glyph_min(x + w - 16, cy - 36);
				_disc(x + w - 16, cy,      8, 52, 70, 90);  _glyph_resize(x + w - 16, cy);
				_disc(x + w - 16, cy + 36, 8, 214, 74, 74); _glyph_close(x + w - 16, cy + 36);
			}

			if (_paused && _gui_root.cells().focused())
				_draw_text(surface, _win_x(_gui_root.cells().focused_index()) + 62,
				           _focus_cy() - 6, _solar(), "PAUSED");

			/* installer wizard: drawn last, on top of the Cells */
			if (_installer_mode) draw_installer(surface);

			/* AI reply drop-down (on top of the cells, under the cursor) */
			_draw_ai_response(surface);

			/* live pointer, on top */
			_cursor();

			_fb.refresh(Rect(Point(0, 0), _fb.mode().area));
		}

		/* golden-compass emblem, keyed out of its dark background by luminance */
		void _draw_emblem(int cx, int cy, int size)
		{
			unsigned char const *const data =
				reinterpret_cast<unsigned char const *>(_binary_emblem_rgba_start);

			int const x = cx - size / 2, y = cy - size / 2;
			for (int py = 0; py < size; ++py)
				for (int px = 0; px < size; ++px) {
					int const sx = px * 80 / size;
					int const sy = py * 80 / size;
					unsigned const at = unsigned((sy * 80 + sx) * 4);

					unsigned const lum = (unsigned(data[at]) * 77
					                    + unsigned(data[at + 1]) * 150
					                    + unsigned(data[at + 2]) * 29) >> 8;
					unsigned const a = (unsigned(data[at + 3]) * lum) / 255;
					if (a < 8) continue;

					Pixel const dst = _px[(y + py) * _w + (x + px)];
					unsigned const r = (unsigned(data[at]) * a + unsigned(dst.r()) * (255 - a)) / 255;
					unsigned const g = (unsigned(data[at + 1]) * a + unsigned(dst.g()) * (255 - a)) / 255;
					unsigned const b = (unsigned(data[at + 2]) * a + unsigned(dst.b()) * (255 - a)) / 255;
					_set(x + px, y + py, uint8_t(r), uint8_t(g), uint8_t(b));
				}
		}

		/* ---- input handling (Phase 1b) ---- */

		bool _hit(int x, int y, int cx, int cy, int r)
		{ return x >= cx - r && x <= cx + r && y >= cy - r && y <= cy + r; }

		/* ---- floating window manager: z-order, hit-test, drag/resize ---- */

		void _bring_to_front(int cell)
		{
			int pos = -1;
			for (int j = 0; j < NUM_CELLS; ++j)
				if (_draw_order[j] == cell) { pos = j; break; }
			if (pos < 0) return;
			for (int j = pos; j < NUM_CELLS - 1; ++j)
				_draw_order[j] = _draw_order[j + 1];
			_draw_order[NUM_CELLS - 1] = cell;
		}

		bool _in_cell(int cell, int x, int y) const
		{
			Cell_geom const &g = _geom[cell];
			int const chamfer_px = _chamfer[cell] * _min2(g.w, g.h) / 100;
			return is_inside_octagon(x, y, g.x, g.y, g.w, g.h, chamfer_px);
		}

		/* which border edge(s) the point is within 10px of (bitmask) */
		int _edge_at(int cell, int x, int y) const
		{
			Cell_geom const &g = _geom[cell];
			int edge = 0;
			if (x <  g.x + 10)       edge |= 1;   /* left   */
			if (x >= g.x + g.w - 10) edge |= 2;   /* right  */
			if (y <  g.y + 10)       edge |= 4;   /* top    */
			if (y >= g.y + g.h - 10) edge |= 8;   /* bottom */
			return edge;
		}

		/* apply a drag/resize on mouse motion */
		void _drag_to(int nx, int ny)
		{
			if (_drag_mode == DRAG_MOVE && _drag_cell >= 0) {
				Cell_geom &g = _geom[_drag_cell];
				g.x = nx - _drag_off_x;
				g.y = ny - _drag_off_y;
				_dirty = true;
			} else if (_drag_mode == DRAG_RESIZE && _drag_cell >= 0) {
				Cell_geom &g = _geom[_drag_cell];
				int const dx = nx - _drag_sx;
				int const dy = ny - _drag_sy;
				if (_drag_edge & 1) { g.x = _drag_orig.x + dx; g.w = _drag_orig.w - dx; }
				if (_drag_edge & 2) { g.w = _drag_orig.w + dx; }
				if (_drag_edge & 4) { g.y = _drag_orig.y + dy; g.h = _drag_orig.h - dy; }
				if (_drag_edge & 8) { g.h = _drag_orig.h + dy; }

				/* Step 1: the KDE resize clamps (clientArea() concept).
				 * Hard floor: no microscopic Cells (150x100 px).
				 * Hard ceiling: never overlap the agents_sidebar (right edge)
				 * or the bottom dock (bottom edge). */
				constexpr int MIN_W = 150;
				constexpr int MIN_H = 100;
				int max_w = (int)_fb.mode().area.w - _sb_w - g.x;
				int max_h = (int)_fb.mode().area.h - _dock_y_offset - g.y;
				/* keep the clamp bounds ordered even when the drag pushes the
				 * cell past the sidebar/dock guard */
				if (max_w < MIN_W) max_w = MIN_W;
				if (max_h < MIN_H) max_h = MIN_H;
				g.w = g.w < MIN_W ? MIN_W : (g.w > max_w ? max_w : g.w);
				g.h = g.h < MIN_H ? MIN_H : (g.h > max_h ? max_h : g.h);
				_dirty = true;
			}
		}

		/* end any active drag/resize */
		void _drag_end()
		{
			/* Step 3: the quick-tile snap. When a drag ends with the Cell
			 * within the 20px of the absolute screen edge, snap it to the
			 * normalized slot published by the theme engine. */
			if (_drag_mode == DRAG_MOVE && _drag_cell >= 0) {
				Cell_geom &g = _geom[_drag_cell];
				int const scr_w  = (int)_fb.mode().area.w;
				int const scr_h  = (int)_fb.mode().area.h;
				int const work_h = scr_h - _dock_y_offset;

				char const *match = nullptr;
				if (g.x <= 20)               match = "left";
				if (g.x + g.w >= scr_w - 20) match = "right";

				if (match) {
					for (int i = 0; i < _snap_count; ++i) {
						Snap_def const &s = _snaps[i];
						if (!s.valid || Genode::strcmp(s.id, match) != 0)
							continue;
						g.x = s.x_pct * scr_w / 100;
						g.y = 0;
						g.w = s.w_pct * scr_w / 100;
						g.h = work_h;
						log("HermOS: cell ", _drag_cell, " snapped to \"",
						    match, "\" (normalized slot)");
						break;
					}
				}
			}

			if (_drag_mode != DRAG_NONE)
				log("HermOS: drag ended (mode ", _drag_mode, ")");
			_drag_mode = DRAG_NONE;
			_drag_cell = -1;
			_drag_edge = 0;
		}

		void _click_cells(int x, int y)
		{
			/* hit-test cells top-down (reverse z-order): focus / move / resize */
			for (int oi = NUM_CELLS - 1; oi >= 0; --oi) {
				int const i = _draw_order[oi];
				if (!_in_cell(i, x, y)) continue;

				_bring_to_front(i);
				_gui_root.cells().focus(i);

				/* Step 2: the focused slot becomes ACTIVE (unless paused);
				 * the previously-active slot falls back to INACTIVE */
				if (_cell_state[i] != CELL_PAUSED) {
					for (int j = 0; j < NUM_CELLS; ++j)
						if (j != i && _cell_state[j] == CELL_ACTIVE)
							_cell_state[j] = CELL_INACTIVE;
					_cell_state[i] = CELL_ACTIVE;
				}

				int const edge = _edge_at(i, x, y);
				if (edge != 0) {
					/* edge/corner -> resize */
					_drag_mode = DRAG_RESIZE;
					_drag_cell = i;
					_drag_edge = edge;
					_drag_sx = x;
					_drag_sy = y;
					_drag_orig = _geom[i];
					log("HermOS: resizing cell ", i, " (edge ", edge, ")");
				} else if (y < _geom[i].y + 30) {
					/* title bar -> drag move */
					_drag_mode = DRAG_MOVE;
					_drag_cell = i;
					_drag_off_x = x - _geom[i].x;
					_drag_off_y = y - _geom[i].y;
					log("HermOS: dragging cell ", i);
				} else {
					log("HermOS cell ", i, " focused (body)");
				}
				_dirty = true;
				return;
			}
		
		}

		void _click(int x, int y)
		{
			if (_installer_mode) { _installer_click(x, y); return; }
			if (_scene_ready) {
				if (_scene_click(x, y)) return;
				_click_cells(x, y);
				return;
			}
			_click_legacy(x, y);
		}

		void _click_legacy(int x, int y)
		{
			if (_installer_mode) { _installer_click(x, y); return; }

			/* left dock: launcher buttons (populated from the registry) */
			if (x >= 12 && x < 56 && y >= 190) {
				int const iy = (y - 190) / 66;
				if (iy >= 0 && iy < _registry_count && (y - 190) % 66 < 44) {
					log("HermOS dock: launching '", _registry[iy].name, "'");
					_launch_app(_registry[iy].kind);
					return;
				}
			}

			/* "Install HermOS" launcher: launch the 9-step installer */
			int const dock_w = 80, dock_x = 20, dock_y = 250;
			if (x >= dock_x && x < dock_x + dock_w && y >= dock_y) {
				int const iy = (y - dock_y) / (dock_w + 12);
				if (iy == _registry_count && (y - dock_y) % (dock_w + 12) < dock_w) {
					_installer_mode = true;
					_installer_step = 0;
					log("HermOS: launching installer");
					_dirty = true;
					return;
				}
			}

			/* command bar: focus for typing */
			if (x >= _bar_x() && x < _bar_x() + _bar_w() && y >= _bar_y() && y < _bar_y() + _bar_h()) {
				_bar_focused = true;
				log("HermOS command bar focused");
				_dirty = true;
				return;
			}

			/* control icons pinned OUTSIDE the focused Cell */
			if (_gui_root.cells().focused()) {
				int const idx = _gui_root.cells().focused_index();
				int const cy  = _focus_cy();
				int const lx  = _ctrl_lx(), rx = _ctrl_rx();

				if (_hit(x, y, lx, cy - 18, 11)) {   /* mic */
					log("HermOS voice command: listening");
					_dirty = true; return;
				}
				if (_hit(x, y, lx, cy + 18, 11)) {   /* pause */
					/* Step 2: toggle the slot's CellState (PAUSED <-> its
					 * active/inactive baseline) and trigger a redraw */
					Cell_state &st = _cell_state[idx];
					st = (st == CELL_PAUSED)
					   ? (idx == _focus_idx() ? CELL_ACTIVE : CELL_INACTIVE)
					   : CELL_PAUSED;
					_paused = (st == CELL_PAUSED);   /* legacy guards in sync */
					log(st == CELL_PAUSED ? "HermOS cell SUSPENDED (CPU/RAM released, state kept)"
					                      : "HermOS cell RESUMED");
					_dirty = true; return;
				}
				if (_hit(x, y, rx, cy - 36, 11)) {   /* minimize */
					_win_open[idx] = false;
					log("HermOS cell ", idx, " minimized");
					_dirty = true; return;
				}
				if (_hit(x, y, rx, cy, 11)) {        /* maximize -> the work area */
					Cell_geom &g = _geom[idx];

					/* Step 2: KDE's clientArea() concept. A maximized Cell
					 * fills ONLY the space between the left edge and the
					 * agents_sidebar, above the bottom dock — never the
					 * raw screen. The dock + sidebar are the "struts". */
					g.x = 0;
					g.y = 0;
					g.w = (int)_fb.mode().area.w - _sb_w;
					g.h = (int)_fb.mode().area.h - _dock_y_offset;

					log("HermOS cell ", idx, " maximized (work area)");
					_dirty = true; return;
				}
				if (_hit(x, y, rx, cy + 36, 11)) {   /* close */
					_win_open[idx] = false;
					_gui_root.close_cell(idx);
					_close_app(idx);   /* destroy the spawned app + return quota */
					log("HermOS cell ", idx, " closed: session destroyed, quota returned");
					_dirty = true; return;
				}
			}

			/* right panel slide toggle */
			if (_panel_open) {
				if (_hit(x, y, 774 + 238 - 48, 30, 10)) {
					_panel_open = false;
					log("HermOS panel: slid out");
					_dirty = true;
					return;
				}
			} else {
				if (_hit(x, y, 1016, 30, 10)) {
					_panel_open = true;
					log("HermOS panel: slid in");
					_dirty = true;
					return;
				}
			}

			/* hit-test cells top-down (reverse z-order): focus / move / resize */
			for (int oi = NUM_CELLS - 1; oi >= 0; --oi) {
				int const i = _draw_order[oi];
				if (!_in_cell(i, x, y)) continue;

				_bring_to_front(i);
				_gui_root.cells().focus(i);

				/* Step 2: the focused slot becomes ACTIVE (unless paused);
				 * the previously-active slot falls back to INACTIVE */
				if (_cell_state[i] != CELL_PAUSED) {
					for (int j = 0; j < NUM_CELLS; ++j)
						if (j != i && _cell_state[j] == CELL_ACTIVE)
							_cell_state[j] = CELL_INACTIVE;
					_cell_state[i] = CELL_ACTIVE;
				}

				int const edge = _edge_at(i, x, y);
				if (edge != 0) {
					/* edge/corner -> resize */
					_drag_mode = DRAG_RESIZE;
					_drag_cell = i;
					_drag_edge = edge;
					_drag_sx = x;
					_drag_sy = y;
					_drag_orig = _geom[i];
					log("HermOS: resizing cell ", i, " (edge ", edge, ")");
				} else if (y < _geom[i].y + 30) {
					/* title bar -> drag move */
					_drag_mode = DRAG_MOVE;
					_drag_cell = i;
					_drag_off_x = x - _geom[i].x;
					_drag_off_y = y - _geom[i].y;
					log("HermOS: dragging cell ", i);
				} else {
					log("HermOS cell ", i, " focused (body)");
				}
				_dirty = true;
				return;
			}
		}

		/* ================= immediate-mode scene glue ================= */

		static char const *_weekday_name(int y, int m, int d)
		{
			static char const *n[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
			static int const t[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
			if (m < 3) y -= 1;
			return n[(y + y/4 - y/100 + y/400 + t[(m - 1) % 12] + d) % 7];
		}

		char const *_scene_clock()
		{
			if (_rtc.constructed()) {
				Rtc::Timestamp const ts = _rtc->current_time();
				_sclock = Genode::String<48>(
					_weekday_name((int)ts.year, (int)ts.month, (int)ts.day), " ",
					(ts.day < 10 ? "0" : ""), ts.day, " ", _month_name(ts.month), " ", ts.year, " ",
					(ts.hour < 10 ? "0" : ""), ts.hour, ":", (ts.minute < 10 ? "0" : ""), ts.minute);
			} else {
				_sclock = Genode::String<48>("-- --- ---- --:--");
			}
			return _sclock.string();
		}

		/* load one PNG from /assets as straight RGBA (kept alive for the scene) */
		bool _scene_load_image(char const *path, Scene::Image &out)
		{
			void *data = nullptr; size_t size = 0;
			if (!_read_file(_vfs, path, _asset_heap, data, size)) return false;
			Png_rgba const img = load_png_rgba(data, size);
			if (!img.px || img.w <= 0 || img.h <= 0) return false;
			out = Scene::Image { img.px, img.w, img.h };
			return true;
		}

		bool _scene_load_font(char const *path, Scene::Font &out)
		{
			void *data = nullptr; size_t size = 0;
			if (!_read_file(_vfs, path, _asset_heap, data, size)) return false;
			return out.load(data);
		}

		void _init_scene()
		{
			bool ok = true;
			ok &= _scene_load_font("/assets/Inter-Regular.ttf",  _sfonts.reg);
			ok &= _scene_load_font("/assets/Inter-Medium.ttf",   _sfonts.med);
			ok &= _scene_load_font("/assets/Inter-SemiBold.ttf", _sfonts.semi);
			ok &= _scene_load_image("/assets/wallpaper.png",     _sassets.wall);
			ok &= _scene_load_image("/assets/compass.png",       _sassets.compass);
			ok &= _scene_load_image("/assets/hydro_thumb.png",   _sassets.hydro_thumb);
			ok &= _scene_load_image("/assets/hydro_big.png",     _sassets.hydro_big);
			_scene_px = (uint32_t *)_asset_heap.alloc(size_t(_w) * size_t(_h) * 4);
			_scene_ready = ok && _scene_px != nullptr;
			log("HermOS scene: ", _scene_ready ? "ready (immediate-mode desktop)"
			                                   : "assets missing, using the legacy renderer");
		}

		void _sync_scene()
		{
			Scene::State &st = _sstate;
			st.cw = _w; st.ch = _h;
			st.clock       = _scene_clock();
			st.prompt      = _prompt_len ? _prompt : nullptr;
			st.search_focus = _bar_focused;
			st.active_win  = _focus_idx();
			for (int i = 0; i < NUM_CELLS; ++i) {
				st.win[i]       = { float(_win_x(i)), float(_win_y(i)), float(_cell_w(i)), float(_cell_h(i)) };
				st.win_open[i]  = _win_open[i];
				st.win_hosted[i] = _gui_root.cells().at(i) != nullptr;
			}
			for (int oi = 0; oi < NUM_CELLS; ++oi) st.order[oi] = _draw_order[oi];
		}

		unsigned _scene_state_hash() const
		{
			unsigned h = 2166136261u;
			auto mix = [&] (unsigned v) { h = (h ^ v) * 16777619u; };
			Scene::State const &st = _sstate;
			for (int i = 0; i < 6; ++i) {
				mix(unsigned(st.win[i].x)); mix(unsigned(st.win[i].y)); mix(unsigned(st.win[i].w)); mix(unsigned(st.win[i].h));
				mix(st.win_open[i]); mix(st.win_hosted[i]); mix(unsigned(st.order[i]));
			}
			mix(unsigned(st.active_win)); mix(st.search_focus); mix(unsigned(_chrome_hidden));
			for (char const *c = st.clock; c && *c; ++c) mix(unsigned(*c));
			for (char const *c = st.prompt; c && *c; ++c) mix(unsigned(*c));
			return h;
		}

		void _render_scene()
		{
			_sync_scene();
			unsigned const hsh = _scene_state_hash();
			if (!_scene_valid || hsh != _scene_hash) {
				Scene::Canvas c { _scene_px, _w, _h };
				Scene::render_base(c, _sfonts, _sassets, _sstate, _chrome_hidden);
				_scene_hash = hsh; _scene_valid = true;
			}
			Genode::memcpy(_px, _scene_px, size_t(_w) * size_t(_h) * 4);
			{
				/* the hideable panels are redrawn every frame (cheap) */
				Scene::Canvas c { (uint32_t *)_px, _w, _h };
				Scene::render_panels(c, _sfonts, _sassets, _sstate, _shits);
			}

			/* hosted apps draw over their window frame */
			for (int oi = 0; oi < NUM_CELLS; ++oi) {
				int const i = _draw_order[oi];
				if (!_win_open[i]) continue;
				Cell_session *hosted = _gui_root.cells().at(i);
				if (hosted) _blit_cell(i, hosted);
			}

			Surface<Pixel> surface(_px, Area(unsigned(_w), unsigned(_h)));
			if (_installer_mode) draw_installer(surface);
			_draw_ai_response(surface);
			_cursor();
			_fb.refresh(Rect(Point(0, 0), _fb.mode().area));
		}

		/* hover-reveal of the hideable panels (Plasma auto-hide semantics) */
		void _scene_pointer()
		{
			Scene::Map const m = Scene::map_for(_w, _h);
			int const px = _pointer_x, py = _pointer_y;
			bool const lv = _sstate.left.visible(), bv = _sstate.bottom.visible();
			_sstate.left.hovering   = m.hit(_shits.left_grip, px, py)   || (lv && m.hit(_shits.left_panel, px, py));
			_sstate.bottom.hovering = m.hit(_shits.bottom_grip, px, py) || (bv && m.hit(Scene::TASKBAR_RECT, px, py));
		}

		/* returns true when the click was consumed by the scene chrome */
		bool _scene_click(int x, int y)
		{
			Scene::Map const m = Scene::map_for(_w, _h);

			/* window control buttons, topmost window under the pointer first */
			for (int oi = NUM_CELLS - 1; oi >= 0; --oi) {
				int const idx = _draw_order[oi];
				if (!_win_open[idx] || !_in_cell(idx, x, y)) continue;
				Scene::Ctrls const c = Scene::window_ctrls(idx,
					{ float(_win_x(idx)), float(_win_y(idx)), float(_cell_w(idx)), float(_cell_h(idx)) });
				for (int k = 0; k < 4; ++k) {
					if (!c.has[k]) continue;
					float const dx = float(x) - c.x[k], dy = float(y) - c.y;
					if (dx * dx + dy * dy > c.r * c.r * 1.8f) continue;
					if (k == Scene::CTRL_CLOSE) {
						_win_open[idx] = false;
						_gui_root.close_cell(idx);
						_close_app(idx);
						log("HermOS cell ", idx, " closed: session destroyed, quota returned");
					} else if (k == Scene::CTRL_MIN) {
						_win_open[idx] = false;
						log("HermOS cell ", idx, " minimized");
					} else if (k == Scene::CTRL_MAX) {
						/* Plasma clientArea(): the screen minus the struts of the
						 * panels that are permanently shown, and the sidebar */
						Scene::Rect const area = Scene::client_area(
							{ 0, 0, float(_w), float(_h) },
							{ Scene::Edge::Left,   _sstate.left.mode,   _sstate.left.thickness * m.k,   _sstate.left.t,   _sstate.left.pinned,   false },
							{ Scene::Edge::Bottom, _sstate.bottom.mode, _sstate.bottom.thickness * m.k, _sstate.bottom.t, _sstate.bottom.pinned, false });
						Cell_geom &g = _geom[idx];
						g.x = int(area.x); g.y = int(area.y);
						g.w = int(area.w) - _sb_w; g.h = int(area.h) - _dock_y_offset;
						log("HermOS cell ", idx, " maximized (client area)");
					} else {   /* undo: back to the theme slot */
						log("HermOS cell ", idx, " restore");
						_handle_theme();
					}
					_dirty = true;
					return true;
				}
				break;
			}

			/* the left launcher dock */
			if (_sstate.left.visible()) {
				for (int k = 0; k < _shits.launcher_count; ++k) {
					if (!m.hit(_shits.launcher[k], x, y)) continue;
					switch (k) {
					case 0: _launch_app(APP_FILES);    break;
					case 1: _launch_app(APP_TERMINAL); break;
					case 2: _launch_app(APP_TESTNIT);  break;
					default:
						_installer_mode = true; _installer_step = 0;
						log("HermOS: launching installer");
						break;
					}
					_dirty = true;
					return true;
				}
			}

			/* grips: click a hidden panel's tab to show it */
			if (!_sstate.left.visible() && m.hit(_shits.left_grip, x, y))     { _sstate.left.toggle();   _dirty = true; return true; }
			if (!_sstate.bottom.visible() && m.hit(_shits.bottom_grip, x, y)) { _sstate.bottom.toggle(); _dirty = true; return true; }

			/* taskbar: the HermOS button restores minimised windows; the chevron hides the bar */
			if (_sstate.bottom.visible() && m.hit(Scene::TASKBAR_RECT, x, y)) {
				if (x < int(m.X(110))) { for (int i = 0; i < NUM_CELLS; ++i) _win_open[i] = true; _dirty = true; return true; }
				if (x >= int(m.X(380)) && x < int(m.X(710))) { _sstate.bottom.toggle(); _dirty = true; return true; }
			}

			/* the single search field (theme-driven geometry) */
			Scene::Rect const search_rect { _sstate.search_x, _sstate.search_y, _sstate.search_w, _sstate.search_h };
			if (m.hit(search_rect, x, y)) {
				_bar_focused = true; _dirty = true;
				log("HermOS search field focused");
				return true;
			}
			if (_bar_focused) { _bar_focused = false; _dirty = true; }
			return false;
		}

		void _handle_render()
		{
			_render();
		}

		void _handle_timer()
		{
			/* re-render on any pending dirty flag (input, theme, F11, etc.) */
			if (_dirty) { _render(); _dirty = false; }

			/* drive the installer's format + copy while installing */
			if (_installer_mode && _install_phase != INST_IDLE && _install_phase != INST_DONE) {
				_install_tick();
				_render();
				_dirty = false;
			} else {
				if (_scene_ready) {
					/* Plasma-style panel reveal animation */
					/* advance by the real elapsed time, not by the tick count, so the
					 * reveal always takes ~240 ms however slow the frames are */
					unsigned long const now = _timer.elapsed_ms();
					float dt = _last_tick_ms ? float(now - _last_tick_ms) : 16.0f;
					_last_tick_ms = now;
					if (dt > 250.0f) dt = 250.0f;
					if (dt < 1.0f)   dt = 1.0f;
					bool moved = _sstate.left.update(dt);
					moved = _sstate.bottom.update(dt) || moved;
					if (moved) { _render(); _dirty = false; }
				} else {
					/* re-render while the bottom panel is mid-slide */
					int const target = _bottom_panel_visible ? (_h - _panel_h) : _h;
					if (_panel_y != target) { _render(); _dirty = false; }
				}
			}
			_timer.trigger_once(16 * 1000);   /* re-arm the next tick */
		}

		/* map a theme slot id ("0".."5") to the slot index */
		static int _cell_index(char const *id)
		{
			int v = 0;
			char const *p = id;
			while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
			return (v >= 0 && v < NUM_CELLS) ? v : -1;
		}

		/* parse a "r,g,b,a" color string into its components */
		static void _parse_color(char const *s, uint8_t &r, uint8_t &g, uint8_t &b, unsigned &a)
		{
			int vals[4] = { 0, 0, 0, 255 };
			int n = 0;
			char const *p = s;
			while (*p && n < 4) {
				int v = 0;
				while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
				vals[n++] = v;
				while (*p && *p != ',') ++p;
				if (*p == ',') ++p;
			}
			r = (uint8_t)vals[0]; g = (uint8_t)vals[1];
			b = (uint8_t)vals[2]; a = (unsigned)vals[3];
		}

		/* consume the theme_state ROM: parse the colors + the SLOT layout
		 * (x, y, w, h, chamfer) and apply them to the slot geometry.
		 * The builder is a dumb compositor: it knows slots, not apps.
		 * Uses the modern with_optional_sub_node API: the deprecated
		 * sub_node(char const *) double-raises on missing nodes (Guard
		 * destructor) and would abort the component. */
		void _handle_theme()
		{
			_theme_state->update();
			Xml_node const theme_state = _theme_state->xml();

			theme_state.with_optional_sub_node("theme", [&] (Xml_node const &theme) {

				theme.with_optional_sub_node("colors", [&] (Xml_node const &colors) {
					_parse_color(colors.attribute_value("glass",    String<32>("13,18,30,220")).string(),
					             _glass_r,    _glass_g,    _glass_b,    _glass_a);
					_parse_color(colors.attribute_value("active",   String<32>("254,186,121,255")).string(),
					             _active_r,   _active_g,   _active_b,   _active_a);
					_parse_color(colors.attribute_value("inactive", String<32>("252,252,253,255")).string(),
					             _inactive_r, _inactive_g, _inactive_b, _inactive_a);
					_parse_color(colors.attribute_value("paused", String<32>("150,150,150,255")).string(),
					             _paused_r,   _paused_g,   _paused_b,   _paused_a);
					});

					/* Step 4: the cell-icon paths from the theme */
					theme.with_optional_sub_node("icons", [&] (Xml_node const &icons) {
						_copy_icon_path(_icon_close,
						                icons.attribute_value("close",    String<64>("")).string());
						_copy_icon_path(_icon_minimize,
						                icons.attribute_value("minimize", String<64>("")).string());
						_copy_icon_path(_icon_mic,
						                icons.attribute_value("mic",      String<64>("")).string());
						_copy_icon_path(_icon_pause,
						                icons.attribute_value("pause",    String<64>("")).string());
					});

					/* Step 4: load the icon PNGs from those paths */
					_load_theme_icon(_icon_close,    _ti_close);
					_load_theme_icon(_icon_minimize, _ti_minimize);
					_load_theme_icon(_icon_mic,      _ti_mic);
					_load_theme_icon(_icon_pause,    _ti_pause);

				theme.with_optional_sub_node("layout", [&] (Xml_node const &layout) {
					layout.for_each_sub_node("slot", [&] (Xml_node const &slot) {
						String<16> const id = slot.attribute_value("id", String<16>(""));
						int const i = _cell_index(id.string());
						if (i < 0 || i >= NUM_CELLS) return;
						_geom[i].x = (int)slot.attribute_value("x", 0UL);
						_geom[i].y = (int)slot.attribute_value("y", 0UL);
						_geom[i].w = (int)slot.attribute_value("w", 0UL);
						_geom[i].h = (int)slot.attribute_value("h", 0UL);
						_chamfer[i] = (int)slot.attribute_value("chamfer", 15UL);
					});
				});

				/* the sidebar rectangle (absolute pixels from the theme) */
				theme.with_optional_sub_node("sidebar", [&] (Xml_node const &sb) {
					_sb_x = (int)sb.attribute_value("x", (unsigned long)_sb_x);
					_sb_y = (int)sb.attribute_value("y", (unsigned long)_sb_y);
					_sb_w = (int)sb.attribute_value("w", (unsigned long)_sb_w);
					_sb_h = (int)sb.attribute_value("h", (unsigned long)_sb_h);
				});

				/* the search / command bar rectangle (absolute pixels from the theme) */
				theme.with_optional_sub_node("search", [&] (Xml_node const &sch) {
					_sstate.search_x   = (int)sch.attribute_value("x",   (unsigned long)_sstate.search_x);
					_sstate.search_y   = (int)sch.attribute_value("y",   (unsigned long)_sstate.search_y);
					_sstate.search_w   = (int)sch.attribute_value("w",   (unsigned long)_sstate.search_w);
					_sstate.search_h   = (int)sch.attribute_value("h",   (unsigned long)_sstate.search_h);
					_sstate.search_tip = (int)sch.attribute_value("tip", (unsigned long)_sstate.search_tip);
				});

				/* Plasma-style panels: visibility mode + thickness (design px) */
				theme.with_optional_sub_node("panels", [&] (Xml_node const &panels) {
					auto apply = [&] (Scene::Panel &pn, Xml_node const &n) {
						String<16> const m = n.attribute_value("mode", String<16>("visible"));
						if      (m == "hidden")   pn.mode = Scene::Visibility::Hidden;
						else if (m == "autohide") pn.mode = Scene::Visibility::AutoHide;
						else                      pn.mode = Scene::Visibility::AlwaysVisible;
						pn.thickness = float(n.attribute_value("size", (unsigned long)pn.thickness));
					};
					panels.with_optional_sub_node("left",   [&] (Xml_node const &n) { apply(_sstate.left,   n); });
					panels.with_optional_sub_node("bottom", [&] (Xml_node const &n) { apply(_sstate.bottom, n); });
					_sstate.left.t   = _sstate.left.wants_shown()   ? 1.0f : 0.0f;
					_sstate.bottom.t = _sstate.bottom.wants_shown() ? 1.0f : 0.0f;
				});

				/* Step 3: the normalized snap slots (quick tiling) */
				_snap_count = 0;
				theme.with_optional_sub_node("snaps", [&] (Xml_node const &snaps) {
					snaps.for_each_sub_node("snap", [&] (Xml_node const &snap) {
						if (_snap_count >= MAX_SNAPS) return;
						Snap_def &s = _snaps[_snap_count];
						s.valid = true;
						char const *sid = snap.attribute_value("id", String<16>("")).string();
						size_t const sl = Genode::strlen(sid);
						size_t const cp = sl < 15 ? sl : 15;
						Genode::memcpy(s.id, sid, cp);
						s.id[cp] = 0;
						s.x_pct = (int)snap.attribute_value("x", 0L);
						s.w_pct = (int)snap.attribute_value("w", 0L);
						++_snap_count;
					});
				});

				/* the scene takes the exact slot rectangles from the theme;
				 * only the legacy renderer uses the fixed 3x2 tile grid */
				if (!_scene_ready) _compute_grid();

			log("HermOS theme applied: sidebar=(", _sb_x, ",", _sb_y, " ", _sb_w, "x", _sb_h, ")");
			log("HermOS theme applied: cell0=(", _geom[0].x, ",", _geom[0].y,
				    " ", _geom[0].w, "x", _geom[0].h, ") chamfer=", _chamfer[0],
				    " glass=", (int)_glass_r, ",", (int)_glass_g, ",", (int)_glass_b,
				    " active=", (int)_active_r, ",", (int)_active_g, ",", (int)_active_b);
			});

			_dirty = true;
			_render();
			}

		/* consume the bottom_dock's dock_state report:
		 * <dock_state visible="..." height="..."/> (root attributes) */
		void _handle_dock()
		{
			_dock_state_rom.update();
			try {
				Genode::Xml_node dock = _dock_state_rom.xml();
				bool visible = dock.attribute_value("visible", false);
				int height = (int)dock.attribute_value("height", 0UL);

				/* calculate the Y offset (shift cells up while visible) */
				_dock_y_offset = visible ? height : 0;

				log("HermOS dock_state: visible=", visible,
				    " offset=", _dock_y_offset);

				/* trigger the redraw so the shift appears immediately */
				_render();
			} catch (...) {
				/* the report content is not ready yet */
				_dock_y_offset = 0;
			}
		}

		/* draw the Agents & Workers content over the composited sidebar panel */
		void _draw_sidebar_content(Surface<Pixel> &s)
		{
			int const sx = _sb_x, sy = _sb_y, sw = _sb_w, sh = _sb_h;
			if (sw < 40 || sh < 80) return;

			_text(s, sx + 16, sy + 24, 15.0f, _white(), "Agents & Workers");

			_text(s, sx + 16, sy + 56, 11.0f, _muted(), "Active (3)");
			struct { char const *n; char const *st; int r, g, b; } act[3] = {
				{ "Hydroponics Agent", "Running", 80, 200, 100 },
				{ "System Maintenance", "82%", 210, 150, 60 },
				{ "Weather Monitor", "Idle", 100, 160, 220 },
			};
			for (int i = 0; i < 3; ++i) {
				int const cy = sy + 74 + i * 80;
				_fill_rrect_glass(sx + 12, cy, sw - 24, 64, 9, 22, 32, 46, 90);
				_disc(sx + 40, cy + 32, 16, act[i].r, act[i].g, act[i].b);
				_text(s, sx + 66, cy + 22, 13.0f, _white(), act[i].n);
				_text(s, sx + 66, cy + 44, 11.0f, _muted(), act[i].st);
			}

			_text(s, sx + 16, sy + 324, 11.0f, _muted(), "Inactive (5)");
			char const *ina[5] = { "Media Organizer", "Home Security", "Research Assistant",
			                       "Backup Worker", "Energy Monitor" };
			for (int i = 0; i < 5; ++i)
				_text(s, sx + 16, sy + 348 + i * 28, 12.0f, _muted(), ina[i]);
		}

		/* Step 5b: composite the agents_sidebar framebuffer at the theme's
		 * <sidebar> rectangle, BEHIND the floating cells (lower z-order) */
		void _blit_sidebar()
		{
			Cell_session *sb = _gui_root.sidebar();
			if (!sb || !sb->fb_valid()) return;
			if (_sb_w < 1 || _sb_h < 1) return;

			int const sw = _min2(sb->fb_w(), _sb_w);
			int const sh = _min2(sb->fb_h(), _sb_h);

			Pixel const *src = sb->fb_pixels();
			for (int y = 0; y < sh; ++y) {
				int const dy = _sb_y + y;
				if (dy < 0 || dy >= _h) continue;
				for (int x = 0; x < sw; ++x) {
					int const dx = _sb_x + x;
					if (dx < 0 || dx >= _w) continue;
					_px[dy * _w + dx] = src[y * sb->fb_w() + x];
				}
			}
		}

		void _handle_input()
		{
			_input.for_each_event([&] (Input::Event const &ev) {

				ev.handle_absolute_motion([&] (int x, int y) {
					_pointer_x = x * (_w - 1) / 32767;
					_pointer_y = y * (_h - 1) / 32767;
					_dirty = true;
				});

				ev.handle_relative_motion([&] (int x, int y) {
					_pointer_x += x;
					_pointer_y += y;
					if (_pointer_x < 0) _pointer_x = 0;
					if (_pointer_y < 0) _pointer_y = 0;
					if (_pointer_x >= _w) _pointer_x = _w - 1;
					if (_pointer_y >= _h) _pointer_y = _h - 1;
					_dirty = true;
				});

				/* apply an active drag/resize to the pointer's new position */
				_drag_to(_pointer_x, _pointer_y);

				/* bottom panel: show when the cursor reaches the bottom edge,
				 * hide when it leaves the panel area */
				if (_scene_ready) _scene_pointer();
				else if (_pointer_y >= _h - 10) _bottom_panel_visible = true;
				else if (_pointer_y < _h - _panel_h) _bottom_panel_visible = false;

				ev.handle_release([&] (Input::Keycode key) {
					if (key == Input::BTN_LEFT) _drag_end();
					if (key == Input::KEY_LEFTMETA || key == Input::KEY_RIGHTMETA)
						_meta_down = false;
				});

				ev.handle_press([&] (Input::Keycode key, Input::Codepoint cp) {
					if (key == Input::KEY_F11) { _chrome_hidden = !_chrome_hidden; _dirty = true; log("HermOS F11: chrome_hidden=", (int)_chrome_hidden); return; }
					if (key == Input::KEY_LEFTMETA || key == Input::KEY_RIGHTMETA) {
						_meta_down = true;
						return;
					}
					if (_meta_down) {
						_meta_down = false;   /* consumed */
						switch (key) {
						case Input::KEY_L:        _sstate.left.toggle();   _dirty = true; return;
						case Input::KEY_B:        _sstate.bottom.toggle(); _dirty = true; return;
						case Input::KEY_T:        _toggle_tile();  return;
						case Input::KEY_LEFT:     _quick_tile(0);  return;
						case Input::KEY_RIGHT:    _quick_tile(1);  return;
						case Input::KEY_UP:       _quick_tile(2);  return;
						case Input::KEY_DOWN:     _quick_tile(3);  return;
						case Input::KEY_HOME:     _quick_tile(4);  return;
						case Input::KEY_PAGEUP:   _quick_tile(5);  return;
						case Input::KEY_END:      _quick_tile(6);  return;
						case Input::KEY_PAGEDOWN: _quick_tile(7);  return;
						case Input::KEY_F:        _quick_tile(8);  return;
						case Input::KEY_R:        _quick_tile(9);  return;
						default: break;
						}
					}
					if (key == Input::BTN_LEFT) {
						_click(_pointer_x, _pointer_y);
					} else if (_bar_focused) {
						if (key == Input::KEY_ENTER) {
							_submit_prompt();
						} else if (key == Input::KEY_BACKSPACE) {
							if (_prompt_len > 0) _prompt[--_prompt_len] = 0;
							_dirty = true;
						} else if (cp.valid()) {
							unsigned const v = cp.value;
							if (v >= 32 && v < 127 && _prompt_len < 255) {
								_prompt[_prompt_len++] = char(v);
								_prompt[_prompt_len] = 0;
								_dirty = true;
							}
						}
					} else {
						log("HermOS key press: ", Input::key_name(key));
					}
				});

				/* forward the event into the hosted Cell (if any) */
				_forward_input(ev);
			});

			if (_dirty) { _render(); _dirty = false; }
		}

		Main(Env &env) : _env(env)
		{
			Framebuffer::Mode const mode = _fb.mode();
			_w = int(mode.area.w);
			_h = int(mode.area.h);
			_panel_y = _h;   /* bottom panel starts hidden (off-screen) */

			/* publish the exact screen dimensions for the theme engine */
			_screen_state.enabled(true);
			(void)_screen_state.generate_xml([&] (Xml_generator &g) {
				g.attribute("width",  _w);
				g.attribute("height", _h);
			});
			log("HermOS screen_state published: ", _w, "x", _h);

			/* installer detection: the Block session is connected LAZILY on
			   installer launch (_connect_block) — a bare ISO with no disk would
			   hang the desktop here at boot (base-lib DENIED -> sleep_forever). */
			log("HermOS block: deferred (connect on installer launch)");

			_server_mode = _config.node().attribute_value("server", false);
			log("HermOS server mode: ", _server_mode ? "YES" : "NO");

			/* apply the <vfs> config from our config ROM (mount /assets etc.) */
			_config.node().with_optional_sub_node("vfs", [&] (Node const &vfs) {
				_vfs.apply_config(vfs);
			});

			/* load the TrueType font from the /assets VFS */
			if (_read_file(_vfs, "/assets/Roboto-Regular.ttf", _asset_heap, _font_data, _font_size))
				_font.load(_font_data);
			log("HermOS font: ", _font.ok()
			    ? "Roboto-Regular loaded from /assets"
			    : "FONT LOAD FAILED");

			/* load the PNG icons from the /assets VFS */
			for (Icon &ic : _icons) {
				if (_read_file(_vfs, ic.path, _asset_heap, ic.data, ic.size))
					log("HermOS icon loaded: ", ic.name);
				else
					log("HermOS icon MISSING: ", ic.name);
			}

			/* load the app registry (populates the left dock) */
			_load_registry();

			/* bind the native stb allocator to the asset heap before decoding */
			_stb_alloc = &_asset_heap;

			/* load the real wallpaper image from /assets/wallpaper.png */
			{
				void *data = nullptr; size_t size = 0;
				if (_read_file(_vfs, "/assets/wallpaper.png", _asset_heap, data, size)) {
					Png_rgb const img = load_png_rgb(data, size);
					if (img.px && img.w > 0 && img.h > 0) {
						_wall_rgb = (uint8_t *)_asset_heap.alloc((size_t)_w * _h * 3);
						/* aspect-fill (cover) scale, centered crop */
						float const sc = (float(_w) / img.w > float(_h) / img.h)
						               ? float(_w) / img.w : float(_h) / img.h;
						float const cw = img.w * sc, ch = img.h * sc;
						float const ox = (cw - _w) / 2, oy = (ch - _h) / 2;
						for (int y = 0; y < _h; ++y)
							for (int x = 0; x < _w; ++x) {
								int const ix = int((x + ox) * float(img.w) / cw);
								int const iy = int((y + oy) * float(img.h) / ch);
								if (ix < 0 || iy < 0 || ix >= img.w || iy >= img.h) continue;
								int const di = (y * _w + x) * 3;
								int const si = (iy * img.w + ix) * 3;
								_wall_rgb[di]     = img.px[si];
								_wall_rgb[di + 1] = img.px[si + 1];
								_wall_rgb[di + 2] = img.px[si + 2];
							}
						stbi_image_free(img.px);
						log("HermOS wallpaper: loaded /assets/wallpaper.png (",
						    img.w, "x", img.h, " -> ", _w, "x", _h, ")");
					} else {
						log("HermOS wallpaper: /assets/wallpaper.png decode failed");
					}
				} else {
					log("HermOS wallpaper: /assets/wallpaper.png missing (dark background)");
				}
			}

			_pointer_x = _w / 2;
			_pointer_y = _h / 2;

			_init_scene();

			/* read the wall clock (best-effort; the desktop still boots without it) */
			try { _rtc.construct(_env); }
			catch (...) { log("HermOS desktop_builder: RTC unavailable"); }

			if (_rtc.constructed()) {
				Rtc::Timestamp const ts = _rtc->current_time();
				log("HermOS RTC time: ", ts);
			}

			log("HermOS desktop_builder owns framebuffer ", _w, "x", _h,
			    " (no nitpicker in this composition)");

			_input.sigh(_input_sigh);

			_timer.sigh(_timer_sigh);
			_timer.trigger_once(16 * 1000);   /* start the animation tick loop */

			_env.parent().announce(_env.ep().manage(_gui_root));
			log("HermOS desktop_builder announces 'Gui' service for hosted Cells");

			_apps_report.enabled(true);
			_emit_apps_config();
			log("HermOS desktop_builder launching app sandbox (2 cells)");

			/* Phase 2 bridge: subscribe to the reply, then send the prompt */
			try {
				_ai_command.construct(_env, "ai_command");
				_ai_command->sigh(_ai_sigh);
				log("HermOS desktop_builder: ai_command ROM connected");
			} catch (...) {
				log("HermOS desktop_builder: ai_command ROM unavailable");
			}

			/* Theme bridge: consume the theme_engine's report as a ROM */
			try {
				_theme_state.construct(_env, "theme_state");
				_theme_state->sigh(_theme_sigh);
				log("HermOS desktop_builder: theme_state ROM connected");
				_handle_theme();   /* apply the already-published theme (if any) */
			} catch (...) {
				log("HermOS desktop_builder: theme_state ROM unavailable (using defaults)");
			}

			/* Dock bridge: the bottom_dock's dock_state ROM + the signal
			 * handler wiring, exactly per the Step 4 pattern */
			_dock_state_rom.sigh(_dock_sigh);
			log("HermOS desktop_builder: dock_state ROM connected");
			_handle_dock();   /* apply the already-published state (if any) */

			_ui_intent.enabled(true);
			log("HermOS desktop_builder: ui_intent reporter ready (prompts sent on demand)");

			/* Step 2: spawn theme_engine as a child with the exact screen
			 * size handed over in its generated config. */
			try {
				_theme_child.construct(_env, _w, _h);
				log("HermOS desktop_builder: theme_engine child spawned (",
				    _w, "x", _h, ")");
			} catch (Genode::Out_of_ram) {
				Genode::error("Theme spawn failed: Out_of_ram");
			} catch (Genode::Out_of_caps) {
				Genode::error("Theme spawn failed: Out_of_caps");
			} catch (Genode::Service_denied) {
				Genode::error("Theme spawn failed: Service_denied");
			} catch (Genode::Insufficient_ram_quota) {
				Genode::error("Theme spawn failed: Insufficient_ram_quota");
			} catch (Genode::Insufficient_cap_quota) {
				Genode::error("Theme spawn failed: Insufficient_cap_quota");
			} catch (...) {
				Genode::error("Theme spawn failed: Unknown exception");
			}

			/* Step 5b: spawn the agents_sidebar child (ram 32M, caps 200);
			 * its Gui session is served locally by our own Cell_root. */
			try {
				_sidebar_child.construct(_env, _gui_root);
				log("HermOS desktop_builder: agents_sidebar child spawned");
			} catch (Genode::Out_of_ram) {
				Genode::error("Sidebar spawn failed: Out_of_ram");
			} catch (Genode::Out_of_caps) {
				Genode::error("Sidebar spawn failed: Out_of_caps");
			} catch (Genode::Service_denied) {
				Genode::error("Sidebar spawn failed: Service_denied");
			} catch (Genode::Insufficient_ram_quota) {
				Genode::error("Sidebar spawn failed: Insufficient_ram_quota");
			} catch (Genode::Insufficient_cap_quota) {
				Genode::error("Sidebar spawn failed: Insufficient_cap_quota");
			} catch (...) {
				Genode::error("Sidebar spawn failed: Unknown exception");
			}

			_render();

			log("HermOS desktop_builder ready: input connected, Cells interactive");
		}
	};
}

void Component::construct(Genode::Env &env)
{
	/* the scene's text arena is a global object */
	env.exec_static_constructors();

	static Hermos::Main main(env);
}
