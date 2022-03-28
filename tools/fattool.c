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
 * legalize paths or not
 */
int legalize;

/*
 * ask the used whether to proceed
 */
void check() {
	char line[10] = " ";
	char *res;

	printf("continue (y/N)? ");

	res = fgets(line, 5, stdin);

	if (res != NULL && res[0] == 'y')
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
 * print the complete path of a file
 */
void printpath(fat *f, wchar_t *path, unit *directory, int index,
		wchar_t *name, int err, unit *longdirectory, int longindex,
		void *user) {
	(void) f;
	(void) directory;
	(void) index;
	(void) err;
	(void) longdirectory;
	(void) longindex;
	(void) user;
	if (fatentryisdirectory(directory, index) && user)
		return;
	printlongname("", path, "");
	printlongname("", name, "\n");
}

/*
 * process an option that is a file and turns it into a cluster reference; in
 * some cases like "cluster:103", the target is filled but the reference is
 * void
 */
int fileoptiontoreferenceboth(fat *f, char *option,
		unit **directory, int *index,
		unit **longdirectory, int *longindex,
		int32_t *previous, int32_t *target) {
	int32_t r;
	char *path;
	size_t len;
	wchar_t *pathlong, *legalized, *converted;
	int res;

	r = fatgetrootbegin(f);
	*directory = NULL;
	*index = 0;
	*previous = 0;

	while (option[0] == '/')
		option++;

	if (option[0] == '\0') {
		*previous = -1;
		*target = fatreferencegettarget(f,
				*directory, *index, *previous);
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
		if (legalize) {
			legalized = fatlegalizepathlong(pathlong);
			converted = fatstoragepathlong(legalized);
			free(legalized);
		}
		else if (fatinvalidpathlong(pathlong) < 0) {
			printf("invalid path: %s\n", option);
			free(pathlong);
			return -1;
		}
		else
			converted = fatstoragepathlong(pathlong);
		free(pathlong);
	}

	res = fatlookuppathlongboth(f, r, converted, directory, index,
		longdirectory, longindex);
	*target = res ? fatlookuppathfirstclusterlong(f, r, converted) :
		fatreferencegettarget(f, *directory, *index, *previous);
	if (! nostoragepaths)
		free(converted);
	return res && *target == FAT_ERR;
}

/*
 * same as fileoptiontoreferenceboth, but discard the long entry
 */
int fileoptiontoreference(fat *f, char *option,
		unit **directory, int *index,
		int32_t *previous, int32_t *target) {
	unit *longdirectory;
	int longindex;

	return fileoptiontoreferenceboth(f, option,
		directory, index, &longdirectory, &longindex,
		previous, target);
}

/*
 * create a file
 */
int createfile(fat *f, int32_t *dir, char *path, int dot,
		unit **directory, int *index) {
	size_t len;
	wchar_t *pathlong, *legalized, *convertedlong;
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
		if (fatlookuppath(f, *dir, converted, directory, index))
			res = fatcreatefiledir(f, dir, converted,
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
	else if (legalize) {
		legalized = fatlegalizepathlong(pathlong);
		convertedlong = fatstoragepathlong(legalized);
		printlongname("filesystem name: |", convertedlong, "|\n");
		free(legalized);
	}
	else {
		res = fatinvalidpathlong(pathlong);
		if (res < 0 || (res == 1 && ! dot)) {
			printlongname("invalid long name: ", pathlong, "\n");
			return -1;
		}
		convertedlong = fatstoragepathlong(pathlong);
		printlongname("filesystem name: |", convertedlong, "|\n");
	}
	if (! fatlookuppathlong(f, *dir, convertedlong, directory, index)) {
		printf("file exists: ");
		printf("%d,%d\n", (*directory)->n, *index);
		free(convertedlong);
		return -1;
	}
	fatlongdebug = 1;
	res = fatcreatefilepathlongdir(f, dir, convertedlong, directory, index);
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
void fatzero(fat *f, int table) {
	int nfat;
	unit *root;
	int index;

	fatsetdirtybits(f, 0);

	if (table)
		for (nfat = 0; nfat < fatgetnumfats(f); nfat++)
			fatinittable(f, nfat);
	else if (fatbits(f) == 32)
		fatsetnextcluster(f, fatgetrootbegin(f), FAT_EOF);

	root = fatclusterread(f, fatgetrootbegin(f));
	for (index = 0; index < root->size / 32; index++)
		fatentryzero(root, index);
}

/*
 * create an hard link
 */
int fatlink(fat *f, char *target, char *new, int ncluster, uint32_t size) {
	unit *targetdir, *newdir;
	int targetind, newind;
	int32_t targetprev, targetcluster;
	int32_t dir;
	uint8_t attributes;
	int i;

	// fatdirectorydebug = 1;

	if (fileoptiontoreference(f, target,
			&targetdir, &targetind, &targetprev, &targetcluster)) {
		printf("cannot determine location of source file\n");
		return -1;
	}
	printf("source: ");
	fatreferenceprint(targetdir, targetind, targetprev);
	for (i = 0; i < ncluster; i++) {
		targetcluster = fatgetnextcluster(f, targetcluster);
		if (targetcluster == FAT_EOF || targetcluster == FAT_UNUSED)
			break;
	}
	printf(" cluster %d\n", targetcluster);
	if (ncluster != 0 &&
	    (targetcluster == FAT_EOF || targetcluster == FAT_UNUSED)) {
		printf("cluster number too large\n");
		return -1;
	}

	dir = fatgetrootbegin(f);
	if (createfile(f, &dir, new, 1, &newdir, &newind)) {
		printf("cannot create destination file\n");
		return -1;
	}
	printf("\ndestination: %d,%d\n", newdir->n, newind);

	fatentrysetfirstcluster(newdir, newind, f->bits, targetcluster);

	if (fatreferenceisvoid(targetdir, targetind, targetprev)) {
		size = fatbytespercluster(f) *
			fatcountclusters(f, NULL, 0, targetcluster, 0);
		attributes = 0x20;
	}
	else if (fatreferenceisboot(targetdir, targetind, targetprev)) {
		size = 0;
		attributes = 0x10;
	}
	else {
		if (size == 0)
			size = fatentrygetsize(targetdir, targetind) -
				ncluster * fatbytespercluster(f);
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
	int ret;

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
				fatdump(f, directory, index, previous, 0, 0, 0);
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

			fatdump(f, directory, index, previous, 0, 0, 0);
		}
	}

	printf("hexdump -C -s %" PRIu64 " -n %d %s\n",
		f->offset + origin + cl * size, size, f->devicename);
	buf = malloc(100 + strlen(f->devicename));
	sprintf(buf, "bvi -s 0x%" PRIx64 " -n %d %s",
		f->offset + origin + cl * size, size, f->devicename);
	puts(buf);

	if (run) {
		ret = system(buf);
		if (ret == -1 || ! WIFEXITED(ret))
			printf("cannot execute hexdump\n");
	}
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
 * count the number of entries in a directory
 */
void dircount(fat *f, char *path, unit *directory, int index, void *user) {
	(void) f;
	(void) path;
	if (! fatentryisdotfile(directory, index))
		(*((int *) user))++;
}

/*
 * free directory clusters that only contain deleted entries
 */

struct _directorycleanstruct {
	int recur;
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

	s = (struct _directorycleanstruct *) user;

	if (fatreferenceisentry(directory, index, previous) &&
			! fatreferenceisdirectory(directory, index, previous))
		return FAT_REFERENCE_DELETE;

	if (fatreferenceisdotfile(directory, index, previous))
		return FAT_REFERENCE_DELETE;
	
	if (direction != 0)
		return 0;

	target = fatreferencegettarget(f, directory, index, previous);
	if (target == fatgetrootbegin(f)) {
		printf("directory cluster %d used (first of root)\n", target);
		return FAT_REFERENCE_COND(s->recur);
	}
	if (target < FAT_FIRST)
		return FAT_REFERENCE_COND(s->recur);

	cluster = fatclusterread(f, target);
	if (cluster == NULL)
		return 0;

	used = 0;
	for (ind = 0; ind * 32 < cluster->size; ind++)
		if (fatentryexists(cluster, ind))
			used = 1;
	if (used) {
		printf("directory cluster %d used\n", target);
		return FAT_REFERENCE_COND(s->recur);
	}

	next = fatgetnextcluster(f, target);

	printf("directory cluster %d unused:\n\t", target);
	fatreferenceprint(directory, index, previous);
	printf("->%d->%d\n\t\t", target, next);
	printf("%s\n\t", s->testonly ? "would became" : "becomes");
	fatreferenceprint(directory, index, previous);
	printf("->%d\n", next);

	if (! s->testonly) {
		fatreferencesettarget(f, directory, index, previous, next);
		fatsetnextcluster(f, target, FAT_UNUSED);
		s->changed = 1;
	}

	return FAT_REFERENCE_COND(s->recur);
}

int directoryclean(fat *f, unit *directory, int index, uint32_t previous,
		int recur, int testonly) {
	struct _directorycleanstruct s;
	s.recur = recur;
	s.changed = 0;
	s.testonly = testonly;
	fatreferenceexecute(f, directory, index, previous,
		_directoryclean, &s);
	return s.changed;
}

/*
 * clean up the last deleted entries in a directory cluster
 */

struct _directorylaststruct {
	int recur;
	int testonly;
};

int _directorylast(fat *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	struct _directorylaststruct *s;
	unit *scandirectory, *lastdirectory;
	int scanindex, lastindex;

	s = (struct _directorylaststruct *) user;

	if (direction != 0)
		return 0;

	if (! fatreferenceiscluster(directory, index, previous)) {
		if (! fatreferenceisdirectory(directory, index, previous))
			return FAT_REFERENCE_DELETE;
		else if (fatreferenceisdotfile(directory, index, previous))
			return FAT_REFERENCE_DELETE;
		else
			return FAT_REFERENCE_COND(s->recur);
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
		return (s-> recur ? FAT_REFERENCE_RECUR : 0) |
			FAT_REFERENCE_DELETE;

	printf("last entry: %d,%d\n", lastdirectory->n, lastindex);
	printf("%s entries:", s->testonly ? "would clean" : "cleaning");
	while (fatnextentry(f, &lastdirectory, &lastindex) >= 0) {
		printf(" %d,%d", lastdirectory->n, lastindex);
		if (! s->testonly)
			fatentryzero(lastdirectory, lastindex);
	}
	printf("\n");

	/* do not visit the chain */

	return (s-> recur ? FAT_REFERENCE_RECUR : 0) | FAT_REFERENCE_DELETE;
}

void directorylast(fat *f, unit *directory, int index, uint32_t previous,
		int recur, int testonly) {
	struct _directorylaststruct s;
	s.recur = recur;
	s.testonly = testonly;
	fatreferenceexecute(f, directory, index, previous, _directorylast, &s);
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
				else if (strchr("_-%~", c))
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
 * size of a file, as number of 512-sectors
 */
uint32_t filesize(char *file) {
	struct stat ss;
	int fd;
	int sectors;
	int sectorsize = 512;

	if (stat(file, &ss)) {
		perror(file);
		return 0;
	}
	if (S_ISREG(ss.st_mode))
		return ss.st_size / sectorsize;
	else if (S_ISBLK(ss.st_mode)) {
		fd = open(file, O_RDONLY);
		ioctl(fd, BLKGETSIZE, &sectors);
		close(fd);
		return sectors;
	}
	printf("unsupported file type\n");
	return 0;
}

/*
 * obtain offset and size from an mbr partition
 */
int mbr(char *file, int num, uint32_t *start, uint32_t *length) {
	int fd;
	int res;
	unsigned char sector[512], *entry;
	unsigned char active, label;
	uint32_t *record;

	fd = open(file, O_RDONLY);
	if (fd == -1)
		return -2;

	res = read(fd, sector, 512);
	if (res != 512)
		return -3;

	if (sector[0x1FE] != 0x55 || sector[0x1FF] != 0xAA)
		return -4;

	entry = sector + 0x1BE + (num - 1) * 16;

	active = entry[0];
	if (active != 0 && active != 0x80)
		return -4;

	label = entry[4];
	if (label == 0)
		return -1;

	record = (uint32_t *) entry;
	*start = le32toh(record[2]);
	*length = le32toh(record[3]);

	close(fd);

	return 0;
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

void fatsetsize(fat *f) {
	int bits, extra, best;

	extra = 0;

	for (bits = 12; ; bits = fatbits(f)) {
		f->bits = bits;
		fatsetreservedsectors(f, bits == 32 ? 32 : 1);
		best = fatbestfatsize(f);
		fatsetfatsize(f, best + extra);
		f->bits = fatnumdataclusters(f) < 0 ? 32 : 0;
		if (bits == fatbits(f))
			return;
		if (bits > fatbits(f))
			extra++;
	}
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

int fatformat(char *devicename, off_t offset, uint32_t len, int truncate,
		char *option1, char *option2, char *option3, char *option4) {
	fat *f;
	int sectorsize = 512;
	unsigned long int ul;
	uint32_t sectors, sectpercl;
	unsigned maxentries;
	int res;

	if (len > 0)
		sectors = len;
	else {
		errno = 0;
		ul = strtoul(option1, NULL, 10);
		if ((ul == ULONG_MAX && errno == ERANGE) || ul > 0xFFFFFFFF) {
			printf("overflow: %s\n", option1);
			return -1;
		}
		sectors = ul;
	}

	errno = 0;
	ul = strtoul(option2, NULL, 10);
	if ((ul == ULONG_MAX && errno == ERANGE) || ul > 0xFFFFFFFF) {
		printf("overflow: %s\n", option2);
		return -1;
	}
	sectpercl = ul;

	errno = 0;
	ul = strtoul(option3, NULL, 10);
	if ((ul == ULONG_MAX && errno == ERANGE) || ul > 0xFFFFFFFF) {
		printf("overflow: %s\n", option3);
		return -1;
	}
	maxentries = ul;

	if (len == 0 && (option1[0] == '\0' || sectors == 0)) {
		len = filesize(devicename);
		if (len == 0) {
			printf("cannot determine size of filesystem, ");
			printf("and it was not specified\n");
			return -1;
		}
		len -= (offset + sectorsize - 1) / sectorsize;
		sectors = len;
	}
	printf("sectors: %u\n", sectors);

	f = fatcreate();
	f->devicename = devicename;
	f->offset = offset;
	f->boot = fatunitcreate(sectorsize);
	f->boot->n = 0;
	f->boot->origin = offset;
	f->boot->dirty = 1;
	fatunitinsert(&f->sectors, f->boot, 1);

	memset(fatunitgetdata(f->boot), 0, f->boot->size);
	fatsetnumfats(f, 2);
	fatsetbytespersector(f, sectorsize);
	fatsetnumsectors(f, sectors);

	if (option2[0] == '\0' || sectpercl == 0) {
		printf("secXcl\troot\ttype\n");
		for (sectpercl = 1; sectpercl < 256; sectpercl <<= 1) {
			fatsetsectorspercluster(f, sectpercl);
			printf("%d\t", sectpercl);
			fatsetsize(f);
			if (fatsetentries(f, maxentries)) {
				printf("invalid number of entries ");
				printf("in root directory: %d\n", maxentries);
				continue;
			}
			printf("%d\t", fatgetrootentries(f));
			if (fatnumdataclusters(f) < 0)
				printf("too many clusters\n");
			else if (! fatconsistentsize(f))
				toosmall(f);
			else {
				printf("FAT%d", fatbits(f));
				if (fatnumdataclusters(f) > 0x0FFFFFFF)
					printf(" - WARNING: >28bits");
				printf("\n");
			}
		}
		return 0;
	}

	if (fatsetsectorspercluster(f, sectpercl)) {
		printf("invalid sectors per cluster: %d\n", sectpercl);
		return -1;
	}

	fatsetsize(f);
	if (fatnumdataclusters(f) < 0) {
		printf("too many clusters\n");
		return -1;
	}
	else if (! fatconsistentsize(f)) {
		toosmall(f);
		return -1;
	}

	if (fatsetentries(f, maxentries)) {
		printf("invalid number of entries in root: %d\n", maxentries);
		return -1;
	}

	fatsetbootsignature(f);
	fatsetrootbegin(f, fatbits(f) == 32 ? FAT_FIRST : FAT_ROOT);

	if (fatbits(f) != 32)
		f->info = NULL;
	else {
		fataddextendedbootsignature(f);
		fatsetfilesystemtype(f, "FAT32   ");
		srandom(time(NULL));
		fatsetserialnumber(f, random() % 0xFFFFFFFF);

		f->info = fatunitcreate(sectorsize);
		f->info->n = 1;
		f->info->origin = offset;
		f->info->dirty = 1;
		fatunitinsert(&f->sectors, f->info, 1);
		fatsetinfopos(f, f->info->n);
		fatsetinfosignatures(f);
		fatsetfreeclusters(f, fatlastcluster(f) - 2 - 1);

		fatsetbackupsector(f, 6);
	}

	fatsummary(f);
	if (fatnumdataclusters(f) > 0x0FFFFFFF) {
		printf("WARNING: each cluster entry in FAT takes ");
		printf("more than 28 bits: not portable\n");
		check();
	}

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
	if (len == 0 && truncate && ! strstr(option4, "noresize")) {
		res = ftruncate(f->fd,
			f->offset +
			1LLU * fatgetnumsectors(f) * fatgetbytespersector(f));
		if (res == -1)
			return -1;
	}
	f->boot->fd = f->fd;
	if (f->info != NULL)
		f->info->fd = f->fd;

	fatsetmedia(f, 0xF8);
	if (fatbits(f) == 32)
		fatcopyboottobackup(f);
	fatzero(f, ! ! strcmp(option4, "nofats"));
	fatclose(f);

	return 0;
}

/*
 * parse a range of clusters
 */
int parserange(char *option, int32_t *first, int32_t *last) {
	char *minus, *end;
	long int val;

	if (*option == '-')
		minus = option;
	else {
		errno = 0;
		val = strtol(option, &minus, 10);
		if (*minus != '-') {
			printf("cannot parse range: %s\n", option);
			return -1;
		}
		if (val == LONG_MAX && errno == ERANGE) {
			printf("overflow: %s\n", option);
			return -2;
		}
		if (minus != option)
			*first = val;
	}

	errno = 0;
	val = strtol(minus + 1, &end, 0);
	if (val == LONG_MAX && errno == ERANGE) {
		printf("overflow: %s\n", option);
		return -3;
	}
	if (*end != '\0') {
		printf("extra characters at end of range: %s\n", option);
		return -4;
	}
	if (end != minus + 1)
		*last = val;

	return 0;
}

/*
 * usage
 */
void usage() {
	printf("usage:\n\tfattool [-f num] [-l] [-s] [-t] [-n] ");
	printf("[-m] [-c] [-o offset] [-p num]\n");
	printf("\t\t[-a first-last] [-e simerr.txt] ");
	printf("device operation [arg...]\n");
	printf("\t\t-f num\t\tuse the specified file allocation table\n");
	printf("\t\t-l\t\tload the first FAT in cache immediately\n");
	printf("\t\t-s\t\tuse shortnames\n");
	printf("\t\t-t\t\tlegalize paths: convert invalid characters\n");
	printf("\t\t-n\t\tdo not check or convert names\n");
	printf("\t\t-m\t\tmemory check at the end\n");
	printf("\t\t-c\t\tcheck: show cluster cache at the end\n");
	printf("\t\t-o offset\tfilesystem starts at this offset in device\n");
	printf("\t\t-d\t\tdetermine number of bits from signature\n");
	printf("\t\t-b num\t\tuse n-th sector as the boot sector\n");
	printf("\t\t-a first-last\trange of allocable clusters\n");
	printf("\t\t-e simerr.txt\tread simulated errors from file\n");
	printf("\n\toperations:\n");
	printf("\t\tsummary\t\tbasic characteristics of the filesystem\n");
	printf("\t\tgetserial\tget the filesystem serial number\n");
	printf("\t\tsetserial\tset the filesystem serial number\n");
	printf("\t\tgetlastcluster\tget the number of the last cluster\n");
	printf("\t\tsetlastcluster num [nosize]\n");
	printf("\t\t\t\tchange the number of the last cluster\n");
	printf("\t\t\t\tnosize: do not check size of device\n");
	printf("\t\tused\t\tmap of used clusters\n");
	printf("\t\tfree\t\tmap of free clusters\n");
	printf("\t\tmap\t\tmap of all clusters, with\n");
	printf("\t\t\t\tthe free ones bracketed like [21]\n");
	printf("\t\tchains\t\tshow the chains of consecutive clusters\n");
	printf("\t\tview [path [all] [chains]]\n");
	printf("\t\t\t\tshow the filesystem\n");
	printf("\t\t\t\tall: include the deleted entries\n");
	printf("\t\t\t\tchains: print chains of clusters as first-last\n");
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
	printf("\t\tunreachable (fix|clusters|chains) [each]\n");
	printf("\t\t\t\tlist or free all unused clusters\n");
	printf("\t\tdelete\t\tforce deletion of a file or directory\n");
	printf("\t\tlink target new [n [size]]\n");
	printf("\t\t\t\tcreate an hard link\n");
	printf("\t\t\t\tnew file points to the n-th cluster of target\n");
	printf("\t\t\t\toptionally specify size of new file\n");
	printf("\t\tcrop file (leave|free) [size]\n");
	printf("\t\t\t\tcut the chain of clusters of a file\n");
	printf("\t\t\t\toptionally free the subsequent clusters\n");
	printf("\t\t\t\tchain left long enough for a file of given size\n");
	printf("\t\textend file size\n");
	printf("\t\t\t\telongate or shorten a chain of cluster,\n");
	printf("\t\t\t\tmaking it long enough for a file of that size\n");
	printf("\t\tconcat file file [pad]\n");
	printf("\t\t\t\tconcatenate two file, padding the first\n");
	printf("\t\tcreatechain size [start]\n");
	printf("\t\t\t\tcreate a new chain of cluster\n");
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
	printf("\t\tsetnext n m [force]\n");
	printf("\t\t\t\tset the next of cluster n to be m\n");
	printf("\t\tcheckfats [start end [num]]\n");
	printf("\t\tmergefats [start end [num]]\n");
	printf("\t\t\t\tcheck or ensure the consistency of the two fats\n");
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
	printf("\t\tdeletefile name [(dir|force) [erase]]\n");
	printf("\t\t\t\tdelete a file\n");
	printf("\t\toverwrite name [test]\n\t\t\t\toverwrite the ");
	printf("differing clusters of a file\n");
	printf("\t\tconsecutive name size\n\t\t\t\tcreate a ");
	printf("file of consecutive clusters\n");
	printf("\t\tgetattrib file\tget attributes of file\n");
	printf("\t\tsetattrib file attrib\n\t\t\t\tset attributes of file\n");
	printf("\t\tgetsize file\tget size of file\n");
	printf("\t\tsetsize file size\n\t\t\t\tchange file size; ");
	printf("directory entry only, see man\n");
	printf("\t\tisvalid filename\n");
	printf("\t\t\t\tcheck whether the filename is legal\n");
	printf("\t\tlegalize filename\n\t\t\t\tmake the filename legal\n");
	printf("\t\tgetname filename [short]\n");
	printf("\t\t\t\tprint the name or short name of a file\n");
	printf("\t\tsetname filename newname\n");
	printf("\t\t\t\tchange the short name of a file\n");
	printf("\t\tfind\t\tlist all files in the volume\n");
	printf("\t\tmkdir directory\tcreate a directory\n");
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
	printf("\t\trecover file [size]\n");
	printf("\t\t\t\tattempt reading a deleted file\n");
	printf("\t\tundelete file\tattempt undeleting a file\n");
	printf("\t\tdirfind [num]\ttry to locate directory clusters\n");
	printf("\t\tformat (sectors|\"\") (sectorsperclusters|\"\") ");
	printf("(maxentries|\"\")\n");
	printf("\t\t\t\tcreate a filesystem or estimate its type\n");
}

/* 
 * main
 */
int main(int argn, char *argv[]) {
	char *name, *operation, *option1, *option2, *option3, *option4;
	int partition;
	uint32_t begin, length, fsize;
	int32_t afirst, alast;
	off_t offset;
	int signature;
	size_t wlen;
	wchar_t *longname, *longpath, *legalized;
	fat *f, *cross;
	int fatnum, bootindex;
	int32_t previous, target, r, dir, cl, next, other, start;
	int32_t secondprevious, secondtarget, end, last, len;
	unit *directory, *startdirectory, *longdirectory, *seconddirectory;
	int index, startindex, longindex, secondindex;
	unit *cluster;
	int max, size, csize, pos, ncluster;
	uint32_t sector, spos, serial;
	unsigned long readserial;
	int res, diff, finalres, recur, chain, all, chains;
	int over, startdir, nchanges;
	char dummy, pad, *buf, firstchar, attrib;
	int nfat;
	char *timeformat;
	struct tm tm;
	int first, clusterdump, insensitive, memcheck;
	int immediate, testonly, try;
	fatinverse *rev;
	char *simerrfile;
	int dirty;

	finalres = 0;

				/* arguments */

	partition = 0;
	offset = 0;
	length = 0;
	signature = 0;
	fatnum = -1;
	bootindex = 0;
	first = 0;
	insensitive = 0;
	useshortnames = 0;
	legalize = 0;
	nostoragepaths = 0;
	afirst = -1;
	alast = -1;
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
		case 'p':
			if (argv[1][2] != '\0')
				partition = atol(argv[1] + 1);
			else {
				partition = atol(argv[2]);
				argn--;
				argv++;
			}
			break;
		case 'd':
			signature = 1;
			break;
		case 'b':
			if (argv[1][2] != '\0')
				bootindex = atoi(argv[1] + 1);
			else {
				bootindex = atoi(argv[2]);
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
		case 't':
			legalize = 1;
			break;
		case 'n':
			nostoragepaths = 1;
			break;
		case 'a':
			if (argv[1][2] != '\0')
				res = parserange(argv[1] + 2, &afirst, &alast);
			else {
				res = parserange(argv[2], &afirst, &alast);
				argn--;
				argv++;
			}
			if (res < 0)
				exit(EXIT_FAILURE);
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
	option4 = argn - 1 >= 6 ? argv[6] : "";

				/* partition data */

	if (partition >= 1) {
		res = mbr(name, partition, &begin, &length);
		if (res == -1) {
			printf("no partition %d on %s\n", partition, name);
			exit(EXIT_FAILURE);
		}
		else if (res < 0) {
			printf("invalid mbr on %s\n", name);
			exit(EXIT_FAILURE);
		}
		length -= (offset + 512 - 1) / 512;
		offset += 512 * begin;
	}

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
		return fatformat(name, offset, length, partition == 0,
			option1, option2, option3, option4);

				/* validity of a path */

	if (! strcmp(operation, "isvalid")) {
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
		return 0;
	}
	else if (! strcmp(operation, "legalize")) {
		wlen = mbstowcs(NULL, option1, 0);
		if (wlen == (size_t) -1)
			return -1;
		longpath = malloc((wlen + 1) * sizeof(wchar_t));
		mbstowcs(longpath, option1, wlen + 1);
		legalized = fatlegalizepathlong(longpath);
		printf("%ls\n", legalized);
		free(legalized);
		free(longpath);
		return 0;
	}

				/* open and check */

	if (bootindex == 0)
		f = signature ?
			fatsignatureopen(name, offset) : fatopen(name, offset);
	else {
		f = fatopenonly(name, offset, bootindex);
		if (f != NULL) {
			f->bits = signature ? fatsignaturebits(f) : fatbits(f);
			if (fatbits(f) == -1 ||
			    (fatbits(f) == 32 &&
			     fatreadinfosector(f, fatgetinfopos(f))) ||
			    fatcheck(f)) {
				fatquit(f);
				f = NULL;
			}
		}
	}
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

	afirst = afirst != -1 ? afirst : FAT_FIRST;
	alast = alast != -1 ? alast : fatlastcluster(f);

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
	else if (! strcmp(operation, "getserial")) {
		if (fatgetextendedbootsignature(f) != 1) {
			printf("no extended boot signature, ");
			printf("= no serial number\n");
			return 1;
		}
		printf("0x%08X\n", fatgetserialnumber(f));
	}
	else if (! strcmp(operation, "setserial")) {
		if (fatgetextendedbootsignature(f) != 1) {
			printf("no extended boot signature, ");
			printf("= no serial number\n");
			return 1;
		}
		readserial = strtoul(option1, &buf, 0);
		if (buf == option1) {
			printf("not a number: %s\n", option1);
			return 1;
		}
		serial = readserial & 0xFFFFFFFF;
		if (serial != readserial ||
		    (readserial == ULONG_MAX && errno == ERANGE)) {
			printf("number too big: %s\n", option1);
			return 1;
		}
		printf("new serial number: 0x%08X\n", serial);
		fatsetserialnumber(f, serial);
		fatcopyboottobackup(f);
	}
	else if (! strcmp(operation, "getlastcluster"))
		printf("%d\n", last);
	else if (! strcmp(operation, "setlastcluster")) {
		if (option1[0] == '\0') {
			printf("argument missing: new last cluster\n");
			return -1;
		}
		last = strtoul(option1, &buf, 0);
		if (last < 2) {
			printf("last cluster must be >= 2\n");
			return -1;
		}
		if (! strstr(option2, "nofat") &&
		    (last * fatbits(f) + 8 - 1) / 8 >=
		    fatgetfatsize(f) * fatgetbytespersector(f)) {
			printf("not enough space in FAT ");
			printf("for cluster %d\n", last);
			exit(EXIT_FAILURE);
		}
		sector = (last - 1) * fatgetsectorspercluster(f) +
			fatnumrootsectors(f) +
			fatgetnumfats(f) * fatgetfatsize(f) +
			fatgetreservedsectors(f);
		fsize = length > 0 ? length : filesize(name);
		if (! strstr(option2, "nosize") &&
		    fsize > 0 && sector > fsize) {
			printf("file is not large enough: ");
			printf("%d > %d\n", sector, fsize);
			exit(EXIT_FAILURE);
		}
		fatsetnumsectors(f, sector);
	}
	else if (! strcmp(operation, "free"))
		fatmap(f, "%d", "     .", "BAD");
	else if (! strcmp(operation, "used"))
		fatmap(f, "     .", "%d", "BAD");
	else if (! strcmp(operation, "map"))
		fatmap(f, "[%d]", "%6d", "B-%d");
	else if (! strcmp(operation, "chains")) {
		start = FAT_FIRST;
		for (cl = FAT_FIRST; cl <= fatlastcluster(f); cl++) {
			next = fatgetnextcluster(f, cl);
			if (next == cl + 1)
				continue;

			target = fatgetnextcluster(f, start);

			if (target == FAT_UNUSED || target == FAT_BAD) {
			}
			else if (cl == start)
				printf("%d\n", cl);
			else
				printf("%d-%d\n", start, cl);

			start = cl + 1;
		}
	}
	else if (! strcmp(operation, "view")) {
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found\n");
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous))
			previous = target;
		all = ! strcmp(option2, "all") || ! strcmp(option3, "all");
		chains = ! strcmp(option2, "chains") || \
		         ! strcmp(option3, "chains");
		if (useshortnames)
			fatdump(f, directory, index, previous, 1, all, chains);
		else
			fatdumplong(f, directory, index, previous,
				1, all, chains);
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
		else if (nchanges == 0)
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
	else if (! strcmp(operation, "getid") ||
		 ! strcmp(operation, "geteof")) {
		cl = ! strcmp(operation, "getid") ? 0 : 1;
		printf(f->bits == 12 ? "0x%03X\n" :
		       f->bits == 16 ? "0x%04X\n" : "0x%08X\n",
			fatgetfat(f, f->nfat == -1 ? 0 : f->nfat, cl));
	}
	else if (! strcmp(operation, "setid") ||
		 ! strcmp(operation, "seteof")) {
		if (option1[0] == '\0') {
			printf("missing value to set\n");
			exit(1);
		}
		next = atoi(option1);
		cl = ! strcmp(operation, "setid") ? 0 : 1;
		fatsetfat(f, f->nfat == -1 ? 0 : f->nfat, cl, next);
	}
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
		fatzero(f, 1);
	}
	else if (! strcmp(operation, "unreachable")) {
		fatinversedebug = 1;
		all = ! strcmp(option2, "each");
		if (option1[0] == '\0')
			printf("option required: clusters, chain or fix\n");
		else if (! strcmp(option1, "chains"))
			fatunreachable(f, 0, all);
		else if (! strcmp(option1, "clusters"))
			fatunreachable(f, 1, all);
		else if (! strcmp(option1, "fix"))
			fatunreachable(f, 2, all);
		else
			printf("unknow option: %s\n", option1);
	}
	else if (! strcmp(operation, "delete")) {
		if (fileoptiontoreferenceboth(f, option1,
				&directory, &index,
				&longdirectory, &longindex,
				&previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (directory == NULL) {
			printf("cannot delete %s\n", option1);
			exit(1);
		}
		if (! useshortnames)
			if (fatdeletelong(f, longdirectory, longindex))
				printf("error while deleting long name\n");
		fatentrydelete(directory, index);
		printf("filesystem may be unclean, fix with:\n");
		printf("fattool %s unreachable fix\n", name);
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
			ncluster = atoi(option3);
			size = atoi(option4);
			if (fatlink(f, option1, option2, ncluster, size))
				printf("error creating link\n");
			else {
				printf("\ndo not delete with the OS\n");
				printf("ONLY delete with:\n\t fattool ");
				printf("%s delete %s\n", name, option2);
			}
		}
	}
	else if (! strcmp(operation, "crop")) {
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found: %s\n", option1);
			exit(1);
		}
		if (! strcmp(option2, "free"))
			chain = 1;
		else if (! strcmp(option2, "leave"))
			chain = 0;
		else {
			printf("wrong argument: ");
			printf("should be either \"free\" or \"leave\"\n");
			exit(EXIT_FAILURE);
		}
		size = option3[0] == '\0' ?
			fatentrygetsize(directory, index) :
			(unsigned) atoi(option3);

		for (cl = target;
		     cl >= FAT_FIRST && size > 0;
		     cl = fatreferencenext(f, &directory, &index, &previous))
			size -= fatbytespercluster(f);
		fatreferencesettarget(f, directory, index, previous, FAT_EOF);
		if (chain)
			fatclusterfreechain(f, cl);
	}
	else if (! strcmp(operation, "extend")) {
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("not found: %s\n", option1);
			exit(1);
		}
		size = atol(option2);
		for (cl = previous > 0 ? previous : target, next = FAT_UNUSED;
		     size > 0 || next != FAT_EOF;
		     cl = next) {
			cluster = fatclusterread(f, cl);
			if (cluster == NULL)
				break;
			size -= cluster->size;
			next = fatgetnextcluster(f, cl);
			if (size <= -cluster->size)
				fatsetnextcluster(f, cl, FAT_UNUSED);
			else if (size <= 0)
				fatsetnextcluster(f, cl, FAT_EOF);
			else if (next == FAT_EOF || next == FAT_UNUSED) {
				next = fatclusterfindfreebetween(f,
					afirst, alast, -1);
				fatsetnextcluster(f, cl, next);
				fatsetnextcluster(f, next, FAT_EOF);
			}
		}
		if (atol(option2) == 0)
			fatreferencesettarget(f,
				directory, index, previous, FAT_EOF);
	}
	else if (! strcmp(operation, "concat")) {
		if (fileoptiontoreference(f, option1,
				&startdirectory, &startindex,
				&previous, &target)) {
			printf("not found: %s\n", option1);
			exit(1);
		}
		if (fileoptiontoreference(f, option2,
				&seconddirectory, &secondindex,
				&secondprevious, &secondtarget)) {
			printf("not found: %s\n", option2);
			exit(1);
		}

		directory = startdirectory;
		index = startindex;
		ncluster = fatreferencelast(f, &directory, &index, &previous);
		size = fatbytespercluster(f) * ncluster;

		pos = size - fatentrygetsize(startdirectory, startindex);
		if (pos > 0 && ! ! strcmp(option3, "-")) {
			pad = strtol(option3, NULL, 0);
			cluster = fatclusterread(f, previous);
			if (cluster) {
				memset(fatunitgetdata(cluster) +
					fatbytespercluster(f) - pos, pad, pos);
				cluster->dirty = 1;
				fatunitwriteback(cluster);
			}
		}

		fatreferencesettarget(f, directory, index, previous,
			secondtarget);
		size += fatentrygetsize(seconddirectory, secondindex);
		fatentrysetsize(startdirectory, startindex, size);
	}
	else if (! strcmp(operation, "createchain")) {
		size = atol(option1);
		if (option2[0] == '\0') {
			start = fatclusterfindfreebetween(f,
				afirst, alast, -1);
			printf("%d\n", start);
		}
		else {
			start = atol(option2);
			f->last = start + 1;
		}
		for (cl = start; size > 0; cl = next) {
			next = fatclusterfindfreebetween(f,
				afirst, alast, -1);
			if (cl == FAT_ERR || next == FAT_ERR) {
				printf("failed to allocate cluster\n");
				exit(1);
			}
			fatsetnextcluster(f, cl, next);
			fatsetnextcluster(f, next, FAT_EOF);
			cluster = fatclustercreate(f, next);
			size -= cluster->size;
			fatunitdelete(&f->clusters, next);
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
		else {
			next = atol(option2);
			dirty = ! strcmp(option3, "force");
			last = fatlastcluster(f);
			if ((next < FAT_FIRST || next > last) && ! dirty) {
				printf("cluster %d out of range ", next);
				printf("(max %d)\n", fatlastcluster(f));
				exit(1);
			}
		}
		fatsetnextcluster(f, cl, next);
	}
	else if (! strcmp(operation, "checkfats") ||
		 ! strcmp(operation, "mergefats")) {
		testonly = ! strcmp(operation, "checkfats");
		if (fatgetnumfats(f) != 2) {
			printf("this filesystem does not have two fats\n");
			exit(EXIT_FAILURE);
		}
		start = option1[0] == '\0' ? FAT_FIRST : atoi(option1);
		end = option2[0] == '\0' ? fatlastcluster(f) : atoi(option2);
		nfat = option3[0] == '\0' ? -1 : atoi(option3);
		printf("%s fats ", testonly ? "comparing" : "merging");
		printf("on clusters %d - %d", start, end);
		if (nfat != -1)
			printf(", preferred fat %d", nfat);
		printf("\n");
		if (! testonly) {
			printf("WARNING: this operation cannot be undone\n");
			printf("WARNING: better first try checkfats\n");
			check();
			printf("\n");
		}
		diff = 0;
		res = 1;
		for (cl = start; cl <= end; cl++) {
			next = fatgetfat(f, 0, cl);
			other = fatgetfat(f, 1, cl);
			if (next == other)
				continue;
			diff = 1;
			printf("%d -> ", cl);
			fatprintfat(f, next);
			printf("/");
			fatprintfat(f, other);

			if (next > fatlastcluster(f) &&
			         other <= fatlastcluster(f))
				next = other;
			else if (next <= fatlastcluster(f) &&
			         other > fatlastcluster(f))
				other = next;
			else if (nfat == 0)
				other = next;
			else if (nfat == 1)
				next = other;
			else {
				printf(" NOFIX\n");
				res = 0;
				continue;
			}
			printf(" -> ");
			fatprintfat(f, next);
			printf("\n");

			if (! testonly) {
				fatsetfat(f, 0, cl, next);
				fatsetfat(f, 1, cl, next);
			}
		}
		if (diff == 0)
			printf("no difference in FATs\n");
		else {
			printf("%sall automatically ", res == 1 ? "" : "not ");
			printf("%s\n", testonly ? "fixable" : "fixed");
		}
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
			fatunreachable(f, 1, 0);
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
			res = write(1, fatunitgetdata(cluster),
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

		dir = r;
		if (createfile(f, &dir, option1, 0, &directory, &index)) {
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
			next = fatclusterfindfreebetween(f,
				afirst, alast, -1);
			printf("next: %d max: %d       \r", next, max);
			if (next == FAT_ERR) {
				printf("filesystem full\n");
				exit(1);
			}

			if (max == -1) {
				cluster = fatclustercreate(f, next);
				if (cluster == NULL)
					cluster = fatclusterread(f, next);
				if (cluster == NULL) {
					printf("error creating cluster %d\n",
						next);
					exit(1);
				}

				csize = cluster->size;
				res = read(0, fatunitgetdata(cluster), csize);

				if (res > 0) {
					cluster->dirty = 1;
					fatunitwriteback(cluster);
				}
				fatunitdelete(&f->clusters, cluster->n);
			}
			else {
				csize = fatbytespercluster(f);
				res = max > csize ? csize : max;
				max -= res;
			}

			if (res <= 0)
				break;

			fatreferencesettarget(f, directory, index, cl, next);
			fatsetnextcluster(f, next, FAT_EOF);

			fatreferencenext(f, &directory, &index, &cl);

			size += res;
		} while (res == csize);

		fatentrysetsize(startdirectory, startindex, size);
		fatentrysetattributes(startdirectory, startindex, 0x20);
		printf("\n");
	}
	else if (! strcmp(operation, "deletefile")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreferenceboth(f, option1,
				&directory, &index,
				&longdirectory, &longindex,
				&previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot delete a file by cluster\n");
			exit(1);
		}
		if (fatreferenceisboot(directory, index, previous)) {
			printf("cannot delete the root directory\n");
			exit(1);
		}
		if (fatreferenceisdirectory(directory, index, previous)) {
			if (! strcmp(option2, "dir")) {
				size = 0;
				fatfileexecute(f, directory, index, previous,
					dircount, &size);
				if (size > 1) {
					printf("%s ", option1);
					printf("is a non-empty directory, ");
					printf("use \"force\" ");
					printf("to delete it anyway\n");
					printf("then \"fattool ");
					printf("%s ", f->devicename);
					printf("unused\" to fix ");
					printf("the filesystem\n");
					exit(1);
				}
			}
			else if (! ! strcmp(option2, "force")) {
				printf("%s is a directory, ", option1);
				printf("add \"dir\" to delete it anyway\n");
				exit(1);
			}
		}

		next = fatentrygetfirstcluster(directory, index, fatbits(f));
		for (cl = next; cl != FAT_EOF && cl != FAT_UNUSED; cl = next) {
			next = fatgetnextcluster(f, cl);
			fatsetnextcluster(f, cl, FAT_UNUSED);
		}

		if (! ! strcmp(option3, "erase")) {
			if (! useshortnames &&
			    (longdirectory != directory || longindex != index))
				if (fatdeletelong(f, longdirectory, longindex))
					printf("error deleting long name\n");
			fatentrydelete(directory, index);
		}
		else {
			while (longdirectory != directory ||
					longindex != index) {
				fatentryzero(longdirectory, longindex);
				fatentrydelete(longdirectory, longindex);
				fatnextentry(f, &longdirectory, &longindex);
			}
			fatentryzero(longdirectory, longindex);
			fatentrydelete(longdirectory, longindex);
		}
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

		max = 1;
		if (option3[0] != '\0')
			max = atoi(option3);

		cl = target;
		cluster = cl < FAT_FIRST ? NULL : fatclusterread(f, cl);
		size = cluster == NULL ? 0 : cluster->size;
		buf = malloc(size);
		printf(testonly ? "differing clusters:" : "writing clusters:");
		fflush(stdout);

		while (cluster && 0 < (res = read(0, buf, cluster->size))) {
			diff = 1;
			for (try = 0; try < max; try++) {
				diff = memcmp(fatunitgetdata(cluster),
					buf, res);
				if (diff == 0)
					break;
				fatunitfree(cluster);
			}

			if (diff) {
				finalres = 64;
				printf(" %d", cl);
				fflush(stdout);
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
	else if (! strcmp(operation, "consecutive")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (! fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			if (fatreferenceisvoid(directory, index, previous))
				printf("cannot create file by cluster\n");
			else  {
				printf("file %s exists, ", option1);
				printf("not overwriting\n");
			}
			exit(1);
		}
		if (option2[0] == '\0') {
			printf("missing argument: size\n");
			exit(1);
		}
		size = atoi(option2);
		if (size <= 0) {
			printf("size must be greater than zero\n");
			exit(1);
		}
		ncluster = (size + fatbytespercluster(f) - 1) /
			fatbytespercluster(f);

		start = fatclusterfindfreesequencebetween(f,
			afirst, alast, -1, ncluster);
		if (start == FAT_ERR) {
			printf("not enough consecutive free clusters\n");
			exit(1);
		}

		dir = r;
		if (createfile(f, &dir, option1, 0, &directory, &index)) {
			printf("cannot create file %s\n", option1);
			exit(1);
		}
		fatentrysetsize(directory, index, size);
		fatentrysetfirstcluster(directory, index, fatbits(f), start);

		for (cl = 0; cl < ncluster; cl++)
			fatsetnextcluster(f, start + cl, start + cl + 1);
		fatsetnextcluster(f, start + cl - 1, FAT_EOF);
	}
	else if (! strcmp(operation, "getattrib")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		printf("0x%02X\n", fatentrygetattributes(directory, index));
	}
	else if (! strcmp(operation, "setattrib")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (option2[0] == '\0') {
			printf("missing argument: attributes\n");
			exit(1);
		}
		attrib = strtod(option2, NULL);
		fatentrysetattributes(directory, index, attrib);
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
	else if (! strcmp(operation, "getname")) {
		if (option1[0] == '\0') {
			printf("missing argument: file name\n");
			exit(1);
		}
		if (fileoptiontoreferenceboth(f, option1,
				&directory, &index,
				&longdirectory, &longindex,
				&previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot obtain name by cluster\n");
			exit(1);
		}
		if (useshortnames || ! strcmp(option2, "short")) {
			fatentryprintshortname(directory, index);
			printf("\n");
		}
		else {
			if (! (fatlongentrytoshort(f, longdirectory, longindex,
					&directory, &index, &longname))) {
				printf("error in file %ls\n", longname);
				exit(1);
			}
			printf("%ls\n", longname);
		}
	}
	else if (! strcmp(operation, "setname")) {
		if (option1[0] == '\0') {
			printf("missing argument: file name\n");
			exit(1);
		}
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (option2[0] == '\0') {
			printf("missing argument: new file name\n");
			exit(1);
		}
		if ((finalres = fatinvalidname(option2)))
			printf("invalid short file name: %s\n", option2);
		else {
			name = fatstoragename(option2);
			fatentrysetshortname(directory, index, name);
			free(name);
		}
	}
	else if (! strcmp(operation, "find")) {
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		fatfileexecutelong(f, directory, index, previous, printpath,
			! strcmp(option2, "dir") ? (void *) 0 : (void *) 1);
	}
	else if (! strcmp(operation, "mkdir")) {
		if (option1[0] == '\0') {
			printf("missing argument: directory\n");
			exit(1);
		}
		if (! fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			if (fatreferenceisvoid(directory, index, previous))
				printf("cannot create directory by cluster\n");
			else  {
				printf("file %s exists, ", option1);
				printf("not overwriting\n");
			}
			exit(1);
		}

		dir = r;
		if (createfile(f, &dir, option1, 0, &directory, &index)) {
			printf("cannot create directory %s\n", option1);
			exit(1);
		}
		fatentrysetattributes(directory, index, 0x10);
		next = fatclusterfindfreebetween(f, afirst, alast, -1);
		if (next == FAT_ERR) {
			printf("filesystem full\n");
			exit(1);
		}
		fatentrysetfirstcluster(directory, index, fatbits(f), next);
		fatsetnextcluster(f, next, FAT_EOF);

		cluster = fatclustercreate(f, next);
		for (index = 0; index < cluster->size / 32; index++)
			fatentryzero(cluster, index);

		fatentrysetshortname(cluster, 0, DOTFILE);
		fatentrysetfirstcluster(cluster, 0, fatbits(f), next);
		fatentrysetattributes(cluster, 0, 0x10);

		fatentrysetshortname(cluster, 1, DOTDOTFILE);
		fatentrysetfirstcluster(cluster, 1, fatbits(f),
			dir == r ? 0 : dir);
		fatentrysetattributes(cluster, 1, 0x10);
	}
	else if (! strcmp(operation, "directoryclean")) {
		if (fileoptiontoreference(f,
			option1[0] == '\0' ? "/" : option1,
			&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (! fatreferenceisdirectory(directory, index, previous)) {
			printf("not a directory: %s\n", option1);
			exit(1);
		}
		recur = ! strcmp(option2, "recur") ||
			! strcmp(option3, "recur");
		testonly = ! strcmp(option2, "test") ||
			! strcmp(option3, "test");
		printf("%s unused directory clusters\n",
			testonly ? "finding" : "cleaning");
		directoryclean(f, directory, index, previous, recur, testonly);
		printf("%s unused directory entries\n",
			testonly ? "finding" : "cleaning");
		directorylast(f, directory, index, previous, recur, testonly);
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
	else if (! strcmp(operation, "recover")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		useshortnames = 1;
		nostoragepaths = 1;
		if (! strchr(option1, ':'))
			option1[0] = 0xE5;
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (option2[0] != '\0')
			size = atoi(option2);
		else if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot recover by cluster ");
			printf("only, provide a size\n");
			exit(1);
		}
		else
			size = fatentrygetsize(directory, index);
		for (cl = previous > 0 ? previous : target;
		     size > 0;
		     cl = fatclusterfindfreebetween(f,
				FAT_FIRST, fatlastcluster(f),
				fatclusterintervalnext(f, cl,
					FAT_FIRST, fatlastcluster(f)))) {
			cluster = fatclusterread(f, cl);
			if (cluster == NULL)
				break;
			fwrite(fatunitgetdata(cluster), 1,
				size > cluster->size ? cluster->size : size,
				stdout);
			size -= cluster->size;
		}
	}
	else if (! strcmp(operation, "undelete")) {
		if (option1[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		useshortnames = 1;
		nostoragepaths = 1;
		if (! strchr(option1, ':')) {
			firstchar = option1[0];
			option1[0] = 0xE5;
		}
		else
			firstchar = 'X';
		if (fileoptiontoreference(f, option1,
				&directory, &index, &previous, &target)) {
			printf("file %s does not exists\n", option1);
			exit(1);
		}
		if (fatreferenceisvoid(directory, index, previous)) {
			printf("cannot undelete by cluster\n");
			exit(1);
		}
		seconddirectory = directory;
		secondindex = index;
		size = fatentrygetsize(directory, index);
		for (next = target;
		     size > 0;
		     next = fatclusterfindfreebetween(f,
				FAT_FIRST, fatlastcluster(f),
				fatclusterintervalnext(f, target,
					FAT_FIRST, fatlastcluster(f)))) {
			cluster = fatclusterread(f, next);
			if (cluster == NULL)
				break;
			size -= cluster->size;
			fatreferencesettarget(f,
				directory, index, previous, next);
			fatreferencenext(f, &directory, &index, &previous);
			fatsetnextcluster(f, next, FAT_EOF);
		}
		fatentryfirst(seconddirectory, secondindex, firstchar);
	}
	else if (! strcmp(operation, "cross")) {
		if (option1[0] == '\0') {
			printf("missing argument: device\n");
			exit(1);
		}
		if (option2[0] == '\0') {
			printf("missing argument: file\n");
			exit(1);
		}
		cross = signature ?
			fatsignatureopen(option1, offset) :
			fatopen(option1, offset);
		if (cross == NULL) {
			printf("cannot open %s as a FAT filesystem\n", name);
			exit(1);
		}
		if (fileoptiontoreference(f, option2,
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
			cluster = fatclusterread(cross, cl);
			if (cluster == NULL)
				break;
			res = write(1, fatunitgetdata(cluster),
				chain || size > cluster->size ?
					cluster->size : size);
			size -= cluster->size;
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

