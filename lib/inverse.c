/*
 * inverse.c
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "fs.h"
#include "table.h"
#include "entry.h"
#include "directory.h"
#include "reference.h"
#include "inverse.h"

int fatinversedebug = 0;
#define dprintf if (fatinversedebug) printf

/*
 * set an entry in an inverse fat
 */

void fatinverseclear(fatinverse *rev, int32_t cluster) {
	if (cluster < 0)
		return;
	rev[cluster].directory = NULL;
	rev[cluster].index = 0;
	rev[cluster].previous = FAT_UNUSED;
	rev[cluster].isdir = 0;
}

int fatinverseisvoid(fatinverse *rev, int32_t cluster) {
	if (cluster < 0)
		return 1;
	return fatreferenceisvoid(rev[cluster].directory,
			rev[cluster].index,
			rev[cluster].previous);
}

int32_t fatinverseset(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous,
		int isdir) {
	int32_t target;

	target = fatreferencegettarget(f, directory, index, previous);
	if (target < FAT_ROOT)
		return FAT_ERR;

	rev[target].directory = directory;
	rev[target].index = index;
	rev[target].previous = previous;
	if (isdir != -1)
		rev[target].isdir = isdir;

	return target;
}

/*
 * create an empty inverse fat
 */
fatinverse *fatinverseempty(fat *f, int file) {
	fatinverse *rev;
	char filename[] = "/tmp/inversefat-XXXXXX";
	char c = '\0';
	int fd;
	int size;

	size = sizeof(fatinverse) * (fatlastcluster(f) + 2);

	if (! file)
		rev = malloc(size);
	else {
		fd = mkstemp(filename);
		if (fd == -1) {
			perror("mkstemp");
			return NULL;
		}

		if ((off_t) -1 == lseek(fd, size - 1, SEEK_SET)) {
			perror("inverse FAT creation, lseek");
			return NULL;
		}
		if (1 > write(fd, &c, 1)) {
			perror("inverse FAT creation, write");
			return NULL;
		}

		rev = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			fd, 0);
	}

	if (rev == NULL) {
		dprintf("not enough memory/disk space for an inverse FAT ");
		dprintf("array for %d clusters\n", fatlastcluster(f) + 1);
		return NULL;
	}

	rev[0].directory = file ? (unit *) strdup(filename) : NULL;
	rev[0].index = size;
	rev[0].previous = file;

	return rev;
}

/*
 * delete an inverse fat
 */
int fatinversedelete(fat *f, fatinverse *rev) {
	int32_t cl;
	char *filename;

	if (rev == NULL)
		return -1;

	for (cl = FAT_FIRST; cl <= fatlastcluster(f); cl++)
		if (rev[cl].directory != NULL &&
				--rev[cl].directory->refer == 0) {
			dprintf("delete %d\n", rev[cl].directory->n);
			fatunitdelete(&f->clusters, rev[cl].directory->n);
		}

	if (! rev[0].previous)
		free(rev);
	else {
		filename = (char *) rev[0].directory;
		munmap(rev, rev[0].index);
		unlink(filename);
		printf("rm %s\n", filename);
		free(filename);
	}
	return 0;
}

/*
 * create an inverse fat
 */

int _fatinversecreate(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	fatinverse *rev;
	int32_t target;
	int isdir;

	if (direction != 0)
		return FAT_REFERENCE_DELETE;

	if (directory != NULL && fatentryisdotfile(directory, index))
		return 0;

	rev = (fatinverse *) user;

	/* avoid following cycles and confluences of clusters */
	target = fatreferencegettarget(f, directory, index, previous);
	if (target >= FAT_FIRST && ! fatinverseisvoid(rev, target))
		return 0;

	isdir = FATEXECUTEISDIR;

	target = fatinverseset(f, rev, directory, index, previous, isdir);

	/* the cluster is now the target of another pointer */
	if (directory != NULL && target != FAT_ERR)
		directory->refer++;

	return FAT_REFERENCE_NORMAL;
}

fatinverse *fatinversecreate(fat *f, int file) {
	fatinverse *rev;
	int res;
	int cl;

	rev = fatinverseempty(f, file);
	if (rev == NULL)
		return NULL;

	for (cl = FAT_ROOT; cl <= fatlastcluster(f); cl++)
		fatinverseclear(rev, cl);

	res = fatreferenceexecute(f, NULL, 0, -1, _fatinversecreate, rev);
	if (res) {
		dprintf("error while filling the inverse FAT\n");
		fatinversedelete(f, rev);
		return NULL;
	}

	return rev;
}

