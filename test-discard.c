/**
 * (C)2009 Red Hat, Inc., Lukas Czerner <lczerner@redhat.com>
 *
 * What it does ?
 * Invoke ioctl with BLKDISCARD flag and defined range repetitively as the
 * specified amount of is discarded. Running time of each ioctl invocation
 * is measured and stored (also min and max) as well as number of 
 * invocations,range size. From this data we can compute average ioctl
 * running time, overall ioctl sunning time (sum) and throughput.
 *
 * usage: 
 *	<program> [-h] [-b] [-s start] [-r record_size] [-t total_size] 
 *	[-d device] [-R start:end:step]
 *		
 *	-s num Starting point of the discard
 *	-r num Size of the record discarded in one step
 *	-R start:end:step Define record range to be tested
 *	-t num Total amount of discarded data
 *	-d dev Device which should be tested
 *	-b     Output will be optimized for scripts
 *		
 *	 <record_size> <total_size> <min> <max> <avg> <sum> <throughput in MB/s>
 *
 *	-h     Print this help
 *
 *	\"num\" can be specified either as a ordinary number, or as a
 *	number followed by the unit. Supported units are
 *		k|K - kilobytes (n*1024)
 *		m|M - megabytes (n*1024*1024)
 *		g|G - gigabytes (n*1024*1024*1024)
 *
 * Examples:
 *	./test-discard -s 10k -r 4k -t 10M -d /dev/sdb1
 *	start : 10240
 *	record_size : 4096
 *	total_size : 10485760
 *	device : /dev/sdb1
 *
 *	./test-discard -t 100m -R 4k:64k:4k -d /dev/sdb1
 *	start : 0
 *	record_size from 4096 to 65536 with the step 4096 is tested.
 *	total size : 104857600
 *	device : /dev/sdb1
 *
 *	./test-discard -t 50m -R 4k:1024k:4k -d /dev/sdb1 -b
 *	This is the same as above but output will be more script friendly.
 *	Output will be like:
 *
 *	4096 10485760 0.000054 0.143009 0.089510 229.145599 0.043640
 *	8192 10485760 0.000060 0.155563 0.086155 110.278017 0.090680
 *	12288 10485760 0.088581 0.179759 0.099331 84.828429 0.117885
 *	16384 10485760 0.077438 0.177407 0.092211 59.015074 0.169448
 *	20480 10485760 0.076724 0.142325 0.087052 44.570576 0.224363
 *	...
 *	...etc
 */

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/time.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define DEF_REC_SIZE 4096ULL		/* 4KB  */
#define DEF_TOT_SIZE 10485760ULL	/* 10MB */

#define ENT_SIZE 1024

#define BATCH	0
#define HUMAN	1

int stop;


/**
 * Structure for collecting statistics data
 */
struct statistics {
	double min;
	double max;
	double sum;
	unsigned long count;
};


/**
 * Structure for definitions of the run
 */
struct definitions {
	unsigned long long start;
	unsigned long long record_size;
	unsigned long long total_size;
	char target[PATH_MAX];
	int fd;
	char output;
};


/**
 * Structure for surveying record size space
 */
struct records {
	unsigned long long start;
	unsigned long long end;
	unsigned long long step;
};


/**
 * Print program usage
 */
void usage(char *program) {
	fprintf(stdout, "%s [-h] [-b] [-s start] [-r record_size] [-t total_size] [-d device] [-R start:end:step]\n\n\
	-s num Starting point of the discard\n\
	-r num Size of the record discarded in one step\n\
	-R start:end:step Define record range to be tested\n\
	-t num Total amount of discarded data\n\
	-d dev Device which should be tested\n\
	-b     Output will be optimized for scripts\n\
	<record_size> <total_size> <min> <max> <avg> <sum> <throughput in MB/s>\n\
	-h     Print this help\n\n\
	\"num\" can be specified either as a ordinary number, or as a\n\
	number followed by the unit. Supported units are\n\n\
	k|K - kilobytes (n*1024)\n\
	m|M - megabytes (n*1024*1024)\n\
	g|G - gigabytes (n*1024*1024*1024)\n\n\
	Example:\n\
	<program> -s 10k -r 4k -t 10M -d /dev/sdb1\n\
	start : 10240\n\
	record_size : 4096\n\
	total_size : 10485760\n\
	device : /dev/sdb1\n",program);
} /* usage */


