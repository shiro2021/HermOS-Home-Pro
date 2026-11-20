/*
 * HermOS disk installer core — freestanding, no Genode dependency.
 *
 * Installation = copy the boot partitions of the payload image byte for byte
 * (they were produced by Genode's own image tooling and are what has been
 * booted and tested), add one data partition formatted as an empty ext2
 * filesystem, then write the GUID partition table.
 *
 * Safety order: the old partition table is destroyed first, the GPT is
 * written last, and the whole result is read back and compared. A power loss
 * leaves a disk without a partition table, never a half-valid one.
 *
 * The code is driven in small steps (step()) so a UI can show progress, and
 * it only talks to the disk through the Io callbacks, so the same file is
 * tested on the host against e2fsck/sgdisk (tools/installer_test).
 */

#ifndef _HERMOS_INSTALLER_DISK_H_
#define _HERMOS_INSTALLER_DISK_H_

#include <stddef.h>
#include <stdint.h>

namespace Installer {

static constexpr uint32_t SECTOR = 512;

/* ----------------------------------------------------------------- helpers */

static inline void put16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
static inline void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
static inline void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }
static inline uint32_t get32(uint8_t const *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint64_t get64(uint8_t const *p) { return get32(p) | ((uint64_t)get32(p + 4) << 32); }

static inline void zero(void *p, size_t n) { uint8_t *b = (uint8_t *)p; while (n--) *b++ = 0; }
static inline void copy(void *d, void const *s, size_t n) { uint8_t *a = (uint8_t *)d; uint8_t const *b = (uint8_t const *)s; while (n--) *a++ = *b++; }
static inline bool same(void const *a, void const *b, size_t n)
{ uint8_t const *x = (uint8_t const *)a, *y = (uint8_t const *)b; while (n--) if (*x++ != *y++) return false; return true; }

static inline uint32_t crc32(uint8_t const *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < n; ++i) {
		c ^= p[i];
		for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
	}
	return ~c;
}

static inline uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

/* The disk, as seen by the installer. Offsets and lengths are in bytes and
 * multiples of SECTOR. */
struct Io
{
	void *ctx;
	bool (*write)(void *ctx, uint64_t off, void const *buf, size_t len);
	bool (*read) (void *ctx, uint64_t off, void *buf, size_t len);
};

/* ------------------------------------------------------------------ job */

enum class Phase { WIPE, BOOT, DATA, GPT, VERIFY, DONE, FAILED };

struct Params
{
	uint8_t const *payload;       /* system image (a GPT disk image) */
	size_t         payload_size;
	uint64_t       disk_sectors;  /* 512-byte sectors of the target */
	uint64_t       data_bytes;    /* wanted size of the data partition */
	uint64_t       seed;          /* entropy for GUIDs (time etc.) */
	uint32_t       now;           /* unix time for the filesystem stamps (0 = unknown) */
};

class Job
{
	public:

		static constexpr size_t CHUNK = 1u << 20;        /* 1 MiB per I/O */
		static constexpr uint32_t ALIGN = 2048;           /* 1 MiB in sectors */

	private:

		Io      &_io;
		Params   _p;
		Phase    _phase { Phase::WIPE };
		char const *_error { "" };

		/* payload layout */
		uint64_t _boot_sectors { 0 };                    /* sectors to copy: up to the end of its last partition */
		uint64_t _data_first   { 0 }, _data_last { 0 };  /* new data partition, LBA */

		/* progress cursor */
		uint64_t _cursor { 0 };
		uint8_t  _buf[CHUNK];

		uint64_t _rng { 0x9e3779b97f4a7c15ull };
		uint64_t _next()
		{
			_rng ^= _rng << 13; _rng ^= _rng >> 7; _rng ^= _rng << 17;
			return _rng;
		}
		void _guid(uint8_t *g)
		{
			for (int i = 0; i < 16; i += 8) put64(g + i, _next());
			g[7] = (g[7] & 0x0f) | 0x40;      /* version 4 */
			g[8] = (g[8] & 0x3f) | 0x80;      /* RFC 4122 variant */
		}

