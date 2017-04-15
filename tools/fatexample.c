/*
 * fatexample.c
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
 * fatexample.c
 *
 * examples from the man pages
 */

#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <llfat.h>

void printentry(fat *f, char *path, unit *directory, int index, void *user);

int main() {
	char *devicename = "fat12";
	fat *f;

	char *path;
	size_t len;
	wchar_t *wpath, *converted;
	unit *directory;
	int index;

	struct tm tm;

	uint32_t size;
	int32_t st, cl;
	unit *cluster;
	unsigned char *data;
	int i;

	int32_t previous, target, new;

	int32_t d;
	wchar_t *name;

	fatinverse *rev;

	char *spath, *sconverted;

	struct fatlongscan scan;
	int res;

	unit *startdirectory;
	int startindex;

			/* fat_lib.3: OPEN, FLUSH AND CLOSE A FILESYSTEM */

	f = fatopen(devicename, 0);
	if (f == NULL) {
		printf("cannot open %s as a FAT filesystem\n", devicename);
		printf("run testfs to create\n");
		exit(1);
	}
	if (fatcheck(f)) {
		printf("%s does't look like a FAT filesystem\n", devicename);
		printf("fix it (with dosfsck or similar) if it was\n");
		exit(1);
	}

				/* fat_lib.3: FIND A FILE */

	path = strdup("fs.c");
	len = mbstowcs(NULL, path, 0);
	if (len == (size_t) -1) {
		printf("invalid string\n");
		exit(1);
	}
	wpath = malloc((len + 1) * sizeof(wchar_t));
	mbstowcs(wpath, path, len + 1);
	if (fatinvalidpathlong(wpath) < 0) {
		printf("invalid path\n");
		exit(1);
	}
	converted = fatstoragepathlong(wpath);
	if (fatlookuppathlong(f, fatgetrootbegin(f),
			converted, &directory, &index)) {
		printf("file does not exist\n");
		exit(1);
	}
	printf("file found: %d,%d\n", directory->n, index);

			/* fat_lib.3: OPERATE ON A FILE */

	printf("file size: %d\n", fatentrygetsize(directory, index));
	if (fatentryisdirectory(directory, index))
		printf("is a directory\n");
	fatentrygetcreatetime(directory, index, &tm);
	printf("creation time: %s", asctime(&tm));
	fatentrysetreadtimenow(directory, index);	// update read time

			/* fat_lib.3: READ THE CONTENT OF A FILE */

	size = fatentrygetsize(directory, index);
	st = fatentrygetfirstcluster(directory, index, fatbits(f));

	for (cl = st; cl >= FAT_ROOT; cl = fatgetnextcluster(f, cl)) {
		cluster = fatclusterread(f, cl);
		data = fatunitgetdata(cluster);

		for (i = 0; i < cluster->size && (uint32_t) i < size; i++)
			printf(" %c", data[i]);

		data[0] = 'X';
		cluster->dirty = 1;
		fatunitwriteback(cluster);

		size = size > (uint32_t) cluster->size ?
			size - (uint32_t) cluster->size : 0;
	}
	printf("\n");

			/* fat_lib.3: APPEND CLUSTERS TO A FILE */

	new = fatclusterfindfree(f);

	previous = 0;
	target = fatreferencegettarget(f, directory, index, previous);
	while (target != FAT_EOF && target != FAT_UNUSED) {
		directory = NULL;
		index = 0;
		previous = target;
		target = fatreferencegettarget(f, directory, index, previous);
	}
	fatreferencesettarget(f, directory, index, previous, new);
	fatreferencesettarget(f, NULL, 0, new, FAT_EOF);

	previous = new;
	new = fatclusterfindfree(f);
	fatreferencesettarget(f, NULL, 0, previous, new);
	fatreferencesettarget(f, NULL, 0, new, FAT_EOF);

			/* fat_lib.3: CREATE A NEW FILE */

	wpath = wcsdup(L"new.txt");
	if (fatinvalidpathlong(wpath) < 0) {
		printf("invalid path\n");
		exit(1);
	}
	converted = fatstoragepathlong(wpath);

	fatcreatefilelongpath(f, fatgetrootbegin(f),
		converted, &directory, &index);

			/* fat_lib.3: SCAN A DIRECTORY */

	wpath = wcsdup(L"/aaa");
	d = fatlookuppathfirstclusterlong(f, fatgetrootbegin(f), wpath);
	directory = fatclusterread(f, d);

	for (index = 0;
	     ! fatnextname(f, &directory, &index, &name);
	     fatnextentry(f, &directory, &index)) {
		fatentryprint(directory, index);
		printf("\t%ls\n", name);

		if (fatentryisdirectory(directory,index)) {
			d = fatentrygetfirstcluster(directory, index,
				fatbits(f));
			printf("directory, first cluster %d\n", d);
		}

		free(name);
	}

			/* fat_lib.3: EXECUTING A FUNCTION ON EVERY FILE */

	fatfileexecute(f, NULL, 0, -1, printentry, NULL);

			/* fat_lib.3: INVERSE FAT */

	cl = 42;
	rev = fatinversecreate(f, 0);

	directory = NULL;
	index = 0;
	previous = cl;
	if (fatinversereferencetoentry(rev, &directory, &index, &previous))
		printf("cluster %d is in no file\n", cl);
	else {
		printf("cluster %d is in file: ", cl);
		fatentryprint(directory, index);
		printf("\n");
	}

	fatinversedelete(f, rev);

			/* fat_lib.3: Reading a cluster */

	// read cluster 45
	cluster = fatclusterread(f, 45);
	if (cluster == NULL) {
		printf("cluster 45 could not be read\n");
		exit(1);
	}

	// print content of cluster
	for (i = 0; i < cluster->size; i++)
		printf("%02X ", _unit8uint(cluster, i));
	printf("\n");

			/* fat_lib.3: Changing a cluster */

	_unit32int(cluster, 22) = -341245;	// 32-bit int at offset 22
	_unit16uint(cluster, 90) = 321;		// 16-bit unsigned int at 90
	cluster->dirty = 1;

	fatunitwriteback(cluster);
	fatunitdelete(&f->clusters, cluster->n);

			/* fat_lib.3: Creating a new cluster */

	cluster = fatclustercreate(f, 102);
	if (cluster == NULL) {
		// the cluster is already in cache, so it may be used
		// by some other part of the code; if overwriting is
		// not a problem:
		cluster = fatclusterread(f, 102);
		// otherwise, fail
	}
	if (cluster == NULL) {
		// cluster cannot be read/created
		// ...
	}
	_unit8int(cluster, 0) = 0x45;
	cluster->dirty = 1;
	fatunitwriteback(cluster);

			/* fat_lib.3: All clusters */

	for (cl = fatbits(f) == 32 ? FAT_FIRST : FAT_ROOT;
		cl <= fatlastcluster(f);
		cl++) {

		cluster = fatclusterread(f, cl);

		// operate on cluster
		// size is cluster->size
	}

			/* fat_functions.3: directory.h, fatnextentry() */

	wpath = wcsdup(L"/aaa");
	d = fatlookuppathfirstclusterlong(f, fatgetrootbegin(f), wpath);
	directory = fatclusterread(f, d);

	for (index = -1; ! fatnextentry(f, &directory, &index); ) {
		if (! fatentryexists(directory, index))
			continue;
		fatentryprint(directory, index);
		puts("");
	}

			/* fat_functions.3: directory.h, fatcreatefile() */

	d = fatgetrootbegin(f);

	spath = strdup("/aaa/new.txt");

	if (fatinvalidpath(spath)) {
		// path invalid
	}
	else {
		sconverted = fatstoragepath(spath);
		if (! fatlookuppath(f, d, sconverted, &directory, &index))
			printf("file exists\n");
		else if (fatcreatefile(f, d, sconverted, &directory, &index))
			printf("file cannot be created\n");
		else
			printf("file created\n");
		free(converted);
	}

			/* fat_functions.3: long.h, fatlongscan() */

	directory = fatclusterread(f, fatgetrootbegin(f));
	for (index = 0, fatlonginit(&scan);
	     (res = fatlongscan(directory, index, &scan)) != FAT_END;
	     fatnextentry(f, &directory, &index)) {
		printf("%d,%d\t", directory->n, index);
		if (res & FAT_SHORT) {
			fatentryprint(directory, index);
			printf("\t%ls", scan.name);
		}
		puts("");
	}
	fatlongend(&scan);
	puts("");

			/* fat_functions.3: long.h, fatlongnext() */

	directory = fatclusterread(f, fatgetrootbegin(f));
	for (index = 0;
	     (res = fatlongnext(f, &directory, &index,
		&startdirectory, &startindex, &name)) != FAT_END;
	     fatnextentry(f, &directory, &index)) {
		printf("%ls ", name);
		free(name);
	}
	puts("");

			/* fat_functions.3: long.h, fatnextname() */

	for (index = 0;
	     ! fatnextname(f, &directory, &index, &name);
	     fatnextentry(f, &directory, &index)) {
		printf("%ls ", name);
		free(name);
	}
	puts("");

			/* fat_lib.3: OPEN, FLUSH AND CLOSE A FILESYSTEM */

	fatflush(f);			// save filesystem
	if (1)
		fatclose(f);		// flush and close filesystem
	else
		fatquit(f);		// close without saving

	return 0;
}

/* fat_lib.3: EXECUTING A FUNCTION ON EVERY FILE */

void printentry(fat __attribute__((unused)) *f,
		char *path, unit *directory, int index,
		void __attribute__((unused)) *user) {
	printf("%-20s ", path);
	fatentryprint(directory, index);
	printf("\n");
}

