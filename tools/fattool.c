/*
 * fattool.c
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
 * fattool.c
 *
 * various operations on a FAT12/16/32 filesystem
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <wctype.h>
#include <malloc.h>
#include <linux/fs.h>
#include <llfat.h>

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

/*
 * use longname or not
 */
int useshortnames;

/*
 * check and convert paths or not
 */
int nostoragepaths;

/*
 * ask the used whether to proceed
 */
void check() {
	char line[10] = " ";

	printf("continue (y/N)? ");

	fgets(line, 5, stdin);

	if (line[0] == 'y')
		return;
	printf("not continuing\n");
	exit(1);
}

/*
 * print a long name
 */
void printlongname(char *before, wchar_t *name, char *after) {
	int i;

	printf("%s", before);

	for (i = 0; name[i] != L'\0'; i++)
		if (iswcntrl(name[i]))
			printf("?");
		else
			printf("%lc", (wint_t) name[i]);

	printf("%s", after);
}

/*
 * process an option that is a file (either "directory,index" or "path") and
 * turns it into a cluster reference; in some cases like "cluster:103", the
 * target is filled but the reference is void
 */
int fileoptiontoreference(fat *f, char *option,
	unit **directory, int *index, int32_t *previous, int32_t *target) {
	int32_t r, dir;
	char dummy;
	char *path;
	size_t len;
	wchar_t *pathlong, *converted;
	int res;

	r = fatgetrootbegin(f);
	*directory = NULL;
	*index = 0;
	*previous = 0;

	if (option[0] == '\0') {
		*previous = -1;
		*target = fatreferencegettarget(f,
				*directory, *index, *previous);
		return 0;
	}

	if (sscanf(option, "%d,%d%c", &dir, index, &dummy) == 2) {
		if (dir == 0)
			dir = fatgetrootbegin(f);
		*directory = fatclusterread(f, dir);
		*target = fatreferencegettarget(f,
				*directory, *index, *previous);
		return *directory == NULL;
	}

	if (! strcmp(option, "/")) {
		*previous = -1;
		*target = fatgetrootbegin(f);
		return 0;
	}

	if (useshortnames) {
		if (nostoragepaths)
			path = option;
		else {
			if (fatinvalidpath(option) < 0) {
				printf("invalid path: %s\n", option);
				return -1;
			}
			path = fatstoragepath(option);
		}

		res = fatlookuppath(f, r, path, directory, index);
		*target = res ? fatlookuppathfirstcluster(f, r, path) :
			fatreferencegettarget(f,
				*directory, *index, *previous);
		if (! nostoragepaths)
			free(path);
		return res && *target == FAT_ERR;
	}

	len = mbstowcs(NULL, option, 0);
	if (len == (size_t) -1)
		return -1;
	pathlong = malloc((len + 1) * sizeof(wchar_t));
	mbstowcs(pathlong, option, len + 1);

	if (nostoragepaths)
		converted = pathlong;
	else {
		if (fatinvalidpathlong(pathlong) < 0) {
			printf("invalid path: %s\n", option);
			free(pathlong);
			return -1;
		}
		converted = fatstoragepathlong(pathlong);
	}

	res = fatlookuppathlong(f, r, converted, directory, index);
	*target = res ? fatlookuppathfirstclusterlong(f, r, converted) :
		fatreferencegettarget(f, *directory, *index, *previous);
	if (! nostoragepaths)
		free(converted);
	return res && *target == FAT_ERR;
}

/*
 * create a file
 */
int createfile(fat *f, int32_t dir, char *path, int dot,
		unit **directory, int *index) {
	size_t len;
	wchar_t *pathlong, *convertedlong;
	char *converted;
	int res;
	
	if (useshortnames) {
		if (nostoragepaths)
			converted = path;
		else {
			res = fatinvalidpath(path);
			if (res < 0 || (res == 1 && ! dot)) {
				printf("invalid short name: %s\n", path);
				return -1;
			}
			converted = fatstoragepath(path);
			printf("filesystem name: |%s|\n", converted);
		}
		if (fatlookuppath(f, dir, converted, directory, index))
			res = fatcreatefile(f, dir, converted,
				directory, index);
		else {
			printf("file exists: ");
			printf("%d,%d\n", (*directory)->n, *index);
			res = -1;
		}
		if (! nostoragepaths)
			free(converted);
		return res;
	}

	len = mbstowcs(NULL, path, 0);
	if (len == (size_t) -1)
		return -1;
	pathlong = malloc((len + 1) * sizeof(wchar_t));
	mbstowcs(pathlong, path, len + 1);

	if (nostoragepaths)
		convertedlong = pathlong;
	else {
		res = fatinvalidpathlong(pathlong);
		if (res < 0 || (res == 1 && ! dot)) {
			printlongname("invalid long name: ", pathlong, "\n");
			return -1;
		}
		convertedlong = fatstoragepathlong(pathlong);
		printlongname("filesystem name: |", convertedlong, "|\n");
	}
	if (! fatlookuppathlong(f, dir, convertedlong, directory, index)) {
		printf("file exists: ");
		printf("%d,%d\n", (*directory)->n, *index);
		free(convertedlong);
		return -1;
	}
	fatlongdebug = 1;
	res = fatcreatefilelongpath(f, dir, convertedlong, directory, index);
	if (! nostoragepaths)
		free(convertedlong);
	return res;
}

/*
 * evaluate position of the boot sector
 */
uint32_t bootposition(char *filename, int sectorsize, unsigned bound) {
	fat *f;
	int estimate, minestimate;
	uint32_t sector, min;

	f = fatcreate();
	f->fd = open(filename, O_RDONLY);
	if (f->fd == -1) {
		perror(filename);
		exit(1);
	}

	minestimate = -10000;
	min = 0;

	for (sector = 0; sector < bound; sector++) {
		f->boot = fatunitget(&f->sectors, 0, sectorsize, sector,
				f->fd);
		if (f->boot == NULL)
			break;

		estimate = fatcheck(f);
		if (estimate > minestimate) {
			minestimate = estimate;
			min = sector;
		}

		printf("sector %u: penalty %d\n", sector, estimate);

		fatunitdelete(&f->sectors, f->boot->n);
	}

	fatclose(f);

	return min;
}

/*
 * map of the free clusters
 */
void fatmap(fat *f, char *used, char *unused, char *bad) {
	int32_t cl, next;
	char buf[50], other[50], another[50];
	int len, pos;

	pos = 0;

	for (cl = FAT_FIRST; cl <= fatlastcluster(f); cl++) {
		next = fatgetnextcluster(f, cl);
		if (next == FAT_UNUSED) {
			sprintf(buf, used, cl);
			sprintf(other, unused, cl);
			sprintf(another, bad, cl);
		}
		else if (next == FAT_BAD) {
			sprintf(buf, bad, cl);
			sprintf(other, used, cl);
			sprintf(another, unused, cl);
		}
		else {
			sprintf(buf, unused, cl);
			sprintf(other, used, cl);
			sprintf(another, bad, cl);
		}
		len = MAX(MAX(strlen(buf), strlen(other)), strlen(another));
		printf("%*s ", len, buf);

		pos += len + 1;
		if (pos > 70) {
			printf("\n");
			pos = 0;
		}
	}

	printf("\n");
}

/*
 * restore the filesystem to its pristine state
 */
void fatzero(fat *f) {
	int nfat;
	unit *root;
	int index;

	fatsetdirtybits(f, 0);

	for (nfat = 0; nfat < fatgetnumfats(f); nfat++)
		fatinittable(f, nfat);

	root = fatclusterread(f, fatgetrootbegin(f));
	for (index = 0; index < root->size / 32; index++)
		fatentryzero(root, index);
}

/*
 * create an hard link
 */