		/* generated metadata that must be identical when verifying */
		uint8_t _gpt[SECTOR * 33];        /* header sector + 32 sectors of entries (primary) */
		uint8_t _disk_guid[16];
		uint8_t _data_guid[16];
		uint8_t _fs_uuid[16];

		bool _fail(char const *why) { _error = why; _phase = Phase::FAILED; return false; }

		bool _w(uint64_t off, void const *b, size_t n) { return _io.write(_io.ctx, off, b, n) ? true : _fail("disk write failed"); }
		bool _r(uint64_t off, void *b, size_t n)       { return _io.read(_io.ctx, off, b, n)  ? true : _fail("disk read failed"); }

		bool _parse_payload();
		bool _write_gpt();
		bool _data_step();
		bool _build_ext2();       /* layout + the pieces that depend on it */

		/* ext2 layout (empty filesystem, 4 KiB blocks, 128-byte inodes) */
		struct Fs
		{
			uint64_t start_byte, bytes;
			uint32_t blocks, groups, bpg, ipg, itable_blocks, gdt_blocks;
			uint32_t free_blocks, free_inodes;
			uint32_t lpf_blocks;                  /* blocks of lost+found */
			uint32_t gd_free_blocks0;
		} _fs { };
		static constexpr uint32_t BS = 4096, INODE_SIZE = 128, LPF_BLOCKS = 4;
		static bool _has_backup(uint32_t g)
		{
			if (g <= 1) return true;
			static uint32_t const bases[3] = { 3, 5, 7 };
			for (int i = 0; i < 3; ++i) {
				uint64_t p = bases[i];
				while (p < g) p *= bases[i];
				if (p == g) return true;
			}
			return false;
		}
		uint32_t _group_overhead(uint32_t g) const
		{ return (_has_backup(g) ? 1 + _fs.gdt_blocks : 0) + 2 + _fs.itable_blocks; }
		uint32_t _group_first_block(uint32_t g) const { return g * _fs.bpg; }
		uint32_t _group_blocks(uint32_t g) const
		{ return (g + 1 < _fs.groups) ? _fs.bpg : _fs.blocks - g * _fs.bpg; }

		void _superblock(uint8_t *sb, uint32_t group_nr);
		void _group_desc(uint8_t *gd, uint32_t g);
		bool _write_group(uint32_t g);

	public:

		Job(Io &io, Params const &p) : _io(io), _p(p) { _rng ^= p.seed * 0xd6e8feb86659fd93ull | 1; }

		Phase       phase() const { return _phase; }
		char const *error() const { return _error; }

		/* 0..100 */
		unsigned percent() const;

		/* Prepare layout. Returns false (with error()) if the payload or the disk do not fit. */
		bool start();

		/* Do one bounded unit of work. Returns false once finished or failed. */
		bool step();

		uint64_t data_first_lba() const { return _data_first; }
		uint64_t data_last_lba()  const { return _data_last; }
		uint64_t boot_sectors()   const { return _boot_sectors; }
};

/* ------------------------------------------------------- payload + layout */