/*
 * inverse fat of all chains of clusters, including the unrecheable ones
 */
fatinverse *fatinversechains(fat *f, int file) {
	fatinverse *rev;
	int32_t cl;

	rev = fatinverseempty(f, file);
	if (rev == NULL)
		return NULL;

	for (cl = FAT_FIRST; cl <= fatlastcluster(f); cl++)
		fatinverseset(f, rev, NULL, 0, cl, 0);

	return rev;
}

/*
 * print a reverse reference
 */
void fatinverseprint(fat *f, fatinverse *rev, int32_t cl) {
	int32_t target;

	printf("%d", cl);
	if (rev[cl].directory == NULL)
		printf(" [- - %d", rev[cl].previous);
	else
		printf(" [%d %d -", rev[cl].directory->n, rev[cl].index);
	printf("%s", rev[cl].isdir ? " dir]" : "]");
	target = fatreferencegettarget(f,
			rev[cl].directory, rev[cl].index, rev[cl].previous);
	if (target == FAT_ERR)
		printf(" -");
	else
		printf(" %d", target);
}

/*
 * check if an updated inverse fat is the same as a recalculated one;
 *
 * if a function changes the cluster structure (link between directory entries
 * and clusters and between clusters), it has to keep the inverse fat updated;
 * this function checks this to aid debugging
 */
int fatinversecheck(fat *f, fatinverse *rev, int file) {
	fatinverse *check;
	int s;
	int32_t cl;

	check = fatinversecreate(f, file);
	if (check == NULL)
		return -1;
	s = memcmp(rev + 1, check + 1, fatlastcluster(f) * sizeof(fatinverse));
	if (! s)
		printf("inverse fat ok\n");
	else {
		printf("ERROR: discrepancy between updated ");
		printf("and recalculated inverse fat\n");

		for (cl = FAT_ROOT; cl <= fatlastcluster(f); cl++) {
			if (! memcmp(&rev[cl], &check[cl], sizeof(fatinverse)))
				continue;
			fatinverseprint(f, rev, cl);
			printf("   ");
			fatinverseprint(f, check, cl);
			printf("\n");
		}
	}

	fatinversedelete(f, check);
	return s;
}

/*
 * set target, move and swap clusters; also update the inverse fat
 */

void fatinversesettarget(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous,
		int32_t new, int isdir) {
	fatreferencesettarget(f, directory, index, previous, new);

	if (isdir == -1) {
		if (fatreferenceisdirectory(directory, index, previous))
			isdir = 1;
		if (directory == NULL && previous >= FAT_ROOT &&
				rev[previous].isdir)
			isdir = 1;
	}

	if (new == FAT_UNUSED)
		fatinverseclear(rev, previous);
	else
		fatinverseset(f, rev, directory, index, previous, isdir);
}

int fatinversemovereference(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous, int isdir,
		int32_t dst, int writeback) {
	int32_t src;
	int res;

	src = fatreferencegettarget(f, directory, index, previous);

	/* fail if the source cluster cannot be read or is unused or the
	   destination is used */
	res = fatclustermove(f, directory, index, previous, dst, writeback);
	if (res)
		return res;

	fatinverseset(f, rev, directory, index, previous, isdir);
	fatinverseset(f, rev, NULL, 0, dst, -1);

	fatinverseclear(rev, src);
	return 0;
}

int fatinversemove(fat *f, fatinverse *rev,
		int32_t src, int32_t dst, int writeback) {
	unit *directory;
	int index;
	int32_t previous;
	int isdir;

	directory = rev[src].directory;
	index =     rev[src].index;
	previous =  rev[src].previous;
	isdir =     rev[src].isdir;

	return fatinversemovereference(f, rev,
			directory, index, previous, isdir,
			dst, writeback);
}