int fatlink(fat *f, char *target, char *new) {
	unit *targetdir, *newdir;
	int targetind, newind;
	int32_t targetprev, targetfirst;
	uint32_t size;
	uint8_t attributes;

	// fatdirectorydebug = 1;

	if (fileoptiontoreference(f, target,
			&targetdir, &targetind, &targetprev, &targetfirst)) {
		printf("cannot determine location of source file\n");
		return -1;
	}
	printf("source: ");
	fatreferenceprint(targetdir, targetind, targetprev);
	printf(" cluster %d\n", targetfirst);

	if (createfile(f, fatgetrootbegin(f), new, 1, &newdir, &newind)) {
		printf("cannot create destination file\n");
		return -1;
	}
	printf("\ndestination: %d,%d\n", newdir->n, newind);

	fatentrysetfirstcluster(newdir, newind, f->bits, targetfirst);

	if (fatreferenceisvoid(targetdir, targetind, targetprev)) {
		size = fatbytespercluster(f) *
			fatcountclusters(f, NULL, 0, targetfirst, 0);
		attributes = 0x20;
	}
	else if (fatreferenceisboot(targetdir, targetind, targetprev)) {
		size = 0;
		attributes = 0x10;
	}
	else {
		size = fatentrygetsize(targetdir, targetind);
		attributes = fatentrygetattributes(targetdir, targetind);
	}

	printf("size: %d\t\tattributes: 0x%02X\n", size, attributes);
	fatentrysetsize(newdir, newind, size);
	fatentrysetattributes(newdir, newind, attributes);
	return 0;
}

/*
 * write a cluster with content coming from stdin
 */
void fatclusterwrite(fat *f, int32_t cl, int part, int readbefore) {
	uint64_t origin;
	int size;
	unit *cluster;

	if (cl < FAT_ROOT || cl > fatlastcluster(f)) {
		printf("invalid cluster: %d\n", cl);
		exit(1);
	}

	fatclusterposition(f, cl, &origin, &size);

	if (readbefore)
		cluster = fatclusterread(f, cl);
	else {
		cluster = fatclustercreate(f, cl);
		if (cluster == NULL)
			cluster = fatclusterread(f, cl);
	}
	if (cluster == NULL) {
		printf("cannot read/create cluster %d\n", cl);
		return;
	}

	if (read(0, fatunitgetdata(cluster), size) != size &&
			! part && ! readbefore) {
		printf("writing less than a whole cluster is disallowed\n");
		printf("enable with option \"part\"\n");
		exit(1);
	}

	cluster->dirty = 1;
	if (fatunitwriteback(cluster))
		printf("cannot write cluster %d\n", cl);

	fatunitdelete(&f->clusters, cl);
}

/*
 * prepare an image for sparsification by filling unused cluster with zero
 */
void fatsparse(fat *f, int readfirst) {
	int32_t cl;
	unit *cluster;
	uint64_t origin;
	int size;
	int i;

	printf("zeroed clusters:");

	for (cl = FAT_FIRST; cl <= fatlastcluster(f); cl++) {
		if (fatgetnextcluster(f, cl) != FAT_UNUSED)
			continue;

		if (! readfirst) {
			printf(" %d", cl);
			fatclusterposition(f, cl, &origin, &size);
			cluster = fatunitcreate(size);
			cluster->fd = f->fd;
			cluster->n = cl;
			cluster->origin = origin;
			cluster->refer = 0;
			memset(fatunitgetdata(cluster), 0, size);
			cluster->dirty = 1;
			fatunitwriteback(cluster);
		}
		else {
			cluster = fatclusterread(f, cl);
			size = cluster->size;
			for (i = 0; i < cluster->size; i++)
				if (fatunitgetdata(cluster)[i] != 0) {
					printf(" %d", cl);
					memset(fatunitgetdata(cluster),
						0, size);
					cluster->dirty = 1;
					fatunitwriteback(cluster);
					break;
				}
		}
	}
	printf("\n");
}

/*
 * print a cluster position and use
 */

void printcluster(fat *f, int32_t cl, fatinverse *rev, int run) {
	char *buf;
	uint64_t origin, offset, sector;
	int size;
	unit *directory;
	int index;
	int32_t previous;
	char *path;
	wchar_t *longpath;

	fatclusterposition(f, cl, &origin, &size);
	if (size == 0) {
		printf("no such cluster: %d\n", cl);
		exit(1);
	}

	offset = origin + cl * size;
	sector = offset / fatgetbytespersector(f);

	printf("cluster %d:", cl);
	printf(" offset %" PRIu64, offset);
	printf(" offset 0x%" PRIx64, offset);
	printf(" sectors %" PRIu64, sector);
	printf("-%" PRIu64, sector + (size - 1) / fatgetbytespersector(f));
	printf(" size %d\n", size);

	if (rev) {
		directory = NULL;
		index = 0;
		previous = cl;

		if (fatinversereferencetoentry(rev, &directory,
				&index, &previous))
			if (previous == -1) {
				printf("/\n");
				fatdump(f, directory, index, previous, 0, 0);
			}
			else
				printf("not in a file\n");
		else {
			if (useshortnames) {
				path = fatinversepath(rev,
					directory, index, previous);
				printf("%s\n", path);
				free(path);
			}
			else {
				longpath = fatinversepathlong(f, rev,
					directory, index, previous);
				printf("%ls\n", longpath);
				free(longpath);
			}

			fatdump(f, directory, index, previous, 0, 0);
		}
	}

	printf("hexdump -C -s %" PRIu64 " -n %d %s\n",
		f->offset + origin + cl * size, size, f->devicename);
	buf = malloc(100 + strlen(f->devicename));
	sprintf(buf, "bvi -s 0x%" PRIx64 " -n %d %s",
		f->offset + origin + cl * size, size, f->devicename);
	puts(buf);

	if (run)
		system(buf);
	free(buf);
}

int _dumpclusters(fat *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		wchar_t *name,
		int __attribute__((unused)) err,
		unit __attribute__((unused)) *longdirectory,
		int __attribute__((unused)) longindex,
		int direction, void *user) {
	if (direction != 0)
		return 0;

	if (fatreferenceisboot(directory, index, previous))
		printf("/\n");
	else if (fatreferenceisentry(directory, index, previous))
		printf("%ls\n", name);
	else
		printcluster(f, previous, NULL, 0);

	if (fatreferenceisdotfile(directory, index, previous))
		return 0;
	return FAT_REFERENCE_COND(* (int *) user);
}

int dumpclusters(fat *f, unit *directory, int index, int32_t previous,
		int recur) {
	return fatreferenceexecutelong(f, directory, index, previous,
		_dumpclusters, &recur);
}

/*
 * free directory clusters that only contains deleted entries
 */

struct _directorycleanstruct {
	int changed;
	int testonly;
};

int _directoryclean(fat *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	struct _directorycleanstruct *s;
	int32_t target, next;
	unit *cluster;
	int ind;
	int used;

	if (fatreferenceisentry(directory, index, previous) &&
			! fatreferenceisdirectory(directory, index, previous))
		return FAT_REFERENCE_DELETE;

	if (fatreferenceisdotfile(directory, index, previous))
		return FAT_REFERENCE_DELETE;
	
	if (direction != 0)
		return 0;

	target = fatreferencegettarget(f, directory, index, previous);
	if (target < FAT_FIRST || target == fatgetrootbegin(f))
		return FAT_REFERENCE_NORMAL;

	cluster = fatclusterread(f, target);
	if (cluster == NULL)
		return 0;

	used = 0;
	for (ind = 0; ind * 32 < cluster->size; ind++)
		if (fatentryexists(cluster, ind))
			used = 1;
	if (used) {
		printf("directory cluster %d used\n", target);
		return FAT_REFERENCE_NORMAL;
	}

	next = fatgetnextcluster(f, target);

	printf("directory cluster %d unused:\n\t", target);
	fatreferenceprint(directory, index, previous);
	printf("->%d->%d\n\t\tbecomes\n\t", target, next);
	fatreferenceprint(directory, index, previous);
	printf("->%d\n", next);

	s = (struct _directorycleanstruct *) user;
	if (! s->testonly) {
		fatreferencesettarget(f, directory, index, previous, next);
		fatsetnextcluster(f, target, FAT_UNUSED);
		s->changed = 1;
	}

	return FAT_REFERENCE_NORMAL;
}

int directoryclean(fat *f, int testonly) {
	struct _directorycleanstruct s;
	s.changed = 0;
	s.testonly = testonly;
	fatreferenceexecute(f, NULL, 0, -1, _directoryclean, &s);
	return s.changed;
}

/*
 * clean up the last deleted entries in a directory cluster 
 */