inline bool Job::_parse_payload()
{
	if (_p.payload_size < 34 * SECTOR + SECTOR) return _fail("payload too small");
	uint8_t const *hdr = _p.payload + SECTOR;
	static char const sig[8] = { 'E','F','I',' ','P','A','R','T' };
	if (!same(hdr, sig, 8)) return _fail("payload has no GPT");

	uint32_t const hsize = get32(hdr + 12);
	uint8_t tmp[92];
	if (hsize != 92) return _fail("unexpected GPT header size");
	copy(tmp, hdr, 92); put32(tmp + 16, 0);
	if (crc32(tmp, 92) != get32(hdr + 16)) return _fail("payload GPT header CRC mismatch");

	uint64_t const entries_lba = get64(hdr + 72);
	uint32_t const n = get32(hdr + 80), esz = get32(hdr + 84);
	if (esz != 128 || n == 0 || n > 128) return _fail("unexpected GPT entry layout");
	if ((entries_lba + (uint64_t)n * esz / SECTOR) * SECTOR > _p.payload_size) return _fail("payload GPT truncated");
	if (crc32(_p.payload + entries_lba * SECTOR, (size_t)n * esz) != get32(hdr + 88)) return _fail("payload GPT entries CRC mismatch");

	uint64_t last = 0; bool efi = false;
	for (uint32_t i = 0; i < n; ++i) {
		uint8_t const *e = _p.payload + entries_lba * SECTOR + i * esz;
		bool used = false; for (int k = 0; k < 16; ++k) if (e[k]) used = true;
		if (!used) continue;
		uint64_t const lo = get64(e + 32), hi = get64(e + 40);
		if (hi < lo) return _fail("bad partition in payload");
		if (hi > last) last = hi;
		static uint8_t const esp[16] = { 0x28,0x73,0x2a,0xc1, 0x1f,0xf8, 0xd2,0x11, 0xba,0x4b, 0x00,0xa0, 0xc9,0x3e, 0xc9,0x3b };
		if (same(e, esp, 16)) efi = true;
	}
	if (!efi) return _fail("payload has no EFI system partition");
	if ((last + 1) * SECTOR > _p.payload_size) return _fail("payload is shorter than its partitions");
	_boot_sectors = last + 1;
	return true;
}

inline bool Job::start()
{
	if (!_parse_payload()) return false;

	/* data partition: 1 MiB aligned after the boot partitions, backup GPT at the end */
	_data_first = align_up(_boot_sectors, ALIGN);
	uint64_t const last_usable = _p.disk_sectors - 34;
	uint64_t want = _p.data_bytes / SECTOR;
	if (_data_first + want - 1 > last_usable)
		want = (last_usable + 1 > _data_first) ? (last_usable + 1 - _data_first) : 0;
	want = want / ALIGN * ALIGN;
	if (want * SECTOR < 64ull * 1024 * 1024) return _fail("disk too small");
	_data_last = _data_first + want - 1;

	_guid(_disk_guid); _guid(_data_guid); _guid(_fs_uuid);
	if (!_build_ext2()) return false;

	_phase = Phase::WIPE; _cursor = 0;
	return true;
}

inline unsigned Job::percent() const
{
	switch (_phase) {
	case Phase::WIPE:   return 1;
	case Phase::BOOT:   return 2 + (unsigned)(_cursor * 38 / (_boot_sectors ? _boot_sectors : 1));
	case Phase::DATA:   return 40 + (unsigned)(_cursor * 40 / (_fs.groups ? _fs.groups : 1));
	case Phase::GPT:    return 82;
	case Phase::VERIFY: return 84 + (unsigned)(_cursor * 15 / (_boot_sectors + _fs.groups + 1));
	case Phase::DONE:   return 100;
	default:            return 0;
	}
}

/* ------------------------------------------------------------------- GPT */