int fatinverseswapreference(fat *f, fatinverse *rev,
		unit *srcdir, int srcindex, int32_t srcprevious, int srcisdir,
		unit *dstdir, int dstindex, int32_t dstprevious, int dstisdir,
		int writeback) {
	int32_t src, dst;
	int res;

	src = fatreferencegettarget(f, srcdir, srcindex, srcprevious);
	dst = fatreferencegettarget(f, dstdir, dstindex, dstprevious);

	/* fail if any of the two clusters is unused or cannot be read */
	res = fatclusterswap(f,
			srcdir, srcindex, srcprevious,
			dstdir, dstindex, dstprevious,
			writeback);
	if (res)
		return res;

	fatinverseset(f, rev, srcdir, srcindex, srcprevious, srcisdir);
	fatinverseset(f, rev, NULL, 0, src, -1);

	fatinverseset(f, rev, dstdir, dstindex, dstprevious, dstisdir);
	fatinverseset(f, rev, NULL, 0, dst, -1);

	return 0;
}

int fatinverseswap(fat *f, fatinverse *rev,
		int32_t src, int32_t dst,
		int writeback) {
	unit *srcdir, *dstdir;
	int srcindex, dstindex;
	int32_t srcprevious, dstprevious;
	int srcisdir, dstisdir;

	srcdir =      rev[src].directory;
	srcindex =    rev[src].index;
	srcprevious = rev[src].previous;
	srcisdir =    rev[src].isdir;

	dstdir =      rev[dst].directory;
	dstindex =    rev[dst].index;
	dstprevious = rev[dst].previous;
	dstisdir =    rev[dst].isdir;

	return fatinverseswapreference(f, rev,
			srcdir, srcindex, srcprevious, srcisdir,
			dstdir, dstindex, dstprevious, dstisdir,
			writeback);
}

/*
 * go upstream from a cluster reference to a directory entry, if any
 */
int fatinversereferencetoentry(fatinverse *rev,
		unit **directory, int *index, int32_t *previous) {

	while (fatreferenceiscluster(*directory, *index, *previous)) {
		*directory =	rev[*previous].directory;
		*index = 	rev[*previous].index;
		*previous =	rev[*previous].previous;
	}

	return *directory == NULL;
}

/*
 * reconstruct the path of the file from a reference
 */
char *fatinversepath(fatinverse *rev,
		unit *directory, int index, int32_t previous) {
	char shortname[13];
	char *path;
	int pathlen, namelen;

	path = NULL;
	pathlen = 0;

	while (! fatreferenceisvoid(directory, index, previous) &&
	       ! fatreferenceisboot(directory, index, previous)) {

		fatinversereferencetoentry(rev, &directory, &index, &previous);

		if (fatreferenceisboot(directory, index, previous))
			shortname[0] = '\0';
		else if (fatreferenceisentry(directory, index, previous))
			fatentrygetshortname(directory, index, shortname);
		else
			return path;

		dprintf("%s\n", shortname[0] == '\0' ? "/" : shortname);

		namelen = strlen(shortname);
		path = realloc(path, pathlen + namelen + 1);
		memmove(path + namelen + 1, path, pathlen);
		path[namelen] = pathlen == 0 ? '\0' : '/';
		memmove(path, shortname, namelen);
		pathlen = pathlen + 1 + namelen;

		previous = directory == NULL ? 0 : directory->n;
		directory = NULL;
		index = 0;
	}

	return path;
}

/*
 * go back one directory entry
 */
int fatinversepreventry(fat *f, fatinverse *rev,
		unit **directory, int *index) {
	int32_t dir;

	if (*index > 0)
		(*index)--;
	else {
		dir = rev[(*directory)->n].previous;
		if (dir < FAT_ROOT)
			return -1;
		*directory = fatclusterread(f, dir);
		if (*directory == NULL)
			return -1;
		*index = (*directory)->size / 32 - 1;
	}

	return 0;
}

/*
 * mark unused all unused clusters
 */
int fatcleanunused(fat *f) {
	fatinverse *rev;
	int32_t cl;
	int count;

	rev = fatinversecreate(f, 0);
	if (rev == NULL) {
		printf("cannot create inverse fat\n");
		return -1;
	}

	count = 0;
	if (fatinversedebug)
		printf("scanning clusters:");

	for (cl = FAT_FIRST; cl <= fatlastcluster(f); cl++)
		if (fatgetnextcluster(f, cl) != FAT_UNUSED &&
		    fatgetnextcluster(f, cl) != FAT_BAD &&
				rev[cl].directory == NULL &&
				rev[cl].previous == 0) {
			fatsetnextcluster(f, cl, FAT_UNUSED);
			count++;
			dprintf(" %d", cl);
		}

	if (count) {
		dprintf("\nmarked free %d unused clusters\n", count);
	}
	else {
		dprintf("\nno unused non-free cluster found\n");
	}

	return count;
}

