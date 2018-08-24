/*
 * TBD
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>

#define IDLEMAP_CHUNK_SIZE	8
#define IDLEMAP_BUF_SIZE	4096

// big enough to span 640 Gbytes:
#define MAX_IDLEMAP_SIZE	(20 * 1024 * 1024)	//20M * 8 * 4K = 640G ?
#define MAX_FILE_PATH	255

//#define DEBUG

#ifdef DEBUG
#define debug_printf(fmt, args...) \
		printf(fmt, ##args);
#else
#define debug_printf(fmt, args...)
#endif

// globals
int g_debug;			// 1 == some, 2 == verbose
unsigned long long *g_idlebuf;
unsigned long long g_idlebufsize;
unsigned short *g_refs_count;
unsigned long long g_offset, g_size;
unsigned long long g_start_pfn, g_num_pfn;
unsigned char g_setidle_buf[IDLEMAP_BUF_SIZE];
int g_setidle_bufsize;
int g_idlefd;

// on return:
// 	for nrefs in [0, max]:
// 		count_array[nrefs] = npages;
int count_refs(unsigned int max, unsigned long count_array[])
{
	unsigned long long pfn;
	unsigned short nrefs;

	memset(count_array, 0, max + 1);

	for (pfn = 0; pfn < g_num_pfn; pfn++) {
		nrefs = g_refs_count[pfn];
		if (nrefs <= max)
			count_array[nrefs]++;
		else
			return 1;
	}

	return 0;
}

int output_refs_count(unsigned int loop, const char *output_file)
{
	unsigned short nrefs;
	FILE *file;
	unsigned long refs_count[loop];

	if (!g_refs_count)
		return -1;

	if (count_refs(loop, refs_count)) {
		fprintf(stderr, "refs_count out of range\n");
		return -1;
	}

	if ((file = fopen(output_file, "w")) == NULL) {
		perror("Can't open to write the output file");
		exit(2);
	}

	for (nrefs = 0; nrefs <= loop; nrefs++) {
		fprintf(file, "%3u %15lu\n", (unsigned int)nrefs, refs_count[nrefs]);
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
	unsigned long long idlebits, idlemap = 0, base_pfn = 0, pfn;

	while (len < g_idlebufsize) {
		// need to check ?
		idlebits = g_idlebuf[idlemap];
		debug_printf("idlebits: 0x%llx\n", idlebits);

		for (pfn = 0; pfn < 64; pfn++) {
			if (!(idlebits & (1ULL << pfn))) {
				g_refs_count[base_pfn + pfn]++;
				debug_printf("pfn 0x%llx refs_count 0x%llx\n",
					     base_pfn + pfn,
					     g_refs_count[base_pfn + pfn]);
			}
		}

		len += IDLEMAP_CHUNK_SIZE;
		idlemap = len / IDLEMAP_CHUNK_SIZE;
		base_pfn = idlemap * 64;
	}

	return 0;
}

int setidlemap(unsigned long long offset, unsigned long long size)
{
	char *p;
	int i;
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
	g_idlebufsize = 0;

	// unfortunately, larger reads do not seem supported
	while ((len = read(g_idlefd, p, IDLEMAP_CHUNK_SIZE)) > 0) {
		p += len;
		g_idlebufsize += len;
		size -= len;
		if (size <= 0)
			break;
	}
	debug_printf("g_idlebufsize: 0x%llx\n", g_idlebufsize);

_loadidlemap_out:
	return err;
}

static const struct option opts[] = {
	{"offset",	required_argument,	NULL,	'o'},
	{"size",	required_argument,	NULL,	's'},
	{"interval",	required_argument,	NULL,	'i'},
	{"loop",	required_argument,	NULL,	'l'},
	{"bitmap",	required_argument,	NULL,	'b'},
	{"output",	required_argument,	NULL,	'f'},
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
		"    -f|--output	The output file for the result of scanning.\n",
		prog);
}

int main(int argc, char *argv[])
{
	//double duration, mbytes;
	int i, ret = 0;
	double interval = 0.1;
	unsigned short loop = 1;
	int pagesize, optind, opt = 0, options_index = 0;
	char bitmap_file[MAX_FILE_PATH] = "bitmap_file";
	char output_file[MAX_FILE_PATH] = "output_file";
	const char *optstr = "ho:s:i:l:b:f:";
	unsigned long long offset = 0, size = 0, bufsize;
	unsigned long long set_us, read_us, dur_us, slp_us, account_us, est_us;
	static struct timeval ts1, ts2, ts3, ts4, ts5;

	// handle huge pages ?
	pagesize = getpagesize();

	/* parse the options */
	while ((opt = getopt_long(argc, argv, optstr, opts, &options_index)) != EOF) {
		switch (opt) {
		case 0:
			break;
		case 'o':
			offset = strtoul(optarg, NULL, 16);
			debug_printf("offset = 0x%llx\n", offset);
			offset /= pagesize * 8;
			debug_printf("offset of the bitmap = 0x%llx, pagesize = 0x%x\n", offset, pagesize);
			break;
		case 's':
			size = strtoul(optarg, NULL, 16);
			debug_printf("size = 0x%llx\n", size);
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
		case 'h':
			usage(argv[0]);
			exit(0);
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

	g_start_pfn = offset * 8;
	g_num_pfn = size * 8;
	debug_printf("The start pfn is 0x%llx, the number of pfn is 0x%llx.\n",
		     g_start_pfn, g_num_pfn);

	if ((g_refs_count = calloc(g_num_pfn, sizeof(unsigned short))) == NULL) {
		printf("Can't allocate memory for idlemap buf (%d bytes)!\n",
		       sizeof(unsigned short) * g_num_pfn);
		ret = -2;
		goto out;
	}

	for (i = 0; i < loop; i++) {
		// set idle flags
		gettimeofday(&ts1, NULL);
		ret = setidlemap(offset, size);
		if (ret) {
			goto out;
		}
		// sleep
		gettimeofday(&ts2, NULL);
		usleep((int)(interval * 1000000));
		gettimeofday(&ts3, NULL);

		// read idle flags
		ret = loadidlemap(offset, size);
		if (ret) {
			goto out;
		}
		gettimeofday(&ts4, NULL);

		ret = account_refs();
		if (ret) {
			goto out;
		}
		gettimeofday(&ts5, NULL);

		//TODO: create a routine func to simplify the below calculations
		// calculate times
		set_us = 1000000 * (ts2.tv_sec - ts1.tv_sec) +
				   (ts2.tv_usec - ts1.tv_usec);
		slp_us = 1000000 * (ts3.tv_sec - ts2.tv_sec) +
				   (ts3.tv_usec - ts2.tv_usec);
		read_us = 1000000 * (ts4.tv_sec - ts3.tv_sec) +
				    (ts4.tv_usec - ts3.tv_usec);
		account_us = 1000000 * (ts5.tv_sec - ts4.tv_sec) +
				       (ts5.tv_usec - ts4.tv_usec);
		dur_us = 1000000 * (ts5.tv_sec - ts1.tv_sec) +
				   (ts5.tv_usec - ts1.tv_usec);

		if (g_debug) {
			printf("set time   : %.6f s\n", (double)set_us / 1000000);
			printf("sleep time : %.6f s\n", (double)slp_us / 1000000);
			printf("read time  : %.6f s\n", (double)read_us / 1000000);
			printf("count time : %.6f s\n", (double)account_us / 1000000);
			printf("dur time   : %.6f s\n", (double)dur_us / 1000000);
		}
	}

	output_refs_count(loop, output_file);

out:
	if (g_idlefd)
		close(g_idlefd);

	if (g_idlebuf)
		free(g_idlebuf);
	if (g_refs_count)
		free(g_refs_count);

	return ret;
}