inline bool Job::_write_gpt()
{
	/* entries: the payload's, with fresh unique GUIDs, plus the data partition */
	uint8_t const *src_hdr = _p.payload + SECTOR;
	uint64_t const src_entries = get64(src_hdr + 72);
	uint32_t const n = get32(src_hdr + 80);

	uint8_t *entries = _gpt + SECTOR;
	zero(_gpt, sizeof _gpt);
	int free_slot = -1;
	for (uint32_t i = 0; i < 128; ++i) {
		uint8_t *e = entries + i * 128;
		if (i < n) copy(e, _p.payload + src_entries * SECTOR + i * 128, 128);
		bool used = false; for (int k = 0; k < 16; ++k) if (e[k]) used = true;
		if (used) _guid(e + 16);
		else if (free_slot < 0) free_slot = (int)i;
	}
	if (free_slot < 0) return _fail("no free GPT entry");
	uint8_t *d = entries + free_slot * 128;
	static uint8_t const linux_fs[16] = { 0xaf,0x3d,0xc6,0x0f, 0x83,0x84, 0x72,0x47, 0x8e,0x79, 0x3d,0x69, 0xd8,0x47, 0x7d,0xe4 };
	copy(d, linux_fs, 16); copy(d + 16, _data_guid, 16);
	put64(d + 32, _data_first); put64(d + 40, _data_last);
	static char const name[] = "HERMOS-DATA";
	for (int i = 0; name[i]; ++i) d[56 + 2 * i] = (uint8_t)name[i];

	uint32_t const ecrc = crc32(entries, 128 * 128);
	uint64_t const total = _p.disk_sectors;

	auto header = [&] (uint8_t *h, uint64_t cur, uint64_t other, uint64_t entries_lba)
	{
		zero(h, SECTOR);
		static char const sig[8] = { 'E','F','I',' ','P','A','R','T' };
		copy(h, sig, 8);
		put32(h + 8, 0x00010000); put32(h + 12, 92);
		put64(h + 24, cur); put64(h + 32, other);
		put64(h + 40, 34); put64(h + 48, total - 34);
		copy(h + 56, _disk_guid, 16);
		put64(h + 72, entries_lba); put32(h + 80, 128); put32(h + 84, 128);
		put32(h + 88, ecrc);
		put32(h + 16, crc32(h, 92));
	};

	/* protective MBR: the payload's sector 0 (its boot code) with the size fixed up */
	uint8_t mbr[SECTOR];
	copy(mbr, _p.payload, SECTOR);
	put32(mbr + 454, 1);
	put32(mbr + 458, (uint32_t)((total - 1) > 0xffffffffull ? 0xffffffffull : total - 1));

	uint8_t primary[SECTOR];
	header(primary, 1, total - 1, 2);
	uint8_t backup[SECTOR];
	header(backup, total - 1, 1, total - 33);

	copy(_gpt, primary, SECTOR);        /* remember the primary header for verification */

	return _w(0, mbr, SECTOR)
	    && _w(1 * SECTOR, primary, SECTOR)
	    && _w(2 * SECTOR, entries, 128 * 128)
	    && _w((total - 33) * SECTOR, entries, 128 * 128)
	    && _w((total - 1) * SECTOR, backup, SECTOR);
}

/* ------------------------------------------------------------------ ext2 */

inline bool Job::_build_ext2()
{
	Fs &f = _fs;
	f.start_byte = _data_first * SECTOR;
	f.bytes      = (_data_last - _data_first + 1) * SECTOR;
	uint64_t const blocks = f.bytes / BS;
	if (blocks > 0xfffffff0ull) return _fail("data partition too large");
	f.blocks = (uint32_t)blocks;
	f.bpg    = BS * 8;                                   /* one bitmap block per group */
	f.groups = (f.blocks + f.bpg - 1) / f.bpg;
	f.ipg    = f.bpg / 16;                               /* one inode per 64 KiB */
	f.itable_blocks = f.ipg * INODE_SIZE / BS;
	f.gdt_blocks = (f.groups * 32 + BS - 1) / BS;

	/* a last group too small for its own metadata is dropped */
	uint32_t const last_blocks = f.blocks - (f.groups - 1) * f.bpg;
	if (f.groups > 1 && last_blocks < _group_overhead(f.groups - 1) + 50) {
		f.blocks -= last_blocks; --f.groups;
		f.gdt_blocks = (f.groups * 32 + BS - 1) / BS;
	}
	if (f.groups == 0) return _fail("data partition too small");

	f.lpf_blocks = LPF_BLOCKS;
	uint32_t overhead_total = 0;
	for (uint32_t g = 0; g < f.groups; ++g) overhead_total += _group_overhead(g);
	uint32_t const used = overhead_total + 1 /* root */ + f.lpf_blocks;
	if (used >= f.blocks) return _fail("data partition too small");
	f.free_blocks = f.blocks - used;
	f.free_inodes = f.groups * f.ipg - 11;               /* inodes 1..10 reserved, 11 = lost+found */
	return true;
}