int _directorylast(fat *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	unit *scandirectory, *lastdirectory;
	int scanindex, lastindex;

	if (direction != 0)
		return 0;

	if (! fatreferenceiscluster(directory, index, previous)) {
		if (! fatreferenceisdirectory(directory, index, previous))
			return FAT_REFERENCE_DELETE;
		else if (fatreferenceisdotfile(directory, index, previous))
			return FAT_REFERENCE_DELETE;
		else
			return FAT_REFERENCE_NORMAL;
	}

	/* the reference is now guaranteed to be a cluster and not a directory
	 * entry because of the previous conditional block; also, this cluster
	 * cannot belong to a file because we return FAT_REFERENCE_DELETE on
	 * all directory entries of files (and dot/dotdot entries) */

	scandirectory = fatclusterread(f, previous);
	if (scandirectory == NULL) {
		printf("cannot read cluster %d\n", previous);
		return 0;
	}
	lastdirectory = NULL;

	for (scanindex = -1;
	     fatnextentry(f, &scandirectory, &scanindex) == 0; ) {
		if (fatentryexists(scandirectory, scanindex) &&
		    ! fatentryislongpart(scandirectory, scanindex)) {
			lastdirectory = scandirectory;
			lastindex = scanindex;
		}
	}
	
	if (lastdirectory == NULL)
		return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;

	printf("last entry: %d,%d\n", lastdirectory->n, lastindex);
	printf("%s entries:", * (int *) user ? "cleaning" : "would clean");
	while (fatnextentry(f, &lastdirectory, &lastindex) >= 0) {
		printf(" %d,%d", lastdirectory->n, lastindex);
		if (* (int *) user)
			fatentryzero(lastdirectory, lastindex);
	}
	printf("\n");

	/* do not visit the chain, just recur */

	return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;
}

void directorylast(fat *f, int testonly) {
	fatreferenceexecute(f, NULL, 0, -1, _directorylast, &testonly);
}

/*
 * clean case byte (offset 0x0C) in a dot or dotdot entry
 */
void cleandotcase(fat __attribute__((unused)) *f,
		char *path, unit *directory, int index,
		void __attribute__((unused)) *user) {
	if (! fatentryisdotfile(directory, index))
		return;

	if (_unit8uint(directory, 32 * index + 0x0C) != 0) {
		printf("%s: 0x%02X -> 0\n", path,
			_unit8uint(directory, 32 * index + 0x0C));
		_unit8uint(directory, 32 * index + 0x0C) = 0;
		directory->dirty = 1;
	}
}

/*
 * try to detect which clusters are used for directories
 * this is done by an heuristic evaluation of each cluster
 */

#define ENTRYPOS(directory, index, pos)			\
	(fatunitgetdata((directory))[(index) * 32 + (pos)])

int clusterscore(unit *cluster) {
	int entry, pos;
	int factor = 1024;
	int score = 0, incr;
	unsigned char attr, c;

	for (entry = 0; entry < cluster->size / 32; entry++) {
		incr = -5;

		if (ENTRYPOS(cluster, entry, 0) == 0x00) {
			factor /= 2;
			continue;
		}

		attr = ENTRYPOS(cluster, entry, 11);

		if (attr == 0x10 || attr == 0x20) {

			/* make a backup of this before changing something */

			incr = 0;
			for (pos = 0; pos < 11; pos++) {
				c = ENTRYPOS(cluster, entry, pos);
				if (isupper(c) || isdigit(c) || c == ' ')
					incr += 1;
				else if (memchr("_-%~", c, 11))
					incr += 1;
				else if (c == 0xE5 && pos == 0)
					incr += 10;
				else if (c == '.' && pos < 2 && entry < 2)
					incr += 10;
				else
					incr += -50;
			}
			if (! memcmp(& ENTRYPOS(cluster, entry, 8), "TXT", 3))
				incr += 5;
			else
			if (! memcmp(& ENTRYPOS(cluster, entry, 8), "PDF", 3))
				incr += 5;
			else
			if (! memcmp(& ENTRYPOS(cluster, entry, 8), "JPG", 3))
				incr += 5;
		}
		else if ((attr & FAT_ATTR_ALL) == FAT_ATTR_LONGNAME) {

			/* make a backup of this before changing something */

			incr = 10;
			if ((ENTRYPOS(cluster, entry, 0) & 0x3F) < 8)
				incr += 10;
			if (ENTRYPOS(cluster, entry, 0x1C) != 0)
				incr -= 5;
			if (ENTRYPOS(cluster, entry, 0x1C + 1) != 0)
				incr -= 5;
		}
		else
			incr = -5;

		// printf("%d: %d\n", entry, incr);
		score += incr * factor;
	}

	return score;
}

void directoryclusters(fat *f) {
	int32_t cl;
	unit *cluster;
	int score;

	printf("possible directory clusters:");

	for (cl = FAT_FIRST; cl <= fatlastcluster(f); cl++) {
		cluster = fatclusterread(f, cl);
		score = clusterscore(cluster);
		if (0 || score > 0) {
			printf(" %d", cl);
			// printf("(%d)", score);
			fflush(stdout);
		}
	}
	printf("\n");
}

/*
 * create a filesystem
 */

int fatsetentries(fat *f, int maxentries) {
	if (maxentries == 0) {
		maxentries = 256;
		if (maxentries * 32 < fatbytespercluster(f))
			maxentries = fatbytespercluster(f) / 32;
	}

	return fatsetrootentries(f, maxentries);
}

int fatsetsize(fat *f) {
	int nsectors;

	fatsetreservedsectors(f, fatbits(f) == 32 ? 32 : 1);
	fatsetnumfats(f, 2);
	fatsetfatsize(f, 1); /* overestimate a number of data clusters */
	nsectors = 2 + fatnumdataclusters(f);
	nsectors += fatbits(f) == 12 ? 1 : 0;
	nsectors = nsectors * fatbits(f) / 8;
	nsectors += fatgetbytespersector(f) - 1;
	nsectors /= fatgetbytespersector(f);
	fatsetfatsize(f, nsectors);

	if (fatnumdataclusters(f) <= 0)
		return -1;

	return (uint32_t) fatgetreservedsectors(f) +
		fatgetfatsize(f) * fatgetnumfats(f) +
		fatgetrootentries(f) * 32 / fatgetbytespersector(f) +
		fatgetsectorspercluster(f) * (fatbits(f) == 32 ? 2 : 1)
			> fatgetnumsectors(f);
}

void toosmall(fat *f) {
	uint32_t nsectors;
	int fatsize;

	printf("too small: ");

	nsectors = 0;
	printf("%d", fatgetreservedsectors(f));
	nsectors += fatgetreservedsectors(f);
	if (nsectors > fatgetnumsectors(f))
		goto toosmallend;

	fatsize = fatnumdataclusters(f) < 0 ? 1 : fatgetfatsize(f);
	printf(" + %d*%d", fatsize, fatgetnumfats(f));
	nsectors += fatsize * fatgetnumfats(f);
	if (nsectors > fatgetnumsectors(f))
		goto toosmallend;

	printf(" + %d*32/%d", fatgetrootentries(f), fatgetbytespersector(f));
	nsectors += fatgetrootentries(f) * 32 / fatgetbytespersector(f);
	if (nsectors > fatgetnumsectors(f))
		goto toosmallend;

	printf(" + ");
	printf("%d", fatgetsectorspercluster(f) * (fatbits(f) == 32 ? 2 : 1));
	nsectors += fatgetsectorspercluster(f) * (fatbits(f) == 32 ? 2 : 1);
	if (nsectors > fatgetnumsectors(f))
		goto toosmallend;

	printf("error in %s: size is not too small\n", __func__);
	return;

toosmallend:
	printf(" > %d\n", fatgetnumsectors(f));
}

