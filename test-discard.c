/**
 * (C)2009 Red Hat, Inc., Lukas Czerner <lczerner@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
 *	[-d device] [-R start:end:step] [-z]
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
 *	-z     Discard already discarded blocks
 *	-x     Run test witch random IO pattern [-s] will be ignored
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
 *
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
#include <time.h>
#include <stdint.h>

#include "libs/rbtree.h"

/* Do not call BLKDISCARD ioctl() */
/*#define DEBUG_NO_DISCARD*/

/* Do not prepare device prior testing */
/*#define DEBUG_NO_PREPARE*/

#define DEF_REC_SIZE 4096ULL		/* 4KB  */
#define DEF_TOT_SIZE 10485760ULL	/* 10MB */

#define ENT_SIZE 4096			/* size of entropy */

#define BATCHOUT	1		/* batch output */
#define DISCARD2	2		/* discard already discarded */
#define RANDOMIO	4		/* random IO pattern */

#define IS_HUMAN(x)		(~x & BATCHOUT)
#define IS_DISCARD2(x)		(x & DISCARD2)
#define IS_RANDOMIO(x)		(x & RANDOMIO)

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
	unsigned long long dev_size;
	int dev_ssize;
	char target[PATH_MAX];
	int fd;
	int flags;
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
 * Structure for creating tree of discarded
 * records
 */
struct discarded_entry {
	unsigned long long start;
	unsigned long long count;
	struct rb_node node;
};

struct rb_root discarded_root;

void free_entry(struct discarded_entry **entry) {
	free(*entry);
	*entry = NULL;
}

/**
 * Free the tree
 */
void free_tree(void) {
	struct rb_node *node, *next;
	struct discarded_entry *entry;

	for (node = rb_first(&discarded_root); node; node = next) {
		next = rb_next(node);
		entry = rb_entry(node, struct discarded_entry, node);
		rb_erase(node, &discarded_root);
		free_entry(&entry);
	}

} /* free_tree */


/**
 * Print critical error message, free list and exit
 */
void crit_err(void) {
	fprintf(stderr,"Critical failure: You found a BUG!\n");
	free_tree();
	exit(EXIT_FAILURE);
} /* crit_err */


/**
 * Print program usage
 */