inline void Job::_superblock(uint8_t *sb, uint32_t group_nr)
{
	Fs const &f = _fs;
	zero(sb, 1024);
	put32(sb + 0,  f.groups * f.ipg);                    /* inodes count */
	put32(sb + 4,  f.blocks);
	put32(sb + 8,  0);                                   /* reserved blocks */
	put32(sb + 12, f.free_blocks);
	put32(sb + 16, f.free_inodes);
	put32(sb + 20, 0);                                   /* first data block (4 KiB blocks) */
	put32(sb + 24, 2);                                   /* log block size: 1024 << 2 */
	put32(sb + 28, 2);                                   /* log cluster size */
	put32(sb + 32, f.bpg);
	put32(sb + 36, f.bpg);                               /* clusters per group */
	put32(sb + 40, f.ipg);
	put32(sb + 44, 0);                                   /* mount time */
	put32(sb + 48, _p.now);                              /* write time */
	put16(sb + 52, 0);                                   /* mount count */
	put16(sb + 54, 0xffff);                              /* max mount count: never */
	put16(sb + 56, 0xef53);
	put16(sb + 58, 1);                                   /* state: clean */
	put16(sb + 60, 1);                                   /* errors: continue */
	put16(sb + 62, 0);
	put32(sb + 64, _p.now);                              /* last check */
	put32(sb + 68, 0);                                   /* check interval */
	put32(sb + 72, 0);                                   /* creator OS: Linux */
	put32(sb + 76, 1);                                   /* revision 1 */
	put16(sb + 80, 0); put16(sb + 82, 0);                /* reserved uid/gid */
	put32(sb + 84, 11);                                  /* first inode */
	put16(sb + 88, INODE_SIZE);
	put16(sb + 90, (uint16_t)group_nr);
	put32(sb + 92, 0);                                   /* compat features */
	put32(sb + 96, 0x2);                                 /* incompat: filetype */
	put32(sb + 100, 0x1);                                /* ro_compat: sparse_super */
	copy(sb + 104, _fs_uuid, 16);
	static char const label[] = "HERMOS-DATA";
	for (int i = 0; label[i]; ++i) sb[120 + i] = (uint8_t)label[i];
	put32(sb + 0xfc, 1);                                 /* default hash version: half-md4 (unused) */
}

inline void Job::_group_desc(uint8_t *gd, uint32_t g)
{
	Fs const &f = _fs;
	uint32_t const first = _group_first_block(g);
	uint32_t const base  = first + (_has_backup(g) ? 1 + f.gdt_blocks : 0);
	uint32_t free_b = _group_blocks(g) - _group_overhead(g);
	uint32_t free_i = f.ipg;
	uint32_t dirs = 0;
	if (g == 0) { free_b -= 1 + f.lpf_blocks; free_i -= 11; dirs = 2; }
	zero(gd, 32);
	put32(gd + 0, base);                                 /* block bitmap */
	put32(gd + 4, base + 1);                             /* inode bitmap */
	put32(gd + 8, base + 2);                             /* inode table */
	put16(gd + 12, (uint16_t)free_b);
	put16(gd + 14, (uint16_t)free_i);
	put16(gd + 16, (uint16_t)dirs);
}