/**
 * Discards defined amount of data on the device by issuing ioctl with defined 
 * record size as many times as needed to fill total_size. 
 */
int run_ioctl(
	struct definitions *defs,
	struct statistics *stats) 
{
	unsigned long long next_hop, next_start;
	double time;
	struct timeval tv_start, tv_stop;
	unsigned long long range[2];

	/* Sanity check */
	if ((defs->record_size < 1) || 
		(defs->total_size < defs->record_size)) 
	{
		fprintf(stderr,
			"Insane boundaries! Block size = %llu, Total size = %llu\n"
			,defs->record_size,defs->total_size);
		return 1;
	}
	
	next_start = defs->start;
	next_hop = next_start + defs->record_size;

	/* ioctl loop */
	stop=0;
	while (!stop) {

		if (next_hop >= (defs->total_size + defs->start)) {
			next_hop = (defs->total_size + defs->start);
			stop = 1;
		}

		if (gettimeofday(&tv_start, (struct timezone *) NULL) == -1) {
			perror("gettimeofday");
			return 1;
		}

		range[0] = next_start;
		range[1] = next_hop;
		if (ioctl(defs->fd, BLKDISCARD, &range) == -1) {
			perror("Ioctl BLKDISCARD");
			return 1;
		}
		/*fprintf(stderr,"Calling record DISCARD from %llu to %llu\n",
			next_start,next_hop);*/

		if (gettimeofday(&tv_stop, (struct timezone *) NULL) == -1) {
			perror("gettimeofday");
			return 1;
		}
	
		/* time diff */	
		time = (double) tv_stop.tv_sec + \
			(((double) tv_stop.tv_usec) * 0.000001);
		time -= (double) tv_start.tv_sec + \
			(((double) tv_start.tv_usec) * 0.000001);
	
		/* collect some statistics */
		if (time > stats->max)
			stats->max = time;
		if (time < stats->min)
			stats->min = time;
		stats->sum += time;
		stats->count++;

		next_start = next_hop;
		next_hop += defs->record_size;
	}
	return 0;
} /* run_ioctl */


/**
 * Get the number from argument. It can be number followed by
 * units: k|K,m|M,g|G
 */
unsigned long long 
get_number(char **optarg) {
	char *opt;
	unsigned long long number,max;

	/* get the max to avoid overflow */
	max = ULLONG_MAX / 10;
	number = 0;
	opt = *optarg;
	
	/* compute the number */
	while ((*opt >= '0') && (*opt <= '9') && (number < max)) {
		number = number * 10 + *opt++ - '0';
	}
	while (1) {
		/* determine if units are defined */
		switch(*opt++) {
			case 'K': /* kilobytes */
			case 'k': 
				number *= 1024;
				break;
			case 'M': /* megabytes */
			case 'm':
				number *= 1024 * 1024;
				break;
			case 'G': /* gigabytes */
			case 'g':
				number *= 1024 * 1024 * 1024;
				break;
			case ':': /* delimiter */
				if ((number > max) || (number == 0)) {
					fprintf(stderr,"Numeric argument out of range\n");
					return 0;
				}
				*optarg = opt;
				return number;
			case '\0': /* end of the string */
				if ((number > max) || (number == 0)) {
					fprintf(stderr,"Numeric argument out of range\n");
					return 0;
				}
				return number;
			default:
				fprintf(stderr,"Bad syntax of numeric argument\n");
				return 0;
		}
	}
	return number;
} /* get_number */