void usage(char *program) {
	fprintf(stdout, "%s [-h] [-b] [-s start] [-r record_size] "
	"[-t total_size] [-d device] [-R start:end:step] \
	[-z] [-x]\n\n\
	-s num Starting point of the discard\n\
	-r num Size of the record discarded in one step\n\
	-R start:end:step Define record range to be tested\n\
	-t num Total amount of discarded data\n\
	-d dev Device which should be tested\n\
	-b     Output will be optimized for scripts\n\
	<record_size> <total_size> <min> <max> <avg> <sum> <throughput in MB/s>\n\
	-z     Discard already discarded blocks\n\
	-x     Run test witch random IO pattern [-s] will be ignored\n\
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
 * Get random block number on the disk
 */
long int
get_random_block(struct definitions *defs) 
{
	unsigned long long max;

	max = defs->dev_size / defs->record_size;
	if (max > RAND_MAX) {
		fprintf(stderr,"Warning: I can not use whole disc.\n");
		max = RAND_MAX;
	}

	return (random() % max);
} /* get_random_block */

/**
 * Allocate new item and initialize it with block 
 * values
 */
struct discarded_entry *alloc_and_init(long int block) {
	struct discarded_entry *new = NULL;

	if ((new = malloc(sizeof(struct discarded_entry))) == NULL) {
		perror("malloc");
		return NULL;
	}	

	new->start = block;
	new->count = 1;

	return new;
} /* alloc_and_init */

/**
 * Guess the next block in three steps :
 * 1. Get random block
 * 2. Search for the matching extent
 * 3. If not found create new one, otherwise extend existing
 * 4. See if we can merge to the right
 */
int guess_next_block(struct definitions *defs) 
{
	long int block;
	struct rb_node *parent = NULL, **n = &discarded_root.rb_node;
	struct discarded_entry *entry, *new_entry;
	struct rb_node *new_node, *node;

	block = get_random_block(defs);

	while (*n) {
		parent = *n;
		entry = rb_entry(parent, struct discarded_entry, node);

		if (block < entry->start) {
			n = &(*n)->rb_left;
		} else if (block > (entry->start + entry->count)) {
			n = &(*n)->rb_right;
		} else {
			long int end = entry->start + entry->count;

			if (end > (defs->dev_size / defs->record_size)) {
				block = 0;
				n = &discarded_root.rb_node;
				continue;
			}

			entry->count += 1;
			block = end;
			new_entry = entry;
			new_node = &new_entry->node;
			goto skip_insert;
		}
	}

	if ((new_entry = alloc_and_init(block)) == NULL)
		return -1;
	new_node = &new_entry->node;
	rb_link_node(new_node, parent, n);
	rb_insert_color(new_node, &discarded_root);

skip_insert:
	/* See if we can merge to the right */
	node = rb_next(new_node);
	if (node) {
		entry = rb_entry(node, struct discarded_entry, node);
		if (entry->start == new_entry->start + new_entry->count) {
			new_entry->count += entry->count;
			rb_erase(node, &discarded_root);
			free_entry(&entry);
		}
	}

	return block;

} /* guess_next_block */

/**
 * Discards defined amount of data on the device by issuing ioctl with defined 
 * record size as many times as needed to fill total_size. 
 */
int run_ioctl(
	struct definitions *defs,
	struct statistics *stats) 
{
	uint64_t next_hop, next_start;
	double time;
	struct timeval tv_start, tv_stop;
	long int block;
	uint64_t range[2];

	/* Sanity check */
	if ((defs->record_size < 1) || 
		(defs->total_size < defs->record_size)) 
	{
		fprintf(stderr,
			"Insane boundaries! Block size = %llu,"
			" Total size = %llu\n"
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

		if (IS_RANDOMIO(defs->flags)) {
			
			if ((block = guess_next_block(defs)) == -1) {
				return 1;
			}

			range[0] = block * defs->record_size;
			range[1] = defs->record_size;

			if ((range[0] + range[1]) > defs->dev_size) {
				range[1] = defs->dev_size - range[0];
			}
		} else {
			range[0] = next_start;
			range[1] = defs->record_size;
		}

		if (gettimeofday(&tv_start, (struct timezone *) NULL) == -1) {
			perror("gettimeofday");
			return 1;
		}

#ifndef DEBUG_NO_DISCARD
		if (ioctl(defs->fd, BLKDISCARD, &range) == -1) {
			perror("Ioctl BLKDISCARD");
			return 1;
		}
#endif

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
					fprintf(stderr,"Numeric argument "
						"out of range\n");
					return 0;
				}
				*optarg = opt;
				return number;
			case '\0': /* end of the string */
				if ((number > max) || (number == 0)) {
					fprintf(stderr,"Numeric argument "
						"out of range\n");
					return 0;
				}
				return number;
			default:
				fprintf(stderr,"Bad syntax of "
					"numeric argument\n");
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
	uint64_t nblocks;

/*
 *BLKBSZGET - logical block size
 *BLKSSZGET - hw block size
 *BLKGETSIZE - return device size /512 (long *arg) 
 *BLKGETSIZE64 - return device size in bytes (u64 *arg) 
 * */

	if (ioctl(fd, BLKGETSIZE64,&nblocks) == -1) {
		perror("Ioctl block device");
		return 0;
	}

	return (unsigned long long)nblocks;
} /* get_device_size */


/**
 * Determine device sector size
 */
int get_sector_size(const int fd) {
	int ssize;

	if (ioctl(fd, BLKSSZGET, &ssize) == -1) {
		perror("Ioctl block device");
		return 0;
	}

	return ssize;
} /* get sector size */


/**
 * Get some random data to put on the device
 */
int get_entropy(char *entropy, int size) {
	int ent_fd;
	
	if ((ent_fd = open("/dev/urandom",O_RDONLY)) == -1) {
		perror("Opening urandom device");
		return -1;
	}

	read(ent_fd,entropy,size);
	close(ent_fd);

	return 0;
} /* get_entropy */


/**
 * Write defined amount of pseudorandom  data to the device
 */
int write_data(int fd, uint64_t start, uint64_t size) {
	char entropy[ENT_SIZE];
	ssize_t step;
	int64_t total;

	if (get_entropy(entropy,ENT_SIZE) == -1) {
		fprintf(stderr,"Error while gathering entropy\n");
		return -1;
	}

	if (lseek64(fd,(off64_t)start,SEEK_SET) == -1) {
		perror("write_data lseek");
		return -1;
	}

	total = (int64_t)((size / (double)ENT_SIZE) + 0.5);

	while (total) {
		total--;

		if ((step = write(fd,entropy,ENT_SIZE)) == -1) {
			if (errno == ENOSPC) {
				continue;
			}
			perror("write_data write");
			return -1;
		}

		if (step < (ssize_t)ENT_SIZE) {
			fprintf(stderr,"write_data: Written size is "
				"smaller than expected\n");
			return -1;
		}
	}

	fsync(fd);

	return 0;
} /* write data */


/**
 * Write some data to the device in order to prevent
 * discarding already discarded blocks
 */
int prepare_device (struct definitions *defs) {
	uint64_t total;

	if (IS_RANDOMIO(defs->flags)) {
		defs->start = 0;
		total = defs->dev_size;
	} else {
		total = defs->total_size;
	}

#ifndef DEBUG_NO_PREPARE
	return write_data(defs->fd,defs->start,total);
#else
	return 0;
#endif
} /* prepare_device */


/**
 * Overwrite only discarded blocks.
 * Instead of overwriting whole disk with random
 * data in order to prevent discarding already discarded
 * blocks, simply overwrite only data discarded in 
 * previous step - this information is stored in the 
 * DISCARDED_LIST
 */
int prepare_by_tree(struct definitions *defs) {
	struct rb_node *node;
	struct discarded_entry *entry, *prev = NULL;
	char entropy[ENT_SIZE];
	unsigned long long total;

	get_entropy(entropy,ENT_SIZE);

	for (node = rb_first(&discarded_root); node; node = rb_next(node)) {
		entry = rb_entry(node, struct discarded_entry, node);

		total = (entry->count * defs->record_size);

		if (total == 0) {
			fprintf(stderr, "Programming error: total = %llu\n",
				total);
			crit_err();
		}

		if (prev && (prev->start + prev->count >= entry->start)) {
			fprintf(stderr, "Programming error tree is corrupted:\n"
					" prev %llu->%llu(%llu)\n"
					" cur %llu->%llu(%llu)\n",
					prev->start, prev->start + prev->count,
					prev->count, entry->start,
					entry->start + entry->count,
					entry->count);
			crit_err();
		}

#ifndef DEBUG_NO_PREPARE
		if (write_data(defs->fd,
		    (entry->start * defs->record_size),total) == -1) {
			return -1;
		}
#endif
		prev = entry;
	}
	fsync(defs->fd);

	return 0;
} /* prepare_by_tree */


/**
 * Print results
 */
void print_results(
	struct definitions *defs,
	struct statistics *stats
	)
{
	if (IS_HUMAN(defs->flags)) {

		/* Print results */
		fprintf(stdout,"[+] RESULTS\nmin = %lfs\nmax = %lfs\navg = %lfs\n",
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

	if (IS_HUMAN(defs->flags)) {
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


/**
 * Discard part of the device or whole device if the
 * randomio flag is set
 */
int discard_whole_device(struct definitions *defs) {
	uint64_t range[2];

	range[0] = 0;
	range[1] = defs->dev_size;

#ifndef DEBUG_NO_DISCARD
	if (ioctl(defs->fd, BLKDISCARD, &range) == -1) {
		perror("Ioctl BLKDISCARD");
		return -1;
	}
#endif

	return 0;
} /* discard_whole_device */

/**
 * Open the device and get infos about it
 */
int open_device(struct definitions *defs) {
	
	if ((defs->fd = open(defs->target, O_RDWR)) == -1) {
		perror("Opening block device");
		return -1;
	}

	if ((defs->dev_size = get_device_size(defs->fd)) == 0) {
		close(defs->fd);
		return -1;
	}

	if ((defs->dev_ssize = get_sector_size(defs->fd)) == 0) {
		close(defs->fd);
		return -1;
	}

	return 0;
} /* open_device */


/**
 * Basic check of parameters sanity
 */
int check_sanity(struct definitions *defs) {

	if (defs->total_size % defs->dev_ssize) {
		fprintf(stderr,"Total size must be aligned to the sector size\n");
		return -1;
	}

	if (defs->record_size % defs->dev_ssize) {
		fprintf(stderr,"Record size must be aligned to the sector size\n");
		return -1;
	}

	if (defs->start % defs->dev_ssize) {
		fprintf(stderr,"Starting point must be aligned to the sector size\n");
		return -1;
	}

	/* check boundaries */
	if ((defs->start + defs->total_size) > defs->dev_size) {
		fprintf(stderr,"Boundaries does not fit in the device\n");
		return -1;
	}

	return 0;
} /* check_sanity */

int main (int argc, char **argv) {
	int c, err;
	struct stat sb;
	struct definitions defs;
	struct records rec;
	unsigned long long repeat, i;

	defs.record_size = DEF_REC_SIZE;
	defs.total_size = DEF_TOT_SIZE;
	defs.start = 0;
	defs.flags = 0;
	rec.step = 0;

	while ((c = getopt(argc, argv, "hxzbs:r:t:d:R:")) != EOF) {
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
				defs.flags |= BATCHOUT;
				break;
			case 'z':
				defs.flags |= DISCARD2;
				break;
			case 'x':
				defs.flags |= RANDOMIO;
				srandom((unsigned) time(NULL));
				defs.start = 0;
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
	if (open_device(&defs) == -1) {
		return EXIT_FAILURE;
	}

	if (check_sanity(&defs) == -1) {
		close(defs.fd);
		return EXIT_FAILURE;
	}
	
	/* Initial discard */
	if (IS_HUMAN(defs.flags)) {
		fprintf(stdout,"[+] Discarding device\n");
	}
	if (discard_whole_device(&defs) == -1) {
		close(defs.fd);
		return EXIT_FAILURE;
	}

	if (rec.step == 0) {
		repeat = 1;
	} else {
		repeat = ((rec.end - rec.start) / rec.step) + 1;
		defs.record_size = rec.start;
	}

	err = 0;
	for (i = 1;i <= repeat;i++) {

		/* round total size to the multiple of the record_size */
		defs.total_size = (unsigned long long)
			((defs.total_size / (double)defs.record_size) + 0.5);
		defs.total_size *= (defs.record_size);

		/* check boundaries */
		if ((defs.start + defs.total_size) > defs.dev_size) {
			defs.total_size = defs.dev_size;
		}

		/* Prepare device if we are not in the DISCARD2 mode */
		if (!IS_DISCARD2(defs.flags)) {
			
			if (IS_HUMAN(defs.flags)) {
				fprintf(stdout,"[+] Preparing device\n");
			}
			
			if ((!IS_RANDOMIO(defs.flags)) || (i == 1)) {
				if ((err = prepare_device(&defs)) == -1) {
					break;
				}
			}
		}
		
		/* Initialize list in random IO mode */
		if (IS_RANDOMIO(defs.flags))
			discarded_root = RB_ROOT;
		
		if (IS_HUMAN(defs.flags)) {
			fprintf(stdout,"\n[+] Running test\n");
			fprintf(stdout,"Start: %llu\nRecord size: %llu\nTotal size: %llu\n\n",
				defs.start,defs.record_size,defs.total_size);
		}

		/* run test */
		if ((err = test_step(&defs)) == -1) {
			break;
		}

		/* Prepare device if we are not in the DISCARD2 
		 * mode and we are in random IO mode*/
		if ((!IS_DISCARD2(defs.flags)) && (IS_RANDOMIO(defs.flags))) {
			
			if (IS_HUMAN(defs.flags)) {
				fprintf(stdout,"[+] Preparing device\n");
			}
			
			if (i <= repeat) {
				if ((err = prepare_by_tree(&defs)) == -1) {
					break;
				}
			}
		}

		/* next boundaries */
		defs.record_size = rec.start + rec.step * i;
	} 

	free_tree();

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
