/*
 * fatbackup.c
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
 * fatbackup.c
 *
 * make a copy of the essential parts of a filesystem:
 * - the reserved sectors
 * - the file allocation tables
 * - the directory clusters
 *
 * if copying into an image, that file will be sparse if supported
 *
 * example:
 *	fatbackup /dev/sdb1 backup.fat		backup the filesystem
 *	fatbackup backup.fat /dev/sdb1		restore the filesystem
 */

#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <search.h>
#include <llfat.h>

int diffonly = 1;

/*
 * ask the user whether to proceed
 */
int check(char *message) {
	char line[10] = " ";
	char *res;

	printf("%scontinue (y/N)? ", message);

	res = fgets(line, 5, stdin);

	return res != NULL && res[0] != 'y';
}

/*
 * callback to copy all directory clusters to the new filesystem
 */
int copydirectoryclusters(fat *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	fat *dst;
	int cl;
	unit *cluster, *destination, *copy;
	int nfat;

			/* force reading cluster entry from all FATs */

	if (direction == 0 && previous >= FAT_FIRST)
		for (nfat = 1; nfat < fatgetnumfats(f); nfat++)
			fatgetfat(f, nfat, previous);

			/* only directory clusters are called with -2 */

	if (direction != -2)
		return FAT_REFERENCE_NORMAL;

			/* destination filesystem */

	dst = (fat *) user;

			/* read source cluster */

	cl = fatreferencegettarget(f, directory, index, previous);
	if (cl < FAT_ROOT)
		return FAT_REFERENCE_NORMAL;
	cluster = fatclusterread(f, cl);

			/* check if it coincides with destination cluster */

	if (diffonly) {
		destination = fatclusterread(dst, cl);
		if (destination != NULL && 
			cluster->size == destination->size &&
			! memcmp(fatunitgetdata(cluster),
				fatunitgetdata(destination),
				cluster->size))
			return FAT_REFERENCE_NORMAL;
	}

			/* copy cluster to destination filesystem */

	printf(" %d", cl);
	fflush(stdout);
	copy = fatunitcopy(cluster);
	copy->fd = dst->fd;
	fatunitinsert(&dst->clusters, copy, 1);

	return FAT_REFERENCE_NORMAL;
}

/*
 * copy a sector in cache to the new filesystem
 */
void copysector(const void *nodep, const VISIT which, const int depth) {
	static fat *dst;
	unit *o, *d, *c;

	if (depth == -1) {
		dst = (fat *) nodep;
		return;
	}

	if (which != postorder && which != leaf)
		return;

	o = * (unit **) nodep;

	if (diffonly) {
		d = fatunitget(&dst->sectors, 0, o->size, o->n, dst->fd);
		if (d != NULL &&
			o->size == d->size &&
			! memcmp(fatunitgetdata(o), fatunitgetdata(d),
				o->size))
		return;
	}

	c = fatunitcopy(o);
	c->fd = dst->fd;
	fatunitinsert(&dst->sectors, c, 1);

	if (c->n == 0)
		dst->boot = c;
	if (dst->boot != NULL &&
	    (c->n - fatgetreservedsectors(dst)) % fatgetfatsize(dst) == 0)
		printf("  ");
	printf(" %d", c->n);
	fflush(stdout);
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	char *srcname, *dstname;
	fat *src, *dst;
	int overwrite, usedonly;
	int sectors, size, s;
	int nfat;

			/* arguments */

	overwrite = 0;
	usedonly = 0;
	while (argn - 1 >= 1 && argv[1][0] == '-') {
		switch(argv[1][1]) {
		case 'i':
			overwrite = 1;
			break;
		case 'u':
			usedonly = 1;
			break;
		case 'a':
			diffonly = 0;
			break;
		}
		argn--;
		argv++;
	}

	if (argn - 1 < 2) {
		printf("usage:\n\tfatbackup [-i] [-u] [-a] ");
		printf("source destination\n");
		printf("\t\t-i\toverwrite without asking\n");
		printf("\t\t-u\tcopy only sectors of FAT that are used\n");
		printf("\t\t-a\tcopy also sectors and clusters that ");
		printf("already coincide\n");
		exit(1);
	}

	srcname = argv[1];
	dstname = argv[2];

			/* open source file */

	src = fatopen(srcname, 0);
	if (src == NULL) {
		printf("cannot open %s as a FAT filesystem\n", srcname);
		exit(1);
	}

	if (fatcheck(src)) {
		printf("%s does not appear a FAT filesystem\n", srcname);
		exit(1);
	}

			/* open destination file */

	dst = fatcreate();

	dst->fd = open(dstname, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (dst->fd != -1)
		diffonly = 0;
	else if (errno == EEXIST) {
		if (! overwrite && check("WARNING, file exists\n")) {
			printf("aborted\n");
			fatclose(src);
			exit(0);
		}
		close(dst->fd);
		dst->fd = open(dstname,
			O_CREAT | diffonly ? O_RDWR : O_WRONLY, 0666);
	}
	if (dst->fd == -1) {
		perror(dstname);
		fatquit(src);
		exit(1);
	}

			/* copy directory clusters */

	size = fatgetbytespersector(src);
	dst->boot = fatunitget(&src->sectors, 0, size, 0, src->fd);
	printf("copying clusters:");
	fflush(stdout);
	fatreferenceexecute(src, NULL, 0, -1, copydirectoryclusters, dst);
	printf("\n");

			/* read the reserved sectors and possibly the FATs */

	printf("copying sectors:");
	fflush(stdout);
	sectors = fatgetreservedsectors(src);
	sectors += usedonly ? 0 : fatgetfatsize(src) * fatgetnumfats(src);
	size = fatgetbytespersector(src);
	for (s = 0; s < sectors; s++)
		fatunitget(&src->sectors, 0, size, s, src->fd);

	for (nfat = 0; nfat < fatgetnumfats(src); nfat++)
		fatgetfat(src, nfat, 0);

			/* copy sectors */

	copysector(dst, 0, -1);
	twalk(src->sectors, copysector);
	printf("\n");

			/* close */

	// fatunitdebug = 1;
	fatclose(dst);
	fatquit(src);

	return 0;
}

