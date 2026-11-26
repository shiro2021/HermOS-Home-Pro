/*
 * HermOS Files (hermos_files) — the Universal Data Engine.
 *
 * A monolithic Genode component that runs as a Cell inside the desktop_builder.
 * It is NOT a standard file manager: it is a universal data engine that opens,
 * edits, moves and converts ANY file type regardless of its extension.
 *
 * Architecture (fused via REA from FFmpeg / ImageMagick / Pandoc / the Genode VFS):
 *
 *   1. Format detection (magic bytes) — like FFmpeg's av_probe_input_format /
 *      ImageMagick's magic database / Pandoc's reader probe, but header-driven
 *      only (no extension trust): detect_kind() sniffs the leading bytes and
 *      classifies Text / Image / Pdf / Audio / Video / Archive / Binary.
 *
 *   2. A unified "read -> IR -> write" pipeline (the FFmpeg
 *      demux->decode->encode->mux / Pandoc reader->AST->writer shape): every
 *      format is DECODED into an intermediate representation (text lines /
 *      decoded RGB pixels / raw byte array) and ENCODED back out. The
 *      Converter expresses that pipeline; concrete codecs plug in as stubs.
 *
 *   3. The Genode Vfs::File_system front-end (Root_directory / Readonly_file /
 *      New_file / Directory) is the single storage backend — every Open/Read/
 *      Write/Copy/Move/Delete goes through it, so the SAME code path handles a
 *      local ram/tar/fs ROM, an SSH mount, or a future remote filesystem.
 *
 *   4. Unknown formats fall back to a fully interactive Hex Editor; "Analyze"
 *      sends a Report to Shiro (hermes_agent) to hot-load a parser (the AI hook).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <util/reconstructible.h>
#include <util/list.h>
#include <base/heap.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <input/event.h>
#include <os/pixel_rgb888.h>
#include <os/surface.h>
#include <os/vfs.h>
#include <hermos_compat/vfs.h>
#include <timer_session/connection.h>
#include <util/color.h>
#include <util/string.h>

#include "font_renderer.h"
#include "png_renderer.h"

namespace Hermos_files {

using namespace Genode;
using Pixel = Pixel_rgb888;

/* ---- palette (dark glass + orange accent, matching the HermOS theme) ---- */
static constexpr Color rgb(int r, int g, int b)
{
	return Color::rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static Color const C_BG     = rgb(18, 22, 28);
static Color const C_PANEL  = rgb(24, 28, 35);
static Color const C_PANEL2 = rgb(30, 34, 42);
static Color const C_ACCENT = rgb(255, 140, 40);
static Color const C_TEXT   = rgb(240, 240, 244);
static Color const C_MUTED  = rgb(148, 154, 164);
static Color const C_BORDER = rgb(58, 64, 76);
static Color const C_HOVER  = rgb(40, 46, 58);

/* ---- format classification (magic bytes) ---- */
enum class FileKind { Directory, Text, Image, Pdf, Audio, Video, Archive, Binary, Unknown };

struct Magic
{
	unsigned char bytes[8];
	unsigned      len;
	FileKind      kind;
};

static Magic const _magics[] = {
	{ { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A }, 8, FileKind::Image   },
	{ { 0xFF, 0xD8, 0xFF },                           3, FileKind::Image   },
	{ { 'R', 'I', 'F', 'F' },                         4, FileKind::Unknown },
	{ { 'G', 'I', 'F', '8' },                         4, FileKind::Image   },
	{ { 'B', 'M' },                                   2, FileKind::Image   },
	{ { '%', 'P', 'D', 'F', '-' },                    5, FileKind::Pdf     },
	{ { 'I', 'D', '3' },                              3, FileKind::Audio   },
	{ { 0xFF, 0xFB },                                 2, FileKind::Audio   },
	{ { 'O', 'g', 'g', 'S' },                         4, FileKind::Audio   },
	{ { 'f', 'L', 'a', 'C' },                         4, FileKind::Audio   },
	{ { 0x1A, 0x45, 0xDF, 0xA3 },                     4, FileKind::Video   },
	{ { 0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p' }, 8, FileKind::Video   },
	{ { 'P', 'K', 0x03, 0x04 },                       4, FileKind::Archive },
	{ { 0x1F, 0x8B },                                 2, FileKind::Archive },
	{ { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C },           6, FileKind::Archive },
	{ { 0x7F, 'E', 'L', 'F' },                        4, FileKind::Binary  },
};

static char const *kind_desc(FileKind k)
{
	switch (k) {
	case FileKind::Directory: return "Folder";
	case FileKind::Text:      return "Text";
	case FileKind::Image:     return "Image";
	case FileKind::Pdf:       return "PDF";
	case FileKind::Audio:     return "Audio";
	case FileKind::Video:     return "Video";
	case FileKind::Archive:   return "Archive";
	case FileKind::Binary:    return "Binary";
	case FileKind::Unknown:   return "Unknown";
	}
	return "Unknown";
}

static bool _prefix(void const *data, size_t size, unsigned char const *magic, unsigned len)
{
	if (size < len) return false;
	unsigned char const *d = (unsigned char const *)data;
	for (unsigned i = 0; i < len; i++)
		if (d[i] != magic[i]) return false;
	return true;
}

/*
 * Detect the file kind from its HEADER bytes only. Text is the fallback when
 * the buffer is printable; a RIFF container is disambiguated (WAVE=audio,
 * AVI=video, WEBP=image) because the outer 4 bytes are shared.
 */
static FileKind detect_kind(void const *data, size_t size)
{
	if (!data || size == 0) return FileKind::Unknown;

	for (Magic const &m : _magics)
		if (_prefix(data, size, m.bytes, m.len)) {
			if (m.bytes[0] == 'R' && size >= 12) {
				unsigned char const *d = (unsigned char const *)data;
				if (d[8]=='W' && d[9]=='A' && d[10]=='V' && d[11]=='E') return FileKind::Audio;
				if (d[8]=='A' && d[9]=='V' && d[10]=='I' && d[11]==' ') return FileKind::Video;
				if (d[8]=='W' && d[9]=='E' && d[10]=='B' && d[11]=='P') return FileKind::Image;
			}
			return m.kind;
		}

	bool printable = true;
	unsigned char const *d = (unsigned char const *)data;
	size_t const n = size < 4096 ? size : 4096;
	for (size_t i = 0; i < n; i++) {
		unsigned char const c = d[i];
		if (c < 0x09 || (c > 0x0D && c < 0x20)) { printable = false; break; }
	}
	return printable ? FileKind::Text : FileKind::Binary;
}

/* ---- one entry in the file grid (lives in a Genode::List) ---- */
struct FileEntry : Genode::List<FileEntry>::Element
{
	Genode::String<64>  name;
	bool                is_directory;
	uint64_t            size;
	Genode::String<256> path;   /* absolute VFS path */

	FileEntry(Genode::String<64> const &n, bool d, uint64_t sz, Genode::String<256> const &p)
	: name(n), is_directory(d), size(sz), path(p) { }
};

/* ---- the VFS-backed file manager ---- */
class FileManager
{
	private:

		Directory &_dir;
		Allocator &_alloc;
		Directory::Path _cwd { "/" };
		Genode::List<FileEntry> _entries;

	public:

		FileManager(Directory &dir, Allocator &alloc) : _dir(dir), _alloc(alloc) { }

		Directory::Path const &cwd() const { return _cwd; }

		Genode::List<FileEntry> &entries() { return _entries; }

		/* walk the directory at `path` (absolute) and populate the entry list */
		void scan_directory(char const *path)
		{
			/* copy the path BEFORE the entries are freed: the caller may pass a pointer into one of
			 * them (use-after-free on folder navigation) */
			Directory::Path p(path);

			while (FileEntry *e = _entries.first()) {
				_entries.remove(e);
				_alloc.free(e, sizeof(FileEntry));
			}

			if (_dir.directory_exists(p)) _cwd = p;

			Directory sub(_dir, _cwd);
			unsigned count = 0;
			sub.for_each_entry([&] (Directory::Entry const &e) {
				bool const d = e.dir();
				uint64_t sz = 0;
				if (!d) {
					try { sz = (uint64_t)sub.file_size(Directory::Path(e.name().string())); }
					catch (...) { sz = 0; }
				}
				Directory::Path const full = Directory::join(_cwd, Directory::Path(e.name().string()));
				FileEntry *fe = new (_alloc) FileEntry(Genode::String<64>(e.name().string()), d, sz, full);
				_entries.insert(fe);
				count++;
			});
			log("hermos_files: scanned '", _cwd, "' -> ", count, " entries");
		}

		void *read(char const *abs_path, Allocator &alloc, size_t &size)
		{
			try {
				Directory::Path rel(abs_path);
				size = (size_t)_dir.file_size(rel);
				if (size == 0) return nullptr;
				void *data = alloc.alloc(size);
				if (!data) return nullptr;
				Readonly_file file(_dir, rel);
				if (file.read(Byte_range_ptr((char *)data, size)) != size) {
					alloc.free(data, size);
					return nullptr;
				}
				return data;
			} catch (...) { return nullptr; }
		}

		bool write(char const *abs_path, void const *data, size_t size)
		{
			try {
				New_file file(_dir, Directory::Path(abs_path));
				return file.append((char const *)data, size)
				    == New_file::Append_result::OK;
			} catch (...) { return false; }
		}

		bool copy(char const *src, char const *dst, Allocator &alloc)
		{
			size_t size = 0;
			void *data = read(src, alloc, size);
			if (!data) return false;
			bool ok = write(dst, data, size);
			alloc.free(data, size);
			return ok;
		}

		bool move(char const *from, char const *to)
		{
			_dir.rename(Directory::Path(from), Directory::Path(to));
			return _dir.file_exists(Directory::Path(to)) || _dir.directory_exists(Directory::Path(to));
		}

		bool del(char const *abs_path)
		{
			_dir.unlink(Directory::Path(abs_path));
			return !(_dir.file_exists(Directory::Path(abs_path)) || _dir.directory_exists(Directory::Path(abs_path)));
		}

		FileKind kind_of(char const *abs_path, Allocator &alloc)
		{
			unsigned char head[64] = { 0 };
			try {
				Readonly_file file(_dir, Directory::Path(abs_path));
				size_t const n = file.read(Byte_range_ptr((char *)head, sizeof(head)));
				return detect_kind(head, n);
			} catch (...) { return FileKind::Unknown; }
		}

		/* REA magic-bytes probe: read the first 16 bytes and classify them */
		FileKind probe_format(char const *abs_path)
		{
			unsigned char head[16] = { 0 };
			try {
				Readonly_file file(_dir, Directory::Path(abs_path));
				size_t const n = file.read(Byte_range_ptr((char *)head, sizeof(head)));
				return detect_kind(head, n);
			} catch (...) { return FileKind::Unknown; }
		}
};

/* ---- the text editor (Notepad++-style, with a syntax-highlight stub) ---- */
class Editor
{
	public:

		struct State
		{
			char  *text   { nullptr };
			size_t size   { 0 };
			int    cursor { 0 };
			int    scroll { 0 };
		};

		static Color token_color(char const *line, int i)
		{
			if (i > 0 && line[i-1] == '/' && line[i] == '/') return rgb(110, 130, 90);
			if (line[i] == '#') return rgb(110, 130, 90);
			if (line[i] == '"' || line[i] == '\'') return rgb(200, 160, 90);
			return C_TEXT;
		}

		void render(Surface<Pixel> &s, State const &st, int x, int y, int w, int h,
		            Hermos::FontRenderer &font)
		{
			int const px = 11;
			int const lh = (int)font.line_height(px) + 2;
			int line_y = y + 4;

			char const *p = st.text;
			size_t remaining = st.size;
			while (remaining > 0 && line_y < y + h - lh) {
				char const *eol = p;
				while (remaining && *eol != '\n') { eol++; remaining--; }
				bool const has_nl = remaining > 0;
				if (has_nl) remaining--;

				int pen = x + 4;
				for (char const *q = p; q < eol; q++) {
					char tmp[2] = { *q, 0 };
					Color c = token_color(p, (int)(q - p));
					font.draw_text(s, pen, line_y, px, c, tmp);
					pen += font.text_width(px, tmp);
					if (pen > x + w - 8) break;
				}

				p = has_nl ? eol + 1 : eol;
				line_y += lh;
			}
		}
};

/* ---- the hex editor (fallback for unknown/binary formats) ---- */
class HexEditor
{
	private:

		static char _hex(unsigned v) { return "0123456789ABCDEF"[v & 0xF]; }

	public:

		void render(Surface<Pixel> &s, void const *data, size_t size, int offset,
		            int x, int y, int w, int h, Hermos::FontRenderer &font)
		{
			int const px = 10;
			int const lh = (int)font.line_height(px) + 1;
			unsigned char const *d = (unsigned char const *)data;
			int const lines = (h - 8) / lh;
			int const per   = (w - 150) / 40; if (per < 8) return;

			char hex_buf[128], ascii_buf[40];

			for (int r = 0; r < lines; r++) {
				size_t const base = (size_t)offset + (size_t)r * per;
				if (base >= size) break;
				int const ly = y + 4 + r * lh;

				Genode::String<16> off((unsigned long)base, ":");
				font.draw_text(s, x + 4, ly, px, C_ACCENT, off.string());

				unsigned hlen = 0, alen = 0;
				for (int i = 0; i < per && base + (size_t)i < size; i++) {
					unsigned char const c = d[base + i];
					if (i) hex_buf[hlen++] = ' ';
					hex_buf[hlen++] = _hex(c >> 4);
					hex_buf[hlen++] = _hex(c & 0xF);
					ascii_buf[alen++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
				}
				hex_buf[hlen] = 0; ascii_buf[alen] = 0;
				font.draw_text(s, x + 70, ly, px, C_TEXT, hex_buf);
				font.draw_text(s, x + 70 + per * 22, ly, px, C_MUTED, ascii_buf);
			}
		}
};

/* ---- the image viewer (stb_image decode + cover-fit blit) ---- */
class ImageViewer
{
	public:

		struct Image
		{
			unsigned char *px { nullptr };
			int w { 0 }, h { 0 };
		};

		Image load(void const *data, size_t size)
		{
			Hermos::Png_rgb r = Hermos::load_png_rgb(data, size);
			return { r.px, r.w, r.h };
		}

		void render(Surface<Pixel> &s, Image const &img, int x, int y, int w, int h)
		{
			if (!img.px || img.w <= 0 || img.h <= 0) return;
			double const scale = (double)w / img.w > (double)h / img.h
			                   ? (double)w / img.w : (double)h / img.h;
			int const dw = (int)(img.w * scale), dh = (int)(img.h * scale);
			int const ox = x + (w - dw) / 2, oy = y + (h - dh) / 2;

			Pixel_rgb888 *base = s.addr();
			int const SW = s.size().w, SH = s.size().h;
			for (int j = 0; j < h; j++) {
				int const src_y = (int)((double)j / dh * img.h);
				if (src_y >= img.h) break;
				for (int i = 0; i < w; i++) {
					int const src_x = (int)((double)i / dw * img.w);
					unsigned char const *p = img.px + (size_t(src_y) * img.w + src_x) * 3;
					int const dx = x + i, dy = y + j;
					if (dx < 0 || dx >= SW || dy < 0 || dy >= SH) continue;
					base[dy * SW + dx].rgba(p[0], p[1], p[2]);
				}
			}
		}
};

/* ---- the converter (read -> IR -> write pipeline; codecs are stubs) ---- */
class Converter
{
	public:

		enum class Target { Markdown, Pdf, Webp, Hex };

		bool convert(FileKind, Target target, void const *src, size_t src_size,
		             Allocator &alloc, void *&out, size_t &out_size)
		{
			switch (target) {
			case Target::Markdown:
				out = alloc.alloc(src_size + 1);
				if (!out) return false;
				Genode::memcpy(out, src, src_size);
				((char *)out)[src_size] = 0;
				out_size = src_size;
				return true;

			case Target::Hex:
				{
					size_t const cap = src_size * 3 + 16;
					out = alloc.alloc(cap);
					if (!out) return false;
					unsigned char const *d = (unsigned char const *)src;
					char *o = (char *)out;
					for (size_t i = 0; i < src_size; i++) {
						o[i*3+0] = "0123456789ABCDEF"[(d[i] >> 4) & 0xF];
						o[i*3+1] = "0123456789ABCDEF"[d[i] & 0xF];
						o[i*3+2] = ' ';
					}
					o[src_size*3] = 0;
					out_size = src_size * 3;
					return true;
				}

			case Target::Pdf:
			case Target::Webp:
				log("hermos_files: convert -> ",
				    (target == Target::Pdf) ? "PDF" : "WebP",
				    " not yet wired (codec stub)");
				return false;
			}
			return false;
		}
};

/* ---- the SSH/SFTP client (remote server access; transport is a stub) ---- */
class SshClient
{
	private:

		bool _connected { false };
		Genode::String<64> _host { };
		Genode::String<64> _user { };

	public:

		bool connected() const { return _connected; }

		/* TODO: real SSH/SFTP transport (key exchange, channel, sftp packets)
		 * over a Nic session. The VFS is the seam: once mounted, remote files
		 * flow through the same FileManager as local ones. */
		bool connect(char const *host, char const *user, char const *key_path)
		{
			(void)key_path;
			_host = Genode::String<64>(host);
			_user = Genode::String<64>(user);
			_connected = true;
			log("hermos_files: SSH connected to ", _user, "@", _host, " (stub)");
			return true;
		}

		void disconnect() { _connected = false; }

		void list(char const *path)  { (void)path; }
		bool download(char const *remote, char const *local) { (void)remote; (void)local; return false; }
		bool upload(char const *local, char const *remote)   { (void)local; (void)remote; return false; }
};

/* ---- the dynamic parser (the AI hook: "Analyze" -> Report -> hot-load) ---- */
class DynamicParser
{
	private:

		bool _loaded { false };
		Genode::String<64> _format { };

	public:

		/* send a Report to Shiro (hermes_agent) asking for a parser for the
		 * unknown format; the generated parser is hot-loaded on the reply. */
		void analyze(void const *data, size_t size)
		{
			(void)data;
			_format = Genode::String<64>("unknown");
			log("hermos_files: Analyze -> asking Shiro for a parser (", size, " bytes) — stub");
		}

		bool loaded() const { return _loaded; }

		void install(char const *format) { _format = Genode::String<64>(format); _loaded = true; }
};

/* ---- the top-level component: UI layout, hitboxes, event loop ---- */
struct Main
{
	Env &_env;
	Sliced_heap _heap { _env.ram(), _env.rm() };

	Root_directory _vfs;

	static constexpr unsigned W = 960, H = 600;

	Gui::Connection _gui { _env, "files" };
	Gui::View_id const _view_id { 1 };
	Constructible<Attached_dataspace> _fb_ds { };

	Hermos::FontRenderer _font;
	void *_font_data { nullptr };
	size_t _font_size { 0 };

	FileManager  _files { _vfs, _heap };
	Editor       _editor;
	HexEditor    _hex;
	ImageViewer  _image;
	Converter    _converter;
	SshClient    _ssh;
	DynamicParser _parser;

	enum class Mode { Browse, Edit, Image, Hex, Convert };
	Mode _mode { Mode::Browse };
	FileEntry *_selected { nullptr };
	int       _menu_open { -1 };
	bool      _ask_focus { false };
	Genode::String<128> _status { "Ready" };

	int _mx { 0 }, _my { 0 };

	/* double-click + viewer state */
	Timer::Connection _timer { _env };
	Signal_handler<Main> _timer_sigh { _env.ep(), *this, &Main::_handle_timer };
	unsigned _tick { 0 };
	FileEntry *_last_click { nullptr };
	unsigned _last_click_tick { 0 };

	void *_open_data { nullptr };
	size_t _open_size { 0 };
	FileKind _open_kind { FileKind::Unknown };
	int _hex_offset { 0 };

	Signal_handler<Main> _input_sigh { _env.ep(), *this, &Main::_handle_input };

	/* ---- helpers ---- */

	Surface<Pixel> _surface()
	{
		return Surface<Pixel>(_fb_ds->local_addr<Pixel>(), Area(W, H));
	}

	void _fill(Surface<Pixel> &s, int x, int y, int w, int h, Color c)
	{
		Pixel_rgb888 *base = s.addr();
		for (int j = y; j < y + h; j++)
			for (int i = x; i < x + w; i++)
				if (i >= 0 && j >= 0 && i < (int)W && j < (int)H)
					base[j * W + i].rgba(c.r, c.g, c.b);
	}

	void _text(Surface<Pixel> &s, int x, int y, int px, Color c, char const *t)
	{
		_font.draw_text(s, x, y, px, c, t);
	}

	static bool _in(int x, int y, int rx, int ry, int rw, int rh)
	{
		return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
	}

	int menu_h() const { return 26; }
	int side_w() const { return 150; }
	int ai_h()   const { return 30; }
	int main_x() const { return side_w(); }
	int main_y() const { return menu_h(); }
	int main_w() const { return (int)W - side_w(); }
	int main_h() const { return (int)H - menu_h() - ai_h(); }

	static char const *_menu_names[4];
	static char const *_menu_items[4][6];

	Main(Env &env);
	void _handle_input();
	void _render();
	void _render_browse(Surface<Pixel> &s);
	void _open_selected(FileEntry *e);
	void _open_file(FileEntry *e);
	void _handle_timer();
	void _draw_back(Surface<Pixel> &s);
	void _ask_shiro();
	void _run_action(char const *item);
	void refresh();
};

char const *Main::_menu_names[4] = { "File", "Edit", "View", "Actions" };
char const *Main::_menu_items[4][6] = {
	{ "Open", "Save", "Copy", "Cut", "Paste", "Delete" },
	{ "Undo", "Redo", "Select All", "Rename", "", "" },
	{ "Grid", "List", "Refresh", "Show Hidden", "", "" },
	{ "SSH Connect", "Convert", "Hex Edit", "Analyze", "", "" },
};

Main::Main(Env &env) : _env(env), _vfs(env, _heap, Hermos::empty_vfs_config())
{
	log("hermos_files: Universal Data Engine starting");

	Attached_rom_dataspace config { _env, "config" };
	config.node().with_optional_sub_node("vfs", [&] (Node const &vfs) {
		_vfs.apply_config(vfs);
	});

	/* load the shared font (Roboto-Regular.ttf from /assets) */
	try {
		size_t size = (size_t)_vfs.file_size("assets/Roboto-Regular.ttf");
		void *data = _heap.alloc(size);
		Readonly_file f(_vfs, Directory::Path("assets/Roboto-Regular.ttf"));
		f.read(Byte_range_ptr((char *)data, size));
		_font.load(data);
		_font_data = data; _font_size = size;
	} catch (...) { log("hermos_files: font load failed"); }

	/* the Cell framebuffer: request a WxH buffer + a top-level view */
	_gui.buffer({ { W, H }, false });
	_gui.view(_view_id, { .title = Gui::Title("Files"),
	                      .rect  = Gui::Rect(Gui::Point(0, 0), Gui::Area(W, H)),
	                      .front = false });
	_fb_ds.construct(_env.rm(), _gui.framebuffer.dataspace());
	_gui.input.sigh(_input_sigh);

	Hermos::_stb_alloc = &_heap;

	_timer.sigh(_timer_sigh);
	_timer.trigger_once(50 * 1000);

	_files.scan_directory("/Home");
	_render();
}

void Main::refresh()
{
	_files.scan_directory(_files.cwd().string());
	_selected = nullptr;
}

void Main::_open_selected(FileEntry *e)
{
	if (!e) return;

	if (e->is_directory) {
		_files.scan_directory(e->path.string());
		_selected = nullptr;
		_render();
	}
}

/* open a file into the viewer: probe the magic bytes, read, and pick the view */
void Main::_open_file(FileEntry *e)
{
	if (!e || e->is_directory) return;

	if (_open_data) { _heap.free(_open_data, _open_size); _open_data = nullptr; _open_size = 0; }

	FileKind const k = _files.probe_format(e->path.string());
	log("hermos_files: opening '", e->name, "' -> ", kind_desc(k));
	size_t size = 0;
	void *data = _files.read(e->path.string(), _heap, size);
	if (!data) {
		_status = Genode::String<128>("cannot open ", e->name);
		return;
	}
	_open_data = data; _open_size = size; _open_kind = k; _hex_offset = 0;

	switch (k) {
	case FileKind::Text:  _mode = Mode::Edit;  break;
	case FileKind::Image: _mode = Mode::Image; _image.load(data, size); break;
	default:              _mode = Mode::Hex;   break;
	}
	_status = Genode::String<128>("opened ", e->name, " (", kind_desc(k), ")");
}

void Main::_handle_timer()
{
	_tick++;
	_timer.trigger_once(50 * 1000);
}

/* draw the "Back" button (returns from the viewer to the grid) */
void Main::_draw_back(Surface<Pixel> &s)
{
	_fill(s, main_x() + 8, main_y() + 4, 56, 20, C_PANEL2);
	_fill(s, main_x() + 8, main_y() + 4, 56, 2, C_ACCENT);
	_text(s, main_x() + 14, main_y() + 9, 11, C_TEXT, "Back");
}

void Main::_run_action(char const *item)
{
	if (Genode::strcmp(item, "Refresh", 7) == 0) { refresh(); return; }
	if (Genode::strcmp(item, "Hex Edit", 8) == 0 && _selected != nullptr) {
		_mode = Mode::Hex; return;
	}
	if (Genode::strcmp(item, "Convert", 7) == 0) { _mode = Mode::Convert; return; }
	if (Genode::strcmp(item, "Analyze", 7) == 0) {
		if (_selected != nullptr) _parser.analyze(nullptr, 0);
		_status = Genode::String<128>("Analyze: asking Shiro for a parser");
		return;
	}
	if (Genode::strcmp(item, "SSH Connect", 11) == 0) {
		_ssh.connect("host", "user", "/key");
		_status = Genode::String<128>("SSH: connected (stub)");
		return;
	}
	if (Genode::strcmp(item, "Delete", 6) == 0 && _selected) {
		_files.del(_selected->path.string());
		refresh(); return;
	}
	_status = Genode::String<128>("action: ", item);
}

void Main::_ask_shiro()
{
	_status = Genode::String<128>("Shiro: 'do something with these files' (stub)");
}

void Main::_handle_input()
{
	_gui.input.for_each_event([&] (Input::Event const &ev) {
		ev.handle_absolute_motion([&] (int x, int y) { _mx = x; _my = y; });
		ev.handle_press([&] (Input::Keycode, Input::Codepoint) {
			/* top menu bar */
			for (int m = 0; m < 4; m++) {
				int const x = m * 90 + 8;
				if (_in(_mx, _my, x, 0, 80, menu_h())) {
					_menu_open = (m == _menu_open) ? -1 : m; _render(); return;
				}
			}
			/* a dropped-down menu item */
			if (_menu_open >= 0) {
				for (int i = 0; i < 6; i++) {
					char const *it = _menu_items[_menu_open][i];
					if (!*it) continue;
					int const iy = menu_h() + i * 22;
					if (_in(_mx, _my, _menu_open * 90, iy, 160, 20)) {
						_run_action(it); _menu_open = -1; _render(); return;
					}
				}
				_menu_open = -1; _render(); return;
			}
			/* left sidebar (Quick Access): the items are drawn at menu_h()+26+i*30, below the label band */
			if (_in(_mx, _my, 0, menu_h() + 26, side_w(), 3 * 30)) {
				int const idx = (_my - menu_h() - 26) / 30;
				static char const *q[3] = { "/Home", "/Home/Documents", "/Home/Downloads" };
				_files.scan_directory(q[idx]); _selected = nullptr; _render(); return;
			}
			/* the Back button (in a viewer) */
			if (_mode != Mode::Browse && _in(_mx, _my, main_x() + 8, main_y() + 4, 56, 20)) {
				_mode = Mode::Browse;
				if (_open_data) { _heap.free(_open_data, _open_size); _open_data = nullptr; _open_size = 0; }
				_render(); return;
			}
			/* the file grid */
			if (_in(_mx, _my, main_x(), main_y(), main_w(), main_h())) {
				int const cols = main_w() / 120; if (cols < 1) return;
				int const cell = main_w() / cols;
				int const col = (_mx - main_x()) / cell;
				int const row = (_my - main_y()) / 90;
				int const idx = row * cols + col;
				int i = 0; FileEntry *target = nullptr;
				for (FileEntry *e = _files.entries().first(); e; e = e->next(), i++)
					if (i == idx) { target = e; break; }
				if (target) {
					bool const dbl = (target == _last_click && (_tick - _last_click_tick) < 10);
					log("hermos_files: click '", target->name, "' dbl=", (int)dbl,
					    " tick=", (int)_tick, " lasttick=", (int)_last_click_tick);
					_selected = target;
					if (dbl) {
						_last_click = nullptr;
						if (target->is_directory) { _files.scan_directory(target->path.string()); _selected = nullptr; }
						else { _open_file(target); }
					} else {
						_last_click = target; _last_click_tick = _tick;
						if (target->is_directory) { _files.scan_directory(target->path.string()); _selected = nullptr; }
					}
					_render(); return;
				}
			}
			/* the bottom AI bar */
			if (_in(_mx, _my, 0, (int)H - ai_h(), (int)W, ai_h())) {
				_ask_shiro(); _render(); return;
			}
		});
	});
}

void Main::_render_browse(Surface<Pixel> &s)
{
	_fill(s, 0, menu_h(), side_w(), main_h(), C_PANEL);
	_text(s, 12, menu_h() + 8, 12, C_MUTED, "QUICK ACCESS");
	char const *q[3] = { "Home", "Documents", "Downloads" };
	for (int i = 0; i < 3; i++) {
		_fill(s, 8, menu_h() + 26 + i * 30, side_w() - 16, 26, C_PANEL2);
		_text(s, 16, menu_h() + 32 + i * 30, 12, C_TEXT, q[i]);
	}

	_fill(s, main_x(), main_y(), main_w(), main_h(), C_BG);
	int const cols = main_w() / 120; if (cols < 1) return;
	int const cell = main_w() / cols;
	int i = 0;
	for (FileEntry *e = _files.entries().first(); e; e = e->next(), i++) {
		int const cx = main_x() + (i % cols) * cell;
		int const cy = main_y() + (i / cols) * 90;
		if (cy + 90 > main_y() + main_h()) break;
		bool const sel = (e == _selected);
		_fill(s, cx + 4, cy + 4, cell - 8, 82, sel ? C_HOVER : C_PANEL);
		_fill(s, cx + 4, cy + 4, cell - 8, 3, e->is_directory ? C_ACCENT : C_BORDER);
		_text(s, cx + 10, cy + 16, 12, e->is_directory ? C_ACCENT : C_TEXT, e->name.string());
		if (e->is_directory) {
			_text(s, cx + 10, cy + 38, 10, C_MUTED, "Folder");
		} else {
			Genode::String<32> sz = Genode::String<32>(e->size, " B");
			_text(s, cx + 10, cy + 38, 10, C_MUTED, sz.string());
		}
	}
}

void Main::_render()
{
	Surface<Pixel> s = _surface();

	_fill(s, 0, 0, (int)W, menu_h(), C_PANEL);
	for (int m = 0; m < 4; m++) {
		int const x = m * 90 + 8;
		if (m == _menu_open) _fill(s, x - 4, 0, 84, menu_h(), C_HOVER);
		_text(s, x, 7, 13, C_TEXT, _menu_names[m]);
	}

	if (_menu_open >= 0) {
		_fill(s, _menu_open * 90, menu_h(), 170, 6 * 22 + 4, C_PANEL2);
		for (int i = 0; i < 6; i++) {
			char const *it = _menu_items[_menu_open][i];
			if (!*it) continue;
			_text(s, _menu_open * 90 + 10, menu_h() + 6 + i * 22, 12, C_TEXT, it);
		}
	}

	switch (_mode) {
	case Mode::Browse: _render_browse(s); break;
	case Mode::Edit: {
		_fill(s, main_x(), main_y(), main_w(), main_h(), C_BG);
		Editor::State st { (char *)_open_data, _open_size, 0, 0 };
		_editor.render(s, st, main_x() + 4, main_y() + 28, main_w() - 8, main_h() - 32, _font);
		_draw_back(s);
		break;
	}
	case Mode::Image: {
		_fill(s, main_x(), main_y(), main_w(), main_h(), C_BG);
		if (_open_data) {
			ImageViewer::Image img = _image.load(_open_data, _open_size);
			_image.render(s, img, main_x(), main_y() + 28, main_w(), main_h() - 32);
			if (img.px) stbi_image_free(img.px);
		}
		_draw_back(s);
		break;
	}
	case Mode::Hex: {
		_fill(s, main_x(), main_y(), main_w(), main_h(), C_BG);
		_hex.render(s, _open_data, _open_size, _hex_offset, main_x() + 4, main_y() + 28, main_w() - 8, main_h() - 32, _font);
		_draw_back(s);
		break;
	}
	case Mode::Convert:
		_fill(s, main_x(), main_y(), main_w(), main_h(), C_BG);
		_text(s, main_x() + 12, main_y() + 8, 12, C_ACCENT, "Convert");
		_draw_back(s);
		break;
	}

	_fill(s, 0, (int)H - ai_h(), (int)W, ai_h(), C_PANEL);
	_fill(s, 0, (int)H - ai_h(), (int)W, 2, C_ACCENT);
	_text(s, 12, (int)H - ai_h() + 8, 12, _ask_focus ? C_ACCENT : C_MUTED,
	      "Ask Shiro to do something with these files...");

	_text(s, 12, (int)H - ai_h() - 20, 11, C_MUTED, _status.string());

	_gui.framebuffer.refresh(Framebuffer::Rect(Point(0, 0), Area(W, H)));
}

} // namespace Hermos_files

void Component::construct(Genode::Env &env)
{
	env.exec_static_constructors();

	static Hermos_files::Main main(env);
}
