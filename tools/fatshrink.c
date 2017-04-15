/*
 * fatshrink.c
 * Copyright (C) 2016 <sgerwk@aol.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * fatshrink.c
 *
 * show and change number of sectors in a fat
 */

#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <stdio.h>
#include <llfat.h>

void usage() {
	printf("usage:\n\tfatresize ([-m] [-t])|[-f] filename [size]\n");
	printf("\t\t-m\tmove clusters in the area to be cut\n");
	printf("\t\t-t\tremove clusters in the area to be cut\n");
	printf("\t\t-f\tresize even if some clusters are left out\n");
	printf("\t\t\t(the resulting filesystem would be incorrect)\n");
	printf("\t\tsize\tif omitted, only print number of sectors\n");
}

int main(int argn, char *argv[]) {
	char *filename;
	uint32_t sectors;
	int move = 0, truncate = 0, force = 0;
	int32_t clusters, begin, last, allocated, numcut;
	fat *f, *g;

			/* arguments */

	if (argn - 1 < 1) {
		usage();
		exit(1);
	}

	while (argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'm':
			move = 1;
			break;
		case 't':
			truncate = 1;
			break;
		case 'f':
			force = 1;
			break;
		case 'h':
			usage();
			exit(0);
		default:
			printf("unrecognized option: %s\n", argv[1]);
			usage();
			exit(1);
		}
		argn--;
		argv++;
	}

	filename = argv[1];
	f = fatopen(filename, 0);
	if (f == NULL) 
		return -1;
	
	if (0 != fatcheck(f)) {
		printf("invalid filesystem in %s\n", filename);
		return -1;
	}

	printf("filesystem: FAT%d\n", fatbits(f));
	printf("current sectors: %d\n", fatgetnumsectors(f));
	// fatsummary(f);

	if (argn - 1 <= 1) {
		fatclose(f);
		return 0;
	}

	if (argv[2][0] == '-') {
		printf("pass options before device/image name\n");
		exit(1);
	}
	sectors = atoi(argv[2]);
	printf("target sectors: %u\n", sectors);

			/* compare current and target clusters */

	if (sectors > fatgetnumsectors(f)) {
		printf("this program can only shrink a filesystem, ");
		printf("not elarge it\n");
		exit(1);
	}
	else if (sectors == fatgetnumsectors(f)) {
		printf("no change requested\n");
		exit(1);
	}
	else if (sectors < 10) {
		printf("invalid number of sectors: %d\n", sectors);
		printf("suggestion: pass -t/-m before the file name\n");
		exit(1);
	}

			/* estimate resulting filesystem */

	g = fatcreate();
	g->boot = fatunitcopy(f->boot);
	fatsetnumsectors(g, sectors);
	g->bits = 0;	// force recalculation of number of bits of fat
	if (fatbits(g) != fatbits(f)) {
		printf("cannot shrink, ");
		printf("it would change the number of bits of the FAT\n");
		exit(1);
	}
	clusters = fatnumdataclusters(g);
	printf("target clusters: %d\n", clusters);
	fatunitdestroy(g->boot);
	fatquit(g);

			/* clusters left, clusters cut */

	if (clusters < 0) {
		printf("not enough sectors for a filesystem\n");
		exit(1);
	}
	else if (clusters == fatnumdataclusters(f)) {
		printf("no change in number of clusters\n");
		goto resize;
	}
	begin = clusters + 2;
	last = fatlastcluster(f);

	printf("clusters left: %d-%d, ", 2, begin - 1);
	printf("cluster removed: %d-%d\n", begin, last);

	if (force)
		goto resize;

			/* status of clusters to be left out */

	allocated = fatclusterfindallocated(f, begin, last);
	if (allocated == FAT_ERR)
		goto resize;

	if (! move) {
		if (truncate)
			goto truncate;
		printf("some clusters in area to be cut\n");
		printf("(e.g., cluster %d)\n", allocated);
		printf("give option -m to move clusters\n");
		exit(1);
	}

	numcut = fatlastcluster(f) - begin + 1 -
		fatclusternumfreebetween(f, begin, last);
	printf("\nallocated clusters to be left out: %d\n", numcut);

	printf("free clusters in remaining area: %d\n",
		fatclusternumfreebetween(f, 2, begin - 1));

			/* move clusters as much as possible */

	printf("moving clusters: (%d,%d) ", begin, last);
	printf("-> (%d,%d)\n", 2, begin - 1);
	fatmovearea(f, begin, last, 2, begin - 1);
	allocated = fatclusterfindallocated(f, begin, last);
	if (allocated == FAT_ERR)
		goto resize;

	numcut = last - begin + 1 -
		fatclusternumfreebetween(f, begin, last);
	printf("allocated clusters to be left out: %d\n", numcut);

	if (! truncate) {
		printf("give option -t to truncate files\n");
		exit(1);
	}

			/* truncate files */

truncate:
	printf("\ntruncating to clusters (%d,%d)\n", 2, begin - 1);
	fattruncate(f, clusters);

			/* resize */

resize:
	printf("\nchanging the number of sectors to %u\n", sectors);
	if (fatsetnumsectors(f, sectors))
		printf("cannot change number of sectors\n");
	printf("sectors: %d\n", fatgetnumsectors(f));

			/* finalize */

	fatcopyboottobackup(f);
	fatclusternumfree(f);
	fatclose(f);

	return 0;
}