int fatformat(char *devicename, off_t offset,
		char *option1, char *option2, char *option3) {
	int fd;
	fat *f;
	int sectorsize = 512;
	unsigned long int ul;
	uint32_t sectors, sectpercl;
	unsigned maxentries;
	struct stat ss;

	errno = 0;
	ul = strtoul(option1, NULL, 10);
	if (ul == ULONG_MAX && errno == ERANGE) {
		printf("overflow: %s\n", option1);
		return -1;
	}
	sectors = ul;

	errno = 0;
	ul = strtoul(option2, NULL, 10);
	if (ul == ULONG_MAX && errno == ERANGE) {
		printf("overflow: %s\n", option2);
		return -1;
	}
	sectpercl = ul;

	errno = 0;
	ul = strtoul(option3, NULL, 10);
	if (ul == ULONG_MAX && errno == ERANGE) {
		printf("overflow: %s\n", option3);
		return -1;
	}
	maxentries = ul;

	if (option1[0] == '\0' || sectors == 0) {
		if (stat(devicename, &ss)) {
			perror(devicename);
			return -1;
		}
		if (S_ISREG(ss.st_mode))
			sectors = ss.st_size / sectorsize;
		else if (S_ISBLK(ss.st_mode)) {
			fd = open(devicename, O_RDONLY);
			ioctl(fd, BLKGETSIZE, &sectors);
			close(fd);
		}
		else {
			printf("unsupported file type\n");
			return -1;
		}
		sectors -= (offset + sectorsize - 1) / sectorsize;
	}
	printf("sectors: %d\n", sectors);

	f = fatcreate();
	f->devicename = devicename;
	f->offset = offset;
	f->boot = fatunitcreate(sectorsize);
	f->boot->n = 0;
	f->boot->origin = offset;
	f->boot->dirty = 1;
	fatunitinsert(&f->sectors, f->boot, 1);

	fatsetnumsectors(f, sectors);
	fatsetbytespersector(f, sectorsize);

	if (option2[0] == '\0' || sectpercl == 0) {
		printf("secXcl\troot\ttype\n");
		for (sectpercl = 1; sectpercl < 256; sectpercl <<= 1) {
			f->bits = 0;
			fatsetsectorspercluster(f, sectpercl);
			printf("%d\t", sectpercl);
			if (fatsetentries(f, maxentries)) {
				printf("invalid number of entries ");
				printf("in root directory: %d\n", maxentries);
				continue;
			}
			printf("%d\t", fatgetrootentries(f));
			if (fatsetsize(f))
				toosmall(f);
			else
				printf("FAT%d\n", fatbits(f));
		}
		return 0;
	}

	f->bits = 0;
	if (fatsetsectorspercluster(f, sectpercl)) {
		printf("invalid sectors per cluster: %d\n", sectpercl);
		return -1;
	}

	if (fatsetentries(f, maxentries)) {
		printf("invalid number of entries in root: %d\n", maxentries);
		return -1;
	}

	if (fatsetsize(f)) {
		toosmall(f);
		return -1;
	}

	fatsummary(f);

	f->fd = open(devicename, O_RDWR, 0666);
	if (f->fd != -1) {
		printf("WARNING: file exists; ");
		check();
	}
	else {
		f->fd = open(devicename, O_CREAT | O_RDWR, 0666);
		if (f->fd == -1) {
			perror(devicename);
			return -1;
		}
	}
	ftruncate(f->fd,
		f->offset + fatgetnumsectors(f) * fatgetbytespersector(f));
	f->boot->fd = f->fd;

	fatsetmedia(f, 0xF8);
	fatzero(f);
	fatclose(f);

	return 0;
}

/*
 * usage
 */
void usage() {
	printf("usage:\n\tfattool [-f num] [-l] [-s] [-n] ");
	printf("[-m] [-c] [-o offset] [-e simerr.txt]\n");
	printf("\tdevice operation\n");
	printf("\t\t-f num\t\tuse the specified file allocation table\n");
	printf("\t\t-l\t\tload the first FAT in cache immediately\n");
	printf("\t\t-s\t\tuse shortnames\n");
	printf("\t\t-n\t\tdo not check or convert names\n");
	printf("\t\t-m\t\tmemory check at the end\n");
	printf("\t\t-c\t\tcheck: show cluster cache at the end\n");
	printf("\t\t-o offset\tfilesystem starts at this offset in device\n");
	printf("\t\t-e simerr.txt\tread simulated errors from file\n");
	printf("\n\toperations:\n");
	printf("\t\tsummary\t\tbasic characteristics of the filesystem\n");
	printf("\t\tused\t\tmap of used clusters\n");
	printf("\t\tfree\t\tmap of free clusters\n");
	printf("\t\tmap\t\tmap of all clusters, with\n");
	printf("\t\t\t\tthe free ones bracketed like [21]\n");
	printf("\t\tview [all]\tshow the filesystem\n");
	printf("\t\t\t\tall: include the deleted entries\n");
	printf("\t\tcalls [all]\targuments of calls to ");
	printf("filereferenceexecute()\n");
	printf("\t\t\t\tall: also on deleted entries\n");
	printf("\t\tfixdot\t\tfix the dot and dotdot entries\n");
	printf("\t\tcompact\t\tmove used clusters at the beginning,");
	printf("\n\t\t\t\tin place of free clusters\n");
	printf("\t\tdefragment\torder clusters in the filesystem\n");
	printf("\t\tlast [n]\tset the last known free cluster indicator\n");
	printf("\t\t\t\tdefault: first data cluster in the filesystem\n");
	printf("\t\trecompute\tcalculate the number of free clusters\n");
	printf("\t\tzero\t\treset to empty filesystem\n");
	printf("\t\tunused\t\tmark free all unused clusters\n");
	printf("\t\tdelete\t\tforce deletion of a file or directory\n");
	printf("\t\tlink target new\tcreate an hard link\n");
	printf("\t\tposition (n|sector:s|file:name) [file|bvi|recur]\n");
	printf("\t\t\t\tprint position of cluster n\n");
	printf("\t\t\t\tor cluster that contains sector s\n");
	printf("\t\t\t\twith argument file, print the file it is in\n");
	printf("\t\t\t\twith argument bvi, call bvi to edit the cluster\n");
	printf("\t\tread n\t\tprint the content of cluster n\n");
	printf("\t\thex n\t\tcontent of cluster n in hex format\n");
	printf("\t\twrite n [part] [read]\n");
	printf("\t\t\t\tcopy stdin to cluster n\n");
	printf("\t\t\t\tpart: allow writing less than a whole cluster\n");
	printf("\t\t\t\tread: first read the original cluster\n");
	printf("\t\tgetnext n\tfind the next of cluster n\n");
	printf("\t\tsetnext n m\tset the next of cluster n to be m\n");
	printf("\t\tsparse [noread]\tzero all unused clusters\n");
	printf("\t\t\t\tnoread:\tdo not read and check clusters for \n");
	printf("\t\t\t\t\tbeing already filled with zeros\n");
	printf("\t\tlinear file [recur] [start|min|free|n]\n");
	printf("\t\t\t\tmake the clusters of file consecutive\n");
	printf("\t\tbad n m\t\tmark clusters between n and m as bad\n");
	printf("\t\thole (n m|size l)\n\t\t\t\tsame, but first copy the ");
	printf("used clusters to some\n\t\t\t\tunused ones\n");
	printf("\t\tcutbad\t\tcut clusters chains at one marked bad\n");
	printf("\t\treadfile name [chain]\n");
	printf("\t\t\t\tread content of file to stdout\n");
	printf("\t\t\t\tchain: dump the entire cluster chain\n");
	printf("\t\twritefile name\twrite stdin to file\n");
	printf("\t\tdeletefile name\tdelete a file\n");
	printf("\t\toverwrite name [test]\t\n\t\t\t\toverwrite the ");
	printf("differing clusters of a file\n");
	printf("\t\tdirectoryclean\tclean unused directory clusters\n");
	printf("\t\tcountclusters file\n");
	printf("\t\t\t\tcount clusters used by file or directory\n");
	printf("\t\tfilldeleted directory\n");
	printf("\t\t\t\tfill all unused entries with deleted files\n");
	printf("\t\tgettime file [write|create|read]\n");
	printf("\t\t\t\tread write/create/read date of a file\n");
	printf("\t\tsettime file (write|create|read) (date|now)\n");
	printf("\t\t\t\tset the write/create/read date of a file\n");
	printf("\t\tinverse\t\tcheck whether an inverse FAT can be created\n");
	printf("\t\tdirty [[UNCLEAN][,][IOERROR]|NONE]\n");
	printf("\t\t\t\tcheck, set or unset the dirty bits\n");
	printf("\t\tdotcase\t\tclean case byte in . and ..\n");
	printf("\t\tdir [directory] [start]\n\t\t\t\tlist directory entry\n");
	printf("\t\tdirfind [num]\ttry to locate directory clusters\n");
	printf("\t\tformat (sectors|\"\") (sectorsperclusters|\"\") ");
	printf("(maxentries|\"\")\n");
	printf("\t\t\t\tcreate a filesystem or estimate its type\n");
}