/**
 * Get the record ranges from the format start:end:step
 */
int get_range(char *optarg,struct records *rec) {
	char *opt;

	opt = optarg;

	if ((rec->start = get_number(&opt)) == 0) {
		return 0;
	}
	if ((rec->end = get_number(&opt)) == 0) {
		return 0;
	}
	if ((rec->step = get_number(&opt)) == 0) {
		return 0;
	}

	if ((rec->start > rec->end) || 
	   ((rec->start + rec->step) > rec->end) ||
	   (rec->step == 0))
	{
		fprintf(stderr,"Insane record range: %llu:%llu:%llu\n",
			rec->start,rec->end,rec->step);
		return 0;
	}

	return 1;
} /* get_range */


/**
 * Determine device size to avoid issuing discard command
 * out of the device size.
 */
unsigned long long
get_device_size(const int fd) {
	unsigned long long nblocks;

/*
 *BLKBSZGET - logical block size
 *BLKSSZGET - hw block size
 *BLKGETSIZE - return device size /512 (long *arg) 
 *BLKGETSIZE64 - return device size in bytes (u64 *arg) 
 * */

	if (ioctl(fd, BLKGETSIZE64 ,&nblocks) == -1) {
		perror("Ioctl block device");
		return 0;
	}

	return nblocks;
} /* get_device_size */


/**
 * Get some random data to put on the device
 */
int get_entropy(char *entropy, int size) {
	int ent_fd;
	
	ent_fd=open("/dev/urandom",O_RDONLY);
	return read(ent_fd,entropy,size);
} /* get_entropy */


/**
 * Write some data to the device in order to prevent
 * discarding already discarded blocks
 */
int prepare_device (struct definitions *defs) {
	char entropy[ENT_SIZE];
	unsigned long long total;
	int step;

	get_entropy(entropy,ENT_SIZE);

	if (lseek(defs->fd,defs->start,SEEK_SET) == -1) {
		perror("prepare_device lseek");
		return -1;
	}

	total = 0;
	while (total < defs->total_size) {
		if ((step = write(defs->fd,entropy,ENT_SIZE)) == -1) {
			perror("prepare_device write");
			return -1;
		}
		total += step;
	}

	fsync(defs->fd);

	return 0;
} /* prepare_device */


/**
 * Print results
 */
void print_results(
	struct definitions *defs,
	struct statistics *stats
	)
{
	if (defs->output == HUMAN) {

		/* Print results */
		fprintf(stdout,"\n[+] RESULTS\nmin = %lfs\nmax = %lfs\navg = %lfs\n",
			stats->min, stats->max, 
			stats->sum/(double) stats->count
		);
		fprintf(stdout,"count = %ld\nsum = %lfs\nthroughput = %lf MB/s\n",
			stats->count, stats->sum,
			(defs->total_size/(1024*1024))/stats->sum
		);

	} else {

		fprintf(stdout,"%llu %llu %lf %lf %lf %lf %lf\n",
			defs->record_size,
			defs->total_size,
			stats->min,
			stats->max,
			stats->sum/(double) stats->count,
			stats->sum,
			(defs->total_size/(1024*1024))/stats->sum
		);
	}
} /* print results */

/**
 * Run single ioctl test defined by structure defs
 * and print out the results 
 */
int test_step(struct definitions *defs) {
	double time;
	struct timeval tv_start, tv_stop;
	struct statistics stats;
	int err;

	/* initialize statistic structure */
	stats.min = INT_MAX; /* This is big enough */
	stats.max = 0;
	stats.sum = 0;
	stats.count = 0;

	if (defs->output == HUMAN) {
		fprintf(stdout,"[+] Preparing device\n");
	}
	if (prepare_device(defs) == -1) {
		return -1;
	}
	
	if (defs->output == HUMAN) {
		fprintf(stdout,"[+] Testing\n");
	}

	/* start timer */
	if (gettimeofday(&tv_start, (struct timezone *) NULL) == -1) {
		perror("gettimeofday");
		return -1;
	}

	err = run_ioctl(defs, &stats);

	/* stop timer */
	if (gettimeofday(&tv_stop, (struct timezone *) NULL) == -1) {
		perror("gettimeofday");
		return -1;
	}

	if (err) {
		return -1;
	} 

	/* time diff */	
	time = (double) tv_stop.tv_sec + \
		(((double) tv_stop.tv_usec) * 0.000001);

	time -= (double) tv_start.tv_sec + \
		(((double) tv_start.tv_usec) * 0.000001);

	print_results(defs,&stats);

	return 0;

} /* test_step */