/* write the metadata of one group (and, for group 0, the root and lost+found) */
inline bool Job::_write_group(uint32_t g)
{
	Fs const &f = _fs;
	uint64_t const gbase = f.start_byte + (uint64_t)_group_first_block(g) * BS;
	uint32_t const meta  = _group_overhead(g);
	uint32_t const mb    = _has_backup(g) ? 1 + f.gdt_blocks : 0;

	/* backup superblock + group descriptor table */
	if (mb) {
		static uint8_t blk[BS * 1];   /* superblock block */
		if (g == 0) {
			/* block 0: 1 KiB boot area + the superblock at byte 1024 */
			zero(blk, BS);
			_superblock(blk + 1024, 0);
		} else {
			zero(blk, BS);
			_superblock(blk, g);
		}
		if (!_w(gbase, blk, BS)) return false;

		/* group descriptor table (all groups), identical in every copy */
		for (uint32_t b = 0; b < f.gdt_blocks; ++b) {
			static uint8_t gdt[BS];
			zero(gdt, BS);
			for (uint32_t i = 0; i < BS / 32; ++i) {
				uint32_t const gi = b * (BS / 32) + i;
				if (gi < f.groups) _group_desc(gdt + i * 32, gi);
			}
			if (!_w(gbase + (uint64_t)(1 + b) * BS, gdt, BS)) return false;
		}
	}

	/* block bitmap */
	static uint8_t bm[BS];
	zero(bm, BS);
	uint32_t used = meta + (g == 0 ? 1 + f.lpf_blocks : 0);
	for (uint32_t i = 0; i < used; ++i) bm[i / 8] |= uint8_t(1u << (i % 8));
	for (uint32_t i = _group_blocks(g); i < f.bpg; ++i) bm[i / 8] |= uint8_t(1u << (i % 8));  /* padding */
	if (!_w(gbase + (uint64_t)mb * BS, bm, BS)) return false;

	/* inode bitmap: inodes 1..11 used in group 0; the tail up to the block end is padding */
	zero(bm, BS);
	if (g == 0) for (uint32_t i = 0; i < 11; ++i) bm[i / 8] |= uint8_t(1u << (i % 8));
	for (uint32_t i = f.ipg; i < BS * 8; ++i) bm[i / 8] |= uint8_t(1u << (i % 8));
	if (!_w(gbase + (uint64_t)(mb + 1) * BS, bm, BS)) return false;

	/* inode table */
	static uint8_t it[BS];
	for (uint32_t b = 0; b < f.itable_blocks; ++b) {
		zero(it, BS);
		if (g == 0 && b == 0) {
			/* inode 2 (root) and inode 11 (lost+found) live in the first itable block:
			 * 128-byte inodes -> inode n is at byte (n-1)*128 */
			uint32_t const data0 = meta;                       /* first data block of group 0 */
			auto dir_inode = [&] (uint8_t *ino, uint32_t first_blk, uint32_t nblk, uint32_t links)
			{
				put16(ino + 0, 0x41ed);                         /* drwxr-xr-x */
				put32(ino + 4, nblk * BS);
				put32(ino + 8, _p.now); put32(ino + 12, _p.now); put32(ino + 16, _p.now);
				put16(ino + 26, (uint16_t)links);
				put32(ino + 28, nblk * (BS / 512));
				for (uint32_t k = 0; k < nblk; ++k) put32(ino + 40 + 4 * k, first_blk + k);
			};
			dir_inode(it + 1 * 128, data0, 1, 3);               /* root: ".", "..", lost+found's ".." */
			dir_inode(it + 10 * 128, data0 + 1, f.lpf_blocks, 2);
		}
		if (!_w(gbase + (uint64_t)(mb + 2 + b) * BS, it, BS)) return false;
	}

	/* root directory block and lost+found (group 0 only) */
	if (g == 0) {
		uint32_t const data0 = meta;
		static uint8_t d[BS];
		zero(d, BS);
		put32(d + 0, 2);  put16(d + 4, 12); d[6] = 1; d[7] = 2; d[8] = '.';
		put32(d + 12, 2); put16(d + 16, 12); d[18] = 2; d[19] = 2; d[20] = '.'; d[21] = '.';
		put32(d + 24, 11); put16(d + 28, BS - 24); d[30] = 10; d[31] = 2;
		static char const lf[] = "lost+found";
		for (int i = 0; lf[i]; ++i) d[32 + i] = (uint8_t)lf[i];
		if (!_w(gbase + (uint64_t)data0 * BS, d, BS)) return false;

		for (uint32_t k = 0; k < f.lpf_blocks; ++k) {
			zero(d, BS);
			if (k == 0) {
				put32(d + 0, 11); put16(d + 4, 12); d[6] = 1; d[7] = 2; d[8] = '.';
				put32(d + 12, 2); put16(d + 16, BS - 12); d[18] = 2; d[19] = 2; d[20] = '.'; d[21] = '.';
			} else {
				put32(d + 0, 0); put16(d + 4, BS);
			}
			if (!_w(gbase + (uint64_t)(data0 + 1 + k) * BS, d, BS)) return false;
		}
	}
	return true;
}