int main(int argn, char *argv[]) {
	char *name, *operation, *option1, *option2, *option3;
	off_t offset;
	size_t wlen;
	wchar_t *longname, *longpath;
	fat *f;
	int fatnum;
	int32_t previous, target, r, cl, next, start, end, last, len;
	unit *directory, *startdirectory, *longdirectory, *cluster;
	int index, startindex, longindex, max, size, pos;
	uint32_t sector, spos;
	int res, finalres, recur, chain, all, over, startdir, nchanges;
	char dummy, *buf;
	int nfat;
	char *timeformat;
	struct tm tm;
	int first, clusterdump, insensitive, memcheck, immediate, testonly;
	fatinverse *rev;
	char *simerrfile;
	int dirty;

	finalres = 0;

				/* arguments */

	offset = 0;
	fatnum = -1;
	first = 0;
	insensitive = 0;
	useshortnames = 0;
	nostoragepaths = 0;
	memcheck = 0;
	clusterdump = 0;
	simerrfile = NULL;
	while (argn - 1 >= 1 && argv[1][0] == '-') {
		switch(argv[1][1]) {
		case 'o':
			if (argv[1][2] != '\0')
				offset = atol(argv[1] + 1);
			else {
				offset = atol(argv[2]);
				argn--;
				argv++;
			}
			break;
		case 'l':
			first = 1;
			break;
		case 'f':
			if (argv[1][2] != '\0')
				fatnum = atoi(argv[1] + 1);
			else {
				fatnum = atoi(argv[2]);
				argn--;
				argv++;
			}
			break;
		case 'e': 
			if (argv[1][2] != '\0')
				simerrfile = &argv[1][2];
			else {
				simerrfile = argv[2];
				argn--;
				argv++;
			}
			break;
		case 'i':
			insensitive = 1;
			break;
		case 's':
			useshortnames = 1;
			break;
		case 'n':
			nostoragepaths = 1;
			break;
		case 'm':
			memcheck = 1;
			break;
		case 'c':
			clusterdump = 1;
			break;
		case 'h':
			usage();
			exit(0);
		default:
			printf("invalid option: %s\n", argv[1]);
			usage();
			exit(1);
		}
		argn--;
		argv++;
	}

				/* arguments */

	if (argn - 1 < 2) {
		usage();
		exit(1);
	}

	name = argv[1];
	operation = argv[2];
	option1 = argn - 1 >= 3 ? argv[3] : "";
	option2 = argn - 1 >= 4 ? argv[4] : "";
	option3 = argn - 1 >= 5 ? argv[5] : "";

				/* find location of the boot sector */

	if (! strcmp(operation, "boot")) {
		size = atoi(option1) ? atoi(option1) : 512;
		max = atoi(option2) ? atoi(option2) : 10000;
		sector = bootposition(name, size, max);
		printf("first better sector: %u\n", sector);
		return 0;
	}

				/* create a file system */

	if (! strcmp(operation, "format"))
		return fatformat(name, offset, option1, option2, option3);

				/* open and check */

	f = fatopen(name, offset);
	if (f == NULL) {
		printf("cannot open %s as a FAT filesystem\n", name);
		exit(1);
	}

	if ((res = fatcheck(f))) {
		printf("%s does not appear a FAT filesystem: ", name);
		printf("score %d\n", res);
		exit(1);
	}

	r = fatgetrootbegin(f);
	last = fatlastcluster(f);

	f->insensitive = insensitive;
	if (fatnum != -1) {
		if (fatnum < 0 || fatnum >= fatgetnumfats(f)) {
			printf("invalid FAT number: %d, ", fatnum);
			printf("not between 0 and %d\n", fatgetnumfats(f) - 1);
			fatquit(f);
			exit(1);
		}
		f->nfat = fatnum;
	}

				/* read first FAT if -f passed */

	if (first) {
		fattabledebug = 1;
		if (fatreadfat(f, 0)) {
			printf("error reading FAT\n");
			exit(1);
		}
		fattabledebug = 0;
	}

				/* simulated errors file */

	if (simerrfile) {
		fatsimulateinit();
		if (fatsimulateread(simerrfile, f->fd)) {
			printf("cannot read %s\n", simerrfile);
			exit(1);
		}
	}

				/* operate */

	if (! strcmp(operation, "summary"))
		fatsummary(f);
	else if (! strcmp(operation, "free"))
		fatmap(f, "%d", "     .", "BAD");
	else if (! strcmp(operation, "used"))
		fatmap(f, "     .", "%d", "BAD");
	else if (! strcmp(operation, "map"))
		fatmap(f, "[%d]", "%6d", "B-%d");
	else if (! strcmp(operation, "view")) {
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found\n");
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous))
			previous = target;
		all = ! strcmp(option2, "all");
		if (useshortnames)
			fatdump(f, directory, index, previous, 1, all);
		else
			fatdumplong(f, directory, index, previous, 1, all);
	}
	else if (! strcmp(operation, "calls"))
		fatcalls(f, ! strcmp(option1, "all"));
	else if (! strcmp(operation, "fixdot")) {
		fatreferencedebug = 1;
		fatfixdot(f);
	}
	else if (! strcmp(operation, "compact")) {
		printf("WARNING: complex operation on filesystem %s\n", name);
		check();
		fatcompact(f);
	}
	else if (! strcmp(operation, "defragment")) {
		testonly = ! memcmp(option1, "test", 4) ||
			   ! memcmp(option1, "check", 5);

		if (! testonly) {
			printf("WARNING: complex operation ");
			printf("on filesystem %s\n", name);
			check();
		}

		fatcomplexdebug = 1;
		if (fatdefragment(f, testonly, &nchanges) ==
		    FATINTERRUPTIBLEIOERROR) {
			printf("operation aborted due to IO error\n");
			printf("check the device for faulty sectors\n");
		}
		else if(nchanges == 0)
			printf("filesystem already linear\n");
		else
			printf("%d changes %s\n", nchanges,
				testonly ? "required" : "done");
	}
	else if (! strcmp(operation, "last")) {
		if (fatbits(f) != 32)
			printf("warning: no effect on FAT%d\n", fatbits(f));

		if (option1[0] == '\0')
			f->last = FAT_FIRST;
		else {
			cl = atol(option1);
			if (cl < FAT_FIRST)
				printf("error: first cluster is %d\n",
					FAT_FIRST);
			else if (cl > last)
				printf("error: last cluster is %d\n", last);
			else
				f->last = cl;
		}
	}
	else if (! strcmp(operation, "recompute"))
		printf("free clusters: %d\n",
			fatclusternumfree(f));
	else if (! strcmp(operation, "header"))
		for (nfat = 0; nfat < fatgetnumfats(f); nfat++)
			fatfixtableheader(f, nfat);
	else if (! strcmp(operation, "zero")) {
		printf("WARNING: this will delete everything ");
		printf("in filesystem %s\n", name);
		check();
		printf("WARNING: this operation is irreversible\n");
		printf("control-C in four seconds to stop\n");
		sleep(5);
		check();
		printf("resetting the filesystem\n");
		fatzero(f);
	}
	else if (! strcmp(operation, "unused")) {
		fatinversedebug = 1;
		printf("cleaned up %d unused clusters\n", fatcleanunused(f));
	}
	else if (! strcmp(operation, "delete")) {
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found\n");
			exit(1);
		}
		if (directory == NULL) {
			printf("cannot delete %s\n", option1);
			exit(1);
		}
		fatentrydelete(directory, index);
		printf("filesystem may be unclean, fix with:\n");
		printf("fattool %s unused\n", name);
	}
	else if (! strcmp(operation, "link")) {
		if (option1[0] == '\0')
			printf("source and target of link missing\n");
		else if (option2[0] == '\0')
			printf("link target missing\n");
		else {
			printf("WARNING: the resulting filesystem will be ");
			printf("out-of-spec and may not work properly\n");
			check();
			if (! fatlink(f, option1, option2)) {
				printf("\ndo not delete with the OS\n");
				printf("ONLY delete with:\n\t fattool ");
				printf("%s delete %s\n", name, option2);
			}
		}
	}
	else if (! strcmp(operation, "position")) {
		if (option1[0] == '\0') {
			printf("number of cluster missing\n");
			exit(1);
		}
		if (sscanf(option1, "cluster:%" PRId32 "%c", &cl, &dummy) == 1)
			;
		else if (! memcmp(option1, "file:", 5) && 
		         ! fileoptiontoreference(f, option1 + strlen("file:"),
				&directory, &index, &previous, &target))
			cl = -1;
		else if (sscanf(option1, "sector:%" PRIu32 "%c",
				&sector, &dummy) != 1)
			cl = atoi(option1);
		else {
			printf("sector %" PRIu32 ": ", sector);
			cl = fatsectorposition(f, sector);
			if (cl < FAT_ERR - 1) {
				nfat = -cl + FAT_ERR - 2;
				printf("FAT%d: ", nfat);
				spos = fatgetreservedsectors(f);
				spos += nfat * fatgetfatsize(f);
				printf("%d-", spos);
				spos += fatgetfatsize(f) - 1;
				printf("%d\n", spos);
			}
			else if (cl == FAT_ERR - 1) {
				printf("reserved sectors: ");
				printf("0-%d\n", fatgetreservedsectors(f) - 1);
			}
		}

		if (cl == 0) {
			printf("invalid argument or non existing file: ");
			printf("%s\n", option1);
		}
		else if (cl > FAT_ERR) {
			if (! ! strcmp(option2, "file"))
				rev = NULL;
			else {
				printf("creating inverse fat... ");
				fflush(stdout);
				rev = fatinversecreate(f, 0);
				printf("done\n");
				if (rev == NULL) {
					printf("WARNING: ");
					printf("cannot create inverse FAT\n");
				}
			}
			if (cl == -1)
				dumpclusters(f, directory, index, previous,
					! strcmp(option2, "recur"));
			else
				printcluster(f, cl, rev,
					! strcmp(option2, "bvi"));
			if (rev != NULL)
				fatinversedelete(f, rev);
		}
	}
	else if (! strcmp(operation, "read") || ! strcmp(operation, "hex")) {
		if (option1[0] == '\0') {
			printf("number of cluster missing\n");
			exit(1);
		}
		cl = atol(option1);
		if (cl < FAT_ROOT || cl > fatlastcluster(f)) {
			printf("invalid cluster: %d\n", cl);
			if (cl == 0)
				printf("this is `read', not `readfile'\n");
			exit(1);
		}
		cluster = fatclusterread(f, cl);
		if (cluster == NULL)
			printf("cannot read cluster %d\n", cl);
		else
			fatunitdump(cluster, ! strcmp(operation, "hex"));
	}
	else if (! strcmp(operation, "write")) {
		if (option1[0] == '\0') {
			printf("number of cluster missing\n");
			exit(1);
		}
		cl = atol(option1);
		if (cl < FAT_ROOT || cl > fatlastcluster(f)) {
			printf("invalid cluster: %d\n", cl);
			if (cl == 0)
				printf("this is `write', not `writefile'\n");
			exit(1);
		}
		fatclusterwrite(f, cl,
			! strcmp(option2, "part"),
			! strcmp(option2, "read"));
	}
	else if (! strcmp(operation, "getnext")) {
		if (option1[0] == '\0') {
			printf("number of cluster missing\n");
			exit(1);
		}
		cl = atol(option1);
		if (cl < FAT_FIRST || cl > fatlastcluster(f)) {
			printf("cluster out of bounds: %d is not between", cl);
			printf(" %d and %d\n", FAT_FIRST, fatlastcluster(f));
			exit(1);
		}
		next = fatgetnextcluster(f, cl);
		if (next == FAT_EOF)
			printf("EOF\n");
		else if (next == FAT_UNUSED)
			printf("UNUSED\n");
		else if (next == FAT_BAD)
			printf("BAD\n");
		else
			printf("%d\n", next);
	}
	else if (! strcmp(operation, "setnext")) {
		if (option1[0] == '\0' || option2[0] == '\0') {
			printf("args: cluster next\n");
			exit(1);
		}
		cl = atol(option1);
		if (cl < FAT_FIRST || cl > fatlastcluster(f)) {
			printf("cluster out of bound: %d is not between ", cl);
			printf("%d and %d\n", FAT_FIRST, fatlastcluster(f));
			exit(1);
		}
		if (! strcmp(option2, "UNUSED"))
			next = FAT_UNUSED;
		else if (! strcmp(option2, "EOF"))
			next = FAT_EOF;
		else if (! strcmp(option2, "BAD"))
			next = FAT_BAD;
		else
			next = atol(option2);
		fatsetnextcluster(f, cl, next);
	}
	else if (! strcmp(operation, "getfirst")) {
		if (option1[0] == '\0') {
			printf("missing file name\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target) ||
		    directory == NULL) {
			printf("file not found: %s\n", option1);
			exit(1);
		}
		printf("%d\n", target);
	}
	else if (! strcmp(operation, "setfirst")) {
		if (option1[0] == '\0' || option2[0] == '\0') {
			printf("arguments: filename cluster\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found: %s\n", option1);
			exit(1);
		}
		if (directory == NULL) {
			printf("cannot set first cluster of %s\n", option1);
			exit(1);
		}
		cl = atol(option2);
		fatreferenceprint(directory, index, previous);
		fatreferencesettarget(f, directory, index, previous, cl);
	}
	else if (! strcmp(operation, "sparse")) {
		fatsparse(f, ! ! strcmp(option1, "noread"));
	}
	else if (! strcmp(operation, "linear")) {
		if (option1[0] == '\0') {
			printf("arguments: file ");
			printf("[check|test||start|min|free|n] [recur]\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found: %s\n", option1);
			exit(1);
		}

		recur = ! strcmp(option3, "recur");
		if (! strcmp(option2, "recur")) {
			recur = 1;
			option2 = option3;
		}

		testonly = 0;
		testonly |= ! memcmp(option2, "test", 4);
		testonly |= ! memcmp(option2, "check", 5);

		size = fatreferenceisvoid(directory, index, previous) ?
			fatcountclusters(f, NULL, 0, target, recur) - 1:
			fatcountclusters(f, directory, index, previous, recur);
		printf("number of clusters: %d\n", size);

		if (target == fatgetrootbegin(f))
			start = fatbits(f) == 32 ? target : target + 1;
		else if (fatreferenceisvoid(directory, index, previous))
			start = target + 1;
		else if (! strcmp(option2, "start") || testonly)
			start = target;
		else if (! strcmp(option2, "min")) {
			start = fatclusterlongestlinear(f, target, &max, &pos);
			start -= pos;
		}
		else if (! strcmp(option2, "free"))
			start = fatclustermostfree(f, size, 0, &max);
		else if (option2[0] != '\0')
			start = atol(option2);
		else {
			start = target;
			if (fatclusterareaisbad(f, start, start + size - 1) ||
				start + size - 1 > fatlastcluster(f)) {
				start = fatclustermostfree(f, size, 0, &max);
			}
		}

		printf("start: %d\n", start);

		if (start == FAT_ERR) {
			printf("cannot find a suitable destination area\n");
			exit(1);
		}

		if (start < FAT_ROOT) {
			printf("start=%d, cannot be less than 1\n", start);
			exit(1);
		}

		if (start + size - 1 > fatlastcluster(f)) {
			printf("not enough clusters from %c\n", start);
			exit(1);
		}

		if (fatclusterareaisbad(f, start, start + size - 1)) {
			printf("destination area contains bad clusters\n");
			exit(1);
		}

		if (target == fatgetrootbegin(f))
			previous = -1;
		else if (fatreferenceisvoid(directory, index, previous))
			previous = target;

		// fatcomplexdebug = 1;
		fatlinearize(f, directory, index, previous,
				start, recur, testonly, &nchanges);

		if (nchanges == 0)
			printf("%s is already linear\n", option1);
		else
			printf("%d changes %s\n", nchanges,
				testonly ? "required" : "done");
	}
	else if (! strcmp(operation, "bad")) {
		if (option1[0] == '\0') {
			printf("arguments: begin [end]\n");
			exit(1);
		}

		start = atol(option1);
		if (! fatisvalidcluster(f, start)) {
			printf("invalid starting cluster: %d ", start);
			printf("is not between %d ", FAT_FIRST);
			printf("and %d\n", last);
			exit(1);
		}
		end = option2[0] == '\0' ? start : atol(option2);
		if (! fatisvalidcluster(f, end)) {
			printf("invalid ending cluster: %d ", end);
			printf("is not between %d ", FAT_FIRST);
			printf("and %d\n", last);
			exit(1);
		}

		if (fatclusterfindallocated(f, start, end) != FAT_ERR) {
			printf("WARNING: some files will be deleted or ");
			printf("truncated, and the resulting filesystems ");
			printf("made incorrect\n");
			check();
		}
		for (cl = start; cl <= end; cl++)
			fatsetnextcluster(f, cl, FAT_BAD);
	}
	else if (! strcmp(operation, "hole")) {
		if (option1[0] == '\0' || option2[0] == '\0') {
			printf("arguments: begin end\n");
			exit(1);
		}

		if (! strcmp(option1, "size")) {
			size = atol(option2);
			start = fatclustermostfree(f, size, 0, &len);
			end = start + size -1;
			printf("hole starts at cluster %d\n", start);
		}
		else {
			start = atol(option1);
			if (! fatisvalidcluster(f, start)) {
				printf("invalid starting cluster: %d ", start);
				printf("is not between %d ", FAT_FIRST);
				printf("and %d\n", last);
			}
			end = atol(option2);
			if (! fatisvalidcluster(f, end)) {
				printf("invalid ending cluster: %d ", end);
				printf("is not between %d ", FAT_FIRST);
				printf("and %d\n", last);
			}
		}
		if (end < start)
			printf("warning: wrapping area\n");

		fatmovearea(f, start, end,
			fatclusterintervalnext(f, end, FAT_FIRST, last),
			fatclusterintervalprev(f, start, FAT_FIRST, last));
		if (fatclusterfindallocated(f, start, end) != FAT_ERR) {
			printf("allocated cluster remaining, ");
			printf("hole not created\n");
			exit(1);
		}
		for (cl = start; cl <= end; cl++)
			fatsetnextcluster(f, cl, FAT_BAD);
	}
	else if (! strcmp(operation, "cutbad")) {
		if (fatcutbad(f, 1))
			fatcleanunused(f);
	}
	else if (! strcmp(operation, "readfile")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found: %s\n", option1);
			exit(1);
		}
		chain = fatreferenceisvoid(directory, index, previous) ?
			-1 : ! strcmp(option2, "chain");
		size = chain || directory == NULL ?
			0 : fatentrygetsize(directory, index);
		for (cl = previous > 0 ? previous : target;
		     cl != FAT_EOF && cl != FAT_UNUSED && cl != FAT_BAD &&
		     		(size > 0 || chain);
		     cl = fatgetnextcluster(f, cl)) {
			cluster = fatclusterread(f, cl);
			if (cluster == NULL)
				break;
			write(1, fatunitgetdata(cluster),
				chain || size > cluster->size ?
					cluster->size : size);
			size -= cluster->size;
		}
	}
	else if (! strcmp(operation, "writefile")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (! fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			if (fatreferenceisvoid(directory, index, previous))
				printf("cannot write by cluster\n");
			else  {
				printf("file %s exists, ", option1);
				printf("not overwriting\n");
			}
			exit(1);
		}

		if (createfile(f, r, option1, 0, &directory, &index)) {
			printf("cannot create file %s\n", option1);
			exit(1);
		}

		startdirectory = directory;
		startindex = index;
		cl = 0;
		size = 0;
		max = option2[0] == '\0' ? -1 : atoi(option2);
		printf("max: %d\n", max);

		fatreferencesettarget(f, directory, index, cl, FAT_UNUSED);

		do {
			next = fatclusterfindfree(f);
			printf("next: %d max: %d\r", next, max);
			if (next == FAT_ERR) {
				printf("filesystem full\n");
				exit(1);
			}
			cluster = fatclustercreate(f, next);
			if (cluster == NULL)
				cluster = fatclusterread(f, next);
			if (cluster == NULL) {
				printf("error creating cluster %d\n", next);
				exit(1);
			}

			if (max == -1)
				res = read(0, fatunitgetdata(cluster),
					cluster->size);
			else {
				res = max > cluster->size ?
					cluster->size : max;
				max -= res;
			}


			if (res <= 0) {
				fatunitdelete(&f->clusters, cluster->n);
				break;
			}

			if (max == -1) {
				cluster->dirty = 1;
				fatunitwriteback(cluster);
			}

			fatreferencesettarget(f, directory, index, cl,
				cluster->n);
			fatsetnextcluster(f, cluster->n, FAT_EOF);
			fatunitdelete(&f->clusters, cluster->n);

			directory = NULL;
			index = 0;
			cl = next;
			size += res;
		} while (res == cluster->size);

		fatentrysetsize(startdirectory, startindex, size);
		fatentrysetattributes(startdirectory, startindex, 0x20);
		printf("\n");
	}
	else if (! strcmp(operation, "deletefile")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot delete a file by cluster\n");
			exit(1);
		}
		if (fatreferenceisdirectory(directory, index, previous)) {
			printf("%s is a directory\n", option1);
			exit(1);
		}

		next = fatentrygetfirstcluster(directory, index, fatbits(f));
		for(cl = next; cl != FAT_EOF; cl = next) {
			next = fatgetnextcluster(f, cl);
			fatsetnextcluster(f, cl, FAT_UNUSED);
		}
		fatentrydelete(directory, index);
	}
	else if (! strcmp(operation, "overwrite")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisdirectory(directory, index, previous)) {
			printf("%s is a directory\n", option1);
			exit(1);
		}

		immediate = 0;
		if (! strcmp(option2, "immediate"))
			immediate = 1;

		testonly = 0;
		if (! strcmp(option2, "test"))
			testonly = 1;

		cl = target;
		cluster = cl < FAT_FIRST ? NULL : fatclusterread(f, cl);
		size = cluster == NULL ? 0 : cluster->size;
		buf = malloc(size);
		printf(testonly ? "differing clusters:" : "writing clusters:");

		while (cluster && 0 < (res = read(0, buf, cluster->size))) {
			if (memcmp(fatunitgetdata(cluster), buf, res)) {
				printf(" %d", cl);
				memcpy(fatunitgetdata(cluster), buf, res);
				if (! testonly)
					cluster->dirty = 1;
				if (immediate)
					fatunitwriteback(cluster);
			}

			fatunitdelete(&f->clusters, cl);
			cl = fatgetnextcluster(f, cl);
			cluster = cl < FAT_FIRST ?
				NULL : fatclusterread(f, cl);
			if (cluster != NULL && cluster->size > size) {
				size = cluster->size;
				buf = realloc(buf, size);
			}
		}

		printf("\n");
		free(buf);
	}
	else if (! strcmp(operation, "getsize")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot determine size by cluster\n");
			exit(1);
		}
		if (fatreferenceisdirectory(directory, index, previous)) {
			printf("%s is a directory\n", option1);
			exit(1);
		}

		printf("%u\n", fatentrygetsize(directory, index));
	}
	else if (! strcmp(operation, "setsize")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot set size by cluster\n");
			exit(1);
		}
		if (fatreferenceisdirectory(directory, index, previous)) {
			printf("%s is a directory\n", option1);
			exit(1);
		}

		if (option2[0] == '\0')
			printf("missing argument: new size\n");
		else
			fatentrysetsize(directory, index, atoi(option2));
	}
	else if (! strcmp(operation, "directoryclean")) {
		directoryclean(f, strcmp(option1, "test"));
		directorylast(f, strcmp(option1, "test"));
	}
	else if (! strcmp(operation, "countclusters")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		recur = ! strcmp(option2, "recur");
		size = fatreferenceisvoid(directory, index, previous) ?
			fatcountclusters(f, NULL, 0, target, recur) :
			fatcountclusters(f, directory, index, previous, recur);
		printf("%d\n", size);
	}
	else if (! strcmp(operation, "filldeleted")) {
		if (option1[0] == '\0') {
			printf("missing argument: directory\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot check whether %s ", option1);
			printf("is a directory\n");
			check();
		}
		else if (!
			fatreferenceisdirectory(directory, index, previous)) {
			printf("this operation can only be done on ");
			printf("directories, and %s ", option1);
			printf("is a regular file\n");
			exit(1);
		}

		cl = target;
		if (cl < FAT_ROOT) {
			printf("%s does not have any cluster\n", option1);
			exit(1);
		}
		cluster = fatclusterread(f, cl);
		if (cluster == NULL) {
			printf("cannot read first cluster of %s\n", option1);
			exit(1);
		}

		for (index = -1; fatnextentry(f, &cluster, &index) >= 0; )
			if (fatentryend(cluster, index))
				fatentrydelete(cluster, index);
	}
	else if (! strcmp(operation, "gettime")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot determine time by cluster\n");
			exit(1);
		}

		if (! strcmp(option2, "write") || option2[0] == '\0') 
			fatentrygetwritetime(directory, index, &tm);
		else if (! strcmp(option2, "create"))
			fatentrygetcreatetime(directory, index, &tm);
		else if (! strcmp(option2, "read"))
			fatentrygetreadtime(directory, index, &tm);
		printf("%s", asctime(&tm));
	}
	else if (! strcmp(operation, "settime")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot set time by cluster\n");
			exit(1);
		}

		if (! strcmp(option3, "now")) {
			if (! strcmp(option2, "write"))
				fatentrysetwritetimenow(directory, index);
			else if (! strcmp(option2, "create"))
				fatentrysetcreatetimenow(directory, index);
			else if (! strcmp(option2, "read"))
				fatentrysetreadtimenow(directory, index);
		}
		else {
			timeformat = "%Y-%m-%d %H:%M";
			bzero(&tm, sizeof(struct tm));
			if (NULL == strptime(option3, timeformat, &tm)) {
				printf("cannot parse time argument: ");
				printf("time format is \"%s\"\n", timeformat);
				exit(1);
			}
			if (! strcmp(option2, "write"))
				fatentrysetwritetime(directory, index, &tm);
			else if (! strcmp(option2, "create"))
				fatentrysetcreatetime(directory, index, &tm);
			else if (! strcmp(option2, "read"))
				fatentrysetreadtime(directory, index, &tm);
		}
	}
	else if (! strcmp(operation, "inverse")) {
		rev = fatinversecreate(f, 0);
		if (rev != NULL)
			printf("inverse fat created\n");
		else
			printf("error in creating the inverse fat\n");
		fatinversedelete(f, rev);
	}
	else if (! strcmp(operation, "dirty")) {
		if (option1[0] == '\0') {
			printf("dirty bits:");
			if (fatgetdirtybits(f) & FAT_UNCLEAN)
				printf(" UNCLEAN");
			if (fatgetdirtybits(f) & FAT_IOERROR)
				printf(" IOERROR");
			printf("\n");
		}
		else {
			dirty = 0;
			dirty |= strstr(option1, "UNCLEAN") ? FAT_UNCLEAN : 0;
			dirty |= strstr(option1, "IOERROR") ? FAT_IOERROR : 0;
			fatsetdirtybits(f, dirty);
		}
	}
	else if (! strcmp(operation, "dirtyfat")) {
		if (fatbits(f) == 12) {
			printf("no dirty bits in a FAT for FAT12\n");
			exit(1);
		}

		if (option1[0] == '\0') {
			printf("dirty bits:");
			if (fatgetfatdirtybits(f, 0) & FAT_UNCLEAN)
				printf(" UNCLEAN");
			if (fatgetfatdirtybits(f, 0) & FAT_IOERROR)
				printf(" IOERROR");
			printf("\n");
		}
		else {
			dirty = 0;
			dirty |= strstr(option1, "UNCLEAN") ? FAT_UNCLEAN : 0;
			dirty |= strstr(option1, "IOERROR") ? FAT_IOERROR : 0;
			for (nfat = 0; nfat < fatgetnumfats(f); nfat++)
				fatsetfatdirtybits(f, nfat, dirty);
		}
	}
	else if (! strcmp(operation, "dotcase"))
		fatfileexecute(f, NULL, 0, -1, cleandotcase, NULL);
	else if (! strcmp(operation, "dir")) {
		if (option1[0] == '\0')
			target = fatgetrootbegin(f);
		else if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		else if (directory != NULL && 
			! fatentryisdirectory(directory, index)) {
			printf("file %s is not a directory\n", option1);
			exit(1);
		}

		if (target == 0)
			target = fatgetrootbegin(f);
		directory = fatclusterread(f, target);
		if (directory == NULL) {
			printf("cannot read cluster %d\n", target);
			exit(1);
		}

		if (useshortnames) {
			over = ! strcmp(option2, "over");
			all = over || ! strcmp(option2, "all");
			for (index = 0, res = 0;
			     res == 0 || (over && res == 1);
			     res = fatnextentry(f, &directory, &index)) {
				if (! fatentryexists(directory, index) &&
				    ! all)
					continue;
				fatentryprintpos(directory, index, 10);
				fatentryprint(directory, index);
				cl = fatentrygetfirstcluster(directory, index,
					fatbits(f));
				if (cl != FAT_UNUSED && cl != FAT_EOF)
					printf("\t%d", cl);
				printf("\n");
			}
		}
		else {
			startdir = ! strcmp(option2, "start");
			if (! startdir) {
				res = 0;
				longdirectory = NULL;
				longindex = 0;
			}

			for (index = 0;
			     ! startdir ? 
			     ! fatnextname(f, &directory, &index, &longname) :
			     (res = fatlongnext(f, &directory, &index,
				&longdirectory, &longindex, &longname))
			     	!= FAT_END;
			     fatnextentry(f, &directory, &index)) {
				printf("%d,%d", directory->n, index);
				printf("\r\t\t");
				fatentryprint(directory, index);
				printf(" % 8d ",
					fatentrygetfirstcluster(directory,
						index, fatbits(f)));
				printlongname("|", longname, "|");
				if (startdir && (res & FAT_LONG_ALL))
					printf("\t(%d,%d)",
						longdirectory->n, longindex);
				printf("%s\n",
					res & FAT_LONG_ERR ? "ERR" : "");
				free(longname);
			}
		}
	}
	else if (! strcmp(operation, "isvalid")) {
		if (useshortnames) {
			if ((finalres = fatinvalidpath(option1))) 
				printf("invalid\n");
			else {
				name = fatstoragepath(option1);
				printf("valid: |%s|\n", name);
				free(name);
			}
		}
		else {
			wlen = mbstowcs(NULL, option1, 0);
			if (wlen == (size_t) -1) {
				printf("invalid string: %s\n", option1);
				return -1;
			}
			longpath = malloc((wlen + 1) * sizeof(wchar_t));
			mbstowcs(longpath, option1, wlen + 1);

			if ((finalres = fatinvalidpathlong(longpath))) 
				printf("invalid\n");
			else {
				longname = fatstoragepathlong(longpath);
				printf("valid: |%ls|\n", longname);
				free(longname);
			}
		}
	}
	else if (! strcmp(operation, "dirfind")) {
		if (! strcmp(option1, ""))
			directoryclusters(f);
		else {
			cl = atoi(option1);
			if (cl < FAT_ROOT || cl > fatlastcluster(f))
				printf("invalid cluster: %d\n", cl);
			else {
				directory = fatclusterread(f, cl);
				if (directory == NULL)
					printf("cannot read cluster %d\n", cl);
				else {
					res = clusterscore(directory);
					printf("score(%d)=%d\n", cl, res);
				}
			}
		}
	}
	else {
		printf("unknown operation: %s\n", operation);
		exit(1);
	}

	if (clusterdump)
		fatunitdumpcache("clusters", f->clusters);
	fatclose(f);
	if (memcheck) {
		printf("==== memory check:\n");
#if defined(__GLIB__)
		malloc_stats();
#else
		printf("malloc_stats only available in glibc\n");
#endif
	}

	return finalres;
}