int main (int argc, char **argv) {
	int c, err;
	struct stat sb;
	struct definitions defs;
	struct records rec;
	unsigned long long dev_size, repeat;

	defs.record_size = DEF_REC_SIZE;
	defs.total_size = DEF_TOT_SIZE;
	defs.start = 0;
	defs.output = HUMAN;
	rec.step = 0;

	while ((c = getopt(argc, argv, "hbs:r:t:d:R:")) != EOF) {
		switch (c) {
			case 's': /* starting point */
				if ((defs.start = get_number(&optarg)) == 0) {
					usage(argv[0]);
					return EXIT_FAILURE;
				}
				break;
			case 'R': /* record size range */
				if (get_range(optarg,&rec) == 0) {
					usage(argv[0]);
					return EXIT_FAILURE;
				}
				break;
			case 'r': /* record size */
				if ((defs.record_size = get_number(&optarg)) == 0) {
					usage(argv[0]);
					return EXIT_FAILURE;
				}
				break;
			case 't': /* total size */
				if ((defs.total_size = get_number(&optarg)) == 0) {
					usage(argv[0]);
					return EXIT_FAILURE;
				}
				break;
			case 'd': /* device name */
				strncpy(defs.target, optarg, sizeof(defs.target));
				break;
			case 'h': /* help */
				usage(argv[0]);
				return EXIT_SUCCESS;
				break;
			case 'b':
				defs.output = BATCH;
				break;
			default:
				usage(argv[0]);
				break;
		}
	}

	if (strnlen(defs.target,1) < 1) {
		fprintf(stderr,"You must specify device\n");
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	if (stat(defs.target,&sb) == -1) {
		perror("stat");
		fprintf(stderr,"%s is not a valid device\n", defs.target);
		return EXIT_FAILURE;
	}

	if (!S_ISBLK(sb.st_mode)) {
		fprintf(stderr,"%s is not a valid device\n", defs.target);
		return EXIT_FAILURE;
	}

	/* open device */
	if ((defs.fd = open(defs.target, O_RDWR)) == -1) {
		perror("Opening block device");
		return EXIT_FAILURE;
	}

	if (rec.step == 0) {
		repeat = 1;
	} else {
		repeat = ((rec.end - rec.start) / rec.step) + 1;
		defs.record_size = rec.start;
	}

	err = 0;
	for (unsigned long long i = 1;i <= repeat;i++) {

		/* check boundaries */
		if ((dev_size =get_device_size(defs.fd)) == 0) {
			return EXIT_FAILURE;
		}
		if ((defs.start + defs.total_size) > dev_size) {
			fprintf(stderr,"Boundaries does not fit in the device\n");
			return EXIT_FAILURE;
		}
		
		if (defs.output == HUMAN) {
			fprintf(stdout,"\n[+] Running test\n");
			fprintf(stdout,"Start: %llu\nRecord size: %llu\nTotal size: %llu\n\n",
				defs.start,defs.record_size,defs.total_size);
		}

		/* run test */
		if ((err = test_step(&defs)) == -1)
			break;

		/* next boundaries */
		defs.record_size = rec.start + rec.step * i;
	} 

	/* close device */
	if (close(defs.fd) == -1) {
		perror("Closing block device");
		return EXIT_FAILURE;
	}

	if (err == -1) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
} /* main */
