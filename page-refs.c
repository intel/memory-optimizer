/*
 * TBD
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>

#include "lib/memparse.h"
#include "lib/iomem_parse.h"

#define IDLEMAP_CHUNK_SIZE	8
#define IDLEMAP_BUF_SIZE	(1<<20)

// big enough to span 640 Gbytes:
#define MAX_IDLEMAP_SIZE	(20 * 1024 * 1024)	//20M * 8 * 4K = 640G

#define PFN_IDLE_BITMAP_PATH	"/sys/kernel/mm/page_idle/bitmap"
#define KERNEL_PAGE_FLAGS	"/proc/kpageflags"
#define GNUPLOT_PATH		"/usr/bin/gnuplot"
#define PLOT_SCRIPT		"plot-page-refs"

#define PAGE_FLAGS_HUGE      (1 << 17)
#define PAGE_FLAGS_THP       (1 << 22)

// globals
int g_debug;			// 1 == some, 2 == verbose
unsigned long long *g_idlebuf;
unsigned long long g_idlebuf_size;
unsigned long long g_idlebuf_len;
unsigned short *g_refs_count;
unsigned short *g_refs_2m_count;
unsigned long long g_offset, g_size;
unsigned long long g_start_pfn, g_num_pfn, g_sim_2m_num_pfn;
unsigned char g_setidle_buf[IDLEMAP_BUF_SIZE];
int g_setidle_bufsize;
int g_idlefd;
int g_kpageflags_fd;
unsigned long long *g_kpageflags_buf;
unsigned long long g_kpageflags_buf_size;
unsigned long long g_kpageflags_buf_len;

int verbose_printf(int level, const char *format, ...)
{
	if (g_debug < level)
		return 0;

	va_list args;
	va_start(args, format);
	int ret = vprintf(format, args);
	va_end(args);

	return ret;
}

#define debug_printf(fmt, args...)	verbose_printf(1, fmt, ##args)
#define printdd(fmt, args...)		verbose_printf(2, fmt, ##args)

// on return:
// 	for nrefs in [0, max]:
// 		count_array[nrefs] = npages;
int count_refs(unsigned int max,
			unsigned long count_4k_array[],
			unsigned long count_2m_array[])
{
	unsigned long long pfn;
	unsigned short nrefs;

	memset(count_4k_array, 0, (max + 1) * sizeof(count_4k_array[0]));
	memset(count_2m_array, 0, (max + 1) * sizeof(count_2m_array[0]));

	for (pfn = 0; pfn < g_num_pfn; pfn++) {
		nrefs = g_refs_count[pfn];
		if (nrefs <= max) {
			if (!(g_kpageflags_buf[pfn] & PAGE_FLAGS_THP))
				count_4k_array[nrefs]++;
			else {
				count_2m_array[nrefs]++;
				printdd("pfn 0x%llx is a huge page.\n",
					pfn);
			}
		} else
			return 1;
	}

	return 0;
}

int count_sim_2m_refs(unsigned int max,
				unsigned long count_sim_2m_array[])
{
	unsigned long long pfn;
	unsigned short nrefs;

	memset(count_sim_2m_array, 0,
			(max + 1) * sizeof(count_sim_2m_array[0]));

	for (pfn = 0; pfn < g_sim_2m_num_pfn; pfn++) {
		nrefs = g_refs_2m_count[pfn];
		if (nrefs <= max)
			count_sim_2m_array[nrefs]++;
		else
			return 1;
	}

	return 0;
}

int output_refs_count(unsigned int loop, const char *output_file)
{
	unsigned short nrefs;
	FILE *file;
	unsigned long refs_4k_count[loop + 1];
	unsigned long refs_2m_count[loop + 1];
	unsigned long refs_sim_2m_count[loop + 1];

	if (count_refs(loop, refs_4k_count, refs_2m_count)) {
		fprintf(stderr, "refs count out of range\n");
		return -1;
	}

	if (count_sim_2m_refs(loop, refs_sim_2m_count)) {
		fprintf(stderr, "refs count out of range\n");
		return -1;
	}

	if ((file = fopen(output_file, "w")) == NULL) {
		perror("Can't open to write the output file");
		exit(2);
	}

	fprintf(file, "%-8s %-15s %-15s %-15s\n",
				"refs", "count_4K",
				"count_2M", "count_simulate_2M");
	fprintf(file, "=========================================================\n");

	for (nrefs = 0; nrefs <= loop; nrefs++) {
		fprintf(file, "%-8u %-15lu %-15lu %-15lu\n",
				(unsigned int)nrefs,
				refs_4k_count[nrefs],
				refs_2m_count[nrefs],
				refs_sim_2m_count[nrefs]);
	}
	fclose(file);
	return 0;
}

int output_pfn_refs(const char *output_file)
{
	FILE *file;
	unsigned long long pfn;

	if (!g_refs_count)
		return -1;

	if ((file = fopen(output_file, "w")) == NULL) {
		perror("Can't open to write the output file");
		exit(2);
	}

	fprintf(file, "%-18s  refcount\n", "PFN");
	for (pfn = 0; pfn < g_num_pfn; pfn++) {
		fprintf(file, "0x%016llx  %d\n",
			g_start_pfn + pfn,
			g_refs_count[pfn]);
	}
	fclose(file);
	return 0;
}

int account_refs(void)
{
	unsigned long len = 0;
	unsigned long long idlebits, idlemap = 0;
	unsigned long long base_pfn_2m = 0, base_pfn = 0;
	unsigned long long pfn_2m = 0, pfn, fn;
	int count_2m = 1;

	while (len < g_idlebuf_len) {
		idlebits = g_idlebuf[idlemap];
		printdd("idlebits: 0x%llx\n", idlebits);

		for (pfn = 0; pfn < 64; pfn++) {
			if (!(idlebits & (1ULL << pfn))) {
				fn = base_pfn + pfn;
				g_refs_count[fn]++;

				//only count once in a 2M page
				if (count_2m) {
					pfn_2m = fn / 512;
					g_refs_2m_count[pfn_2m]++;
					count_2m = 0;
				}
				printdd("pfn 0x%llx refs_count 0x%llx\n",
					     base_pfn + pfn,
					     g_refs_count[base_pfn + pfn]);
			}
		}

		len += IDLEMAP_CHUNK_SIZE;
		idlemap = len / IDLEMAP_CHUNK_SIZE;
		base_pfn = idlemap * 64;
		base_pfn_2m = base_pfn / 512;
		if (base_pfn_2m > pfn_2m)
			count_2m = 1;
	}

	return 0;
}

int setidlemap(unsigned long long offset, unsigned long long size)
{
	ssize_t len = 0;

	if (lseek(g_idlefd, offset, SEEK_SET) < 0) {
		perror("Can't seek bitmap file");
		exit(3);
	}

	while (size) {
		len = write(g_idlefd, g_setidle_buf, g_setidle_bufsize);

		if (len < 0) {
			// reach max PFN
			if (errno == ENXIO)
				return 0;

			perror("setidlemap: error writing idle bitmap");
			return len;
		}
		size -= len;
	}

	return 0;
}

int loadidlemap(unsigned long long offset, unsigned long long size)
{
	char *p;
	ssize_t len = 0;
	int err = 0;

	if (lseek(g_idlefd, offset, SEEK_SET) < 0) {
		printf("Can't seek bitmap file");
		err = -4;
		goto _loadidlemap_out;
	}

	p = (char *)g_idlebuf;
	g_idlebuf_len = 0;

	// unfortunately, larger reads do not seem supported
	while ((len = read(g_idlefd, p, g_idlebuf_size - g_idlebuf_len)) > 0) {
		p += len;
		g_idlebuf_len += len;
		size -= len;
		if (size <= 0)
			break;
	}
	debug_printf("g_idlebuf_len: 0x%llx\n", g_idlebuf_len);

_loadidlemap_out:
	return err;
}

int loadflags(unsigned long long pfn, unsigned long long num_pfn)
{
	char *p;
	ssize_t len = 0;
	int err = 0;

	if (lseek(g_kpageflags_fd,
		  pfn * sizeof(unsigned long long), SEEK_SET) < 0) {
		printf("Can't seek kernel page flags file");
		err = -4;
		goto _loadflags_out;
	}

	p = (char *)g_kpageflags_buf;
	g_kpageflags_buf_len = 0;

	// unfortunately, larger reads do not seem supported
	while ((len = read(g_kpageflags_fd, p,
			   g_kpageflags_buf_size - g_kpageflags_buf_len)) > 0) {
		p += len;
		g_kpageflags_buf_len += len;
		num_pfn--;
		if (num_pfn <= 0)
			break;
	}
	debug_printf("g_kpageflags_buf_len: 0x%llx\n", g_kpageflags_buf_len);

_loadflags_out:
	return err;
}

static const struct option opts[] = {
	{"offset",	required_argument,	NULL,	'o'},
	{"size",	required_argument,	NULL,	's'},
	{"interval",	required_argument,	NULL,	'i'},
	{"loop",	required_argument,	NULL,	'l'},
	{"bitmap",	required_argument,	NULL,	'b'},
	{"output",	required_argument,	NULL,	'f'},
	{"verbose",	required_argument,	NULL,	'v'},
	{"help",	no_argument,		NULL,	'h'},
	{NULL,		0,			NULL,	0}
};

static void usage(char *prog)
{
	fprintf(stderr,
		"%s [option] ...\n"
		"    -h|--help		Show this information.\n"
		"    -o|--offset	The start offset of the bitmap to be scanned.\n"
		"    -s|--size		The size should be scanned.\n"
		"    -i|--interval	The interval to scan bitmap.\n"
		"    -l|--loop		The number of times to scan bitmap.\n"
		"    -b|--bitmap	The bitmap file for scanning.\n"
		"    -f|--output	The output file for the result of scanning.\n"
		"    -v|--verbose	Show debug info.\n",
		prog);
}

static inline unsigned long long tv_delta(struct timeval t1, struct timeval t2)
{
	return  (t2.tv_sec  - t1.tv_sec) * 1000000 +
		(t2.tv_usec - t1.tv_usec);
}

static int plot_output(char *argv[], char *output_file)
{
	char mypath[PATH_MAX];
	struct stat sb;

	if (stat(GNUPLOT_PATH, &sb))
		return -1;

	if (!(sb.st_mode & S_IXUSR))
		return -2;

	if (!realpath(argv[0], mypath))
		return -3;

	return system(strcat(dirname(mypath), "/" PLOT_SCRIPT));
}


int main(int argc, char *argv[])
{
	int i, ret = 0;
	double interval = 0.1;
	unsigned short loop = 1;
	int pagesize, opt = 0, options_index = 0;
	char bitmap_file[PATH_MAX] = PFN_IDLE_BITMAP_PATH;
	char output_file[PATH_MAX] = "refs_count";
	const char *optstr = "hvo:s:i:l:b:f:";
	unsigned long long offset = 0, size = 0;
	unsigned int bufsize;
	unsigned long long set_us, read_us, dur_us, slp_us, account_us;
	static struct timeval ts1, ts2, ts3, ts4, ts5;
	int is_pfn_bitmap;

	pagesize = getpagesize();

	/* parse the options */
	while ((opt = getopt_long(argc, argv, optstr, opts, &options_index)) != EOF) {
		switch (opt) {
		case 0:
			break;
		case 'o':
			offset = memparse(optarg, NULL);
			debug_printf("offset = 0x%llx\n", offset);
			offset /= pagesize * 8;
			debug_printf("offset of the bitmap = 0x%llx, pagesize = 0x%x\n", offset, pagesize);
			break;
		case 's':
			size = memparse(optarg, NULL);
			debug_printf("size = 0x%llx\n", size);
			size += pagesize * 8 - 1;
			size /= pagesize * 8;
			debug_printf("size of the bitmap = 0x%llx, pagesize = 0x%x\n", size, pagesize);
			break;
		case 'i':
			interval = atof(optarg);
			debug_printf("interval = %.2f\n", interval);
			break;
		case 'l':
			loop = atoi(optarg);
			debug_printf("loop number = %d\n", loop);
			break;
		case 'b':
			strcpy(bitmap_file, optarg);
			debug_printf("bitmap_file = %s\n", bitmap_file);
			break;
		case 'f':
			strcpy(output_file, optarg);
			debug_printf("output_file = %s\n", output_file);
			break;
		case 'v':
			g_debug++;
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0]);
			exit(0);
		}
	}

	printf("Watching the %s to calculate page reference count every %.2f seconds for %d times.\n"
	       "The result will be saved in %s.\n",
	       bitmap_file,
	       interval,
	       loop,
	       output_file);

	if (!size)
		size = MAX_IDLEMAP_SIZE;

	// align on IDLEMAP_CHUNK_SIZE
	bufsize = size;
	bufsize += IDLEMAP_CHUNK_SIZE - 1;
	bufsize = (bufsize / IDLEMAP_CHUNK_SIZE) * IDLEMAP_CHUNK_SIZE;

	is_pfn_bitmap = !strcmp(bitmap_file, PFN_IDLE_BITMAP_PATH);

	g_setidle_bufsize = bufsize < sizeof(g_setidle_buf) ?
			    bufsize : sizeof(g_setidle_buf);
	memset(g_setidle_buf, 0xff, g_setidle_bufsize);

	if ((g_idlefd = open(bitmap_file, O_RDWR)) < 0) {
		perror("Can't write bitmap file");
		exit(2);
	}

	if ((g_idlebuf = malloc(bufsize)) == NULL) {
		printf("Can't allocate memory for idlemap buf (%d bytes)!\n",
		       bufsize);
		ret = -1;
		goto out;
	}
	debug_printf("size: 0x%llx, bufsize: 0x%llx\n", size, bufsize);
	g_idlebuf_size = bufsize;

	g_start_pfn = offset * 8;
	g_num_pfn = size * 8;
	debug_printf("The start pfn is 0x%llx, the number of pfn is 0x%llx.\n",
		     g_start_pfn, g_num_pfn);

	g_refs_count = calloc(g_num_pfn, sizeof(unsigned short));
	if (g_refs_count == NULL) {
		printf("Can't allocate memory for refs count buf (%llu bytes)!\n",
		       sizeof(unsigned short) * g_num_pfn);
		ret = -2;
		goto out;
	}

	g_sim_2m_num_pfn = (g_num_pfn + 511) / 512;
	debug_printf("g_sim_2m_num_pfn: 0x%llx\n", g_sim_2m_num_pfn);
	g_refs_2m_count =
			calloc(g_sim_2m_num_pfn, sizeof(unsigned short));
	if (g_refs_2m_count == NULL) {
		printf("Can't allocate memory for 2M refs count buf (%llu bytes)!\n",
		       sizeof(unsigned short) * g_sim_2m_num_pfn);
		ret = -4;
		goto out;
	}

	g_kpageflags_fd = open(KERNEL_PAGE_FLAGS, O_RDONLY);
	if (g_kpageflags_fd < 0) {
		printf("Can't read file %s!\n", KERNEL_PAGE_FLAGS);
		ret = -3;
		goto out;
	}

	g_kpageflags_buf = calloc(g_num_pfn, sizeof(unsigned long long));
	if (!g_kpageflags_buf) {
		printf("Can't allocate memory for idlemap buf (%d bytes)!\n",
		       bufsize);
		ret = -1;
		goto out;
	}
	g_kpageflags_buf_size = g_num_pfn * sizeof(unsigned long long);

	ret = loadflags(g_start_pfn, g_num_pfn);
	if (ret)
		goto out;

	for (i = 0; i < loop; i++) {
		// set idle flags
		gettimeofday(&ts1, NULL);
		// the per-task idle bitmap will auto clear A bits on read
		if (is_pfn_bitmap)
			ret = setidlemap(offset, size);
		if (ret)
			goto out;

		// sleep
		gettimeofday(&ts2, NULL);
		usleep((int)(interval * 1000000));
		gettimeofday(&ts3, NULL);

		// read idle flags
		ret = loadidlemap(offset, size);
		if (ret)
			goto out;

		gettimeofday(&ts4, NULL);
		ret = account_refs();
		if (ret)
			goto out;
		gettimeofday(&ts5, NULL);

		// calculate times
		set_us = tv_delta(ts1, ts2);
		slp_us = tv_delta(ts2, ts3);
		read_us = tv_delta(ts3, ts4);
		account_us = tv_delta(ts4, ts5);
		dur_us = tv_delta(ts1, ts5);

		if (g_debug) {
			printf("set time   : %.6f s\n", (double)set_us / 1000000);
			printf("sleep time : %.6f s\n", (double)slp_us / 1000000);
			printf("read time  : %.6f s\n", (double)read_us / 1000000);
			printf("count time : %.6f s\n", (double)account_us / 1000000);
			printf("dur time   : %.6f s\n", (double)dur_us / 1000000);
		}
	}

	output_refs_count(loop, output_file);
	plot_output(argv, output_file);

out:
	if (g_idlefd)
		close(g_idlefd);
	if (g_kpageflags_fd)
		close(g_kpageflags_fd);

	if (g_idlebuf)
		free(g_idlebuf);
	if (g_refs_count)
		free(g_refs_count);
	if (g_kpageflags_buf)
		free(g_kpageflags_buf);

	return ret;
}
