/*
 * Host test of the installer core (src/app/desktop_builder/installer_disk.h).
 *
 *   g++ -O2 -std=c++17 -I src/app/desktop_builder tools/installer_test/installer_test.cc -o installer_test
 *   truncate -s 8G disk.img
 *   ./installer_test system.img disk.img <data-partition-GiB>
 *   sgdisk -v disk.img ; e2fsck -fn <data partition>
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include "installer_disk.h"

static bool wr(void *c, uint64_t off, void const *b, size_t n) { return pwrite(*(int *)c, b, n, (off_t)off) == (ssize_t)n; }
static bool rd(void *c, uint64_t off, void *b, size_t n)       { return pread (*(int *)c, b, n, (off_t)off) == (ssize_t)n; }

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: %s payload disk data-GiB\n", argv[0]); return 2; }
	FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
	fseek(f, 0, SEEK_END); size_t psz = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> payload(psz); if (fread(payload.data(), 1, psz, f) != psz) return 1; fclose(f);
	int fd = open(argv[2], O_RDWR); if (fd < 0) { perror(argv[2]); return 1; }
	struct stat st; fstat(fd, &st);

	Installer::Io io { &fd, wr, rd };
	if (argc > 4 && !strcmp(argv[4], "find")) {       /* ./installer_test x disk 0 find */
		Installer::PartInfo pi;
		bool const gpt = Installer::find_partition(io, "HERMOS-DATA", pi);
		printf("gpt=%d found=%d number=%u first=%llu last=%llu (%llu MiB)\n", gpt, pi.found, pi.number,
		       (unsigned long long)pi.first, (unsigned long long)pi.last, (unsigned long long)(pi.bytes() >> 20));
		return pi.found ? 0 : 1;
	}
	Installer::Params p { payload.data(), psz, (uint64_t)st.st_size / 512, (uint64_t)(atof(argv[3]) * 1024.0 * 1024 * 1024),
	                      (uint64_t)time(nullptr) ^ ((uint64_t)getpid() << 20), (uint32_t)time(nullptr) };
	static Installer::Job job(io, p);
	if (!job.start()) { fprintf(stderr, "start failed: %s\n", job.error()); return 1; }
	printf("boot sectors %llu, data LBA %llu..%llu\n", (unsigned long long)job.boot_sectors(),
	       (unsigned long long)job.data_first_lba(), (unsigned long long)job.data_last_lba());
	unsigned last = 101;
	while (job.step()) { if (job.percent() != last) { last = job.percent(); if (last % 10 == 0) printf("%u%%\n", last); } }
	if (job.phase() != Installer::Phase::DONE) { fprintf(stderr, "FAILED: %s\n", job.error()); return 1; }
	printf("DONE\n");
	return 0;
}