/* ------------------------------------------------------------ the stepper */

inline bool Job::step()
{
	switch (_phase) {

	case Phase::WIPE: {
		/* destroy the old partition tables before anything else is touched */
		static uint8_t z[SECTOR * 34];
		zero(z, sizeof z);
		if (!_w(0, z, sizeof z)) return false;
		if (!_w((_p.disk_sectors - 33) * SECTOR, z, SECTOR * 33)) return false;
		_phase = Phase::BOOT; _cursor = 0;
		return true;
	}

	case Phase::BOOT: {
		/* the payload's boot partitions, byte for byte (sectors 34.. : the GPT area is rewritten later) */
		uint64_t const start = _cursor ? _cursor : 34;
		uint64_t const todo  = _boot_sectors - start;
		size_t   const n     = (size_t)((todo * SECTOR > CHUNK) ? CHUNK : todo * SECTOR);
		if (!_w(start * SECTOR, _p.payload + start * SECTOR, n)) return false;
		_cursor = start + n / SECTOR;
		if (_cursor >= _boot_sectors) { _phase = Phase::DATA; _cursor = 0; }
		return true;
	}

	case Phase::DATA:
		if (!_write_group((uint32_t)_cursor)) return false;
		if (++_cursor >= _fs.groups) { _phase = Phase::GPT; _cursor = 0; }
		return true;

	case Phase::GPT:
		if (!_write_gpt()) return false;
		_phase = Phase::VERIFY; _cursor = 0;
		return true;

	case Phase::VERIFY: {
		/* 1. the boot area equals the payload; 2. the GPT reads back with valid CRCs;
		 * 3. the ext2 superblock and group 0 metadata are what was written. */
		uint64_t const boot_chunks = (_boot_sectors * SECTOR + CHUNK - 1) / CHUNK;
		if (_cursor < boot_chunks) {
			uint64_t const off = _cursor * CHUNK;
			uint64_t const lo  = off < 34 * SECTOR ? 34 * SECTOR : off;      /* GPT area differs by design */
			uint64_t const hi  = (off + CHUNK < _boot_sectors * SECTOR) ? off + CHUNK : _boot_sectors * SECTOR;
			if (hi > lo) {
				if (!_r(lo, _buf, (size_t)(hi - lo))) return false;
				if (!same(_buf, _p.payload + lo, (size_t)(hi - lo))) return _fail("verify: boot area differs");
			}
			++_cursor; return true;
		}
		if (_cursor == boot_chunks) {
			uint8_t h[SECTOR], h2[SECTOR], e[128 * 128];
			if (!_r(SECTOR, h, SECTOR) || !_r(2 * SECTOR, e, sizeof e)) return false;
			if (!same(h, _gpt, SECTOR)) return _fail("verify: GPT header differs");
			if (crc32(e, sizeof e) != get32(h + 88)) return _fail("verify: GPT entries CRC");
			if (!same(e, _gpt + SECTOR, sizeof e)) return _fail("verify: GPT entries differ");
			if (!_r((_p.disk_sectors - 1) * SECTOR, h2, SECTOR)) return false;
			if (get64(h2 + 24) != _p.disk_sectors - 1) return _fail("verify: backup GPT");
			++_cursor; return true;
		}
		{
			static uint8_t sb[BS], want[BS];
			if (!_r(_fs.start_byte, sb, BS)) return false;
			zero(want, BS); _superblock(want + 1024, 0);
			if (!same(sb, want, BS)) return _fail("verify: filesystem superblock differs");
			_phase = Phase::DONE;
			return false;
		}
	}

	default: return false;
	}
}

} /* namespace Installer */

#endif /* _HERMOS_INSTALLER_DISK_H_ */
