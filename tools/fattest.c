/*
 * fattest.c
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
 * fattest.c
 *
 * test the fat library
 */

#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __USE_UNIX98
#include <wchar.h>
#include <llfat.h>

/*
 * print the first max entries in a fat
 */
void fattabledump(fat *f, int nfat, int max) {
	int r, n;
	int prev;

	prev = f->nfat;
	f->nfat = nfat;

	printf("fat %d\n", nfat);
	for (r = 0; r < max; r++) {
		printf("next of cluster % 10d: ", r);
		n = fatgetnextcluster(f, r);
		if (n == -1)
			printf("***EOF\n");
		else
			printf("%-10d = 0x%X\n", n, n);
	}

	f->nfat = prev;
}

/*
 * print a file entry
 */
void fatprintentry(fat __attribute__((unused)) *f,
		char *path, unit *directory, int index,
		void __attribute__((unused)) *user) {
	printf("%-20s ", path);
	fatentryprint(directory, index);
	printf("\n");
}

/*
 * print a file entry with its path
 */
void fatprintentrylong(fat __attribute__((unused)) *f, wchar_t *path,
		unit *directory, int index,
		wchar_t *name, int err,
		unit __attribute__((unused)) *longdirectory,
		int __attribute__((unused)) longindex,
		void __attribute__((unused)) *user) {
	char line[80];
	snprintf(line, 50, "%ls%ls", path, name);
	printf("%-50.50s %s", line, err ? "" : "ERR ");
	fatentryprint(directory, index);
	printf("\n");
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	char *filename;
	int test;
	fat *f;
	int r;
	unit *u, *v, *w;
	uint64_t origin;
	int size;
	unit *root, *cluster, *directory, *longdirectory;
	int index, longindex;
	char shortname[13], *path, *left;
	int32_t cl, freecluster, n, previous;
	int i;
	fatinverse *rev;
	int res;
	struct fatlongscan scan;
	wchar_t longname[1000];

	if (argn - 1 < 1) {
		printf("usage:\n\tfattest filename [test]\n");
		exit(1);
	}

	filename = argv[1];
	if (argn - 1 > 1)
		test = atoi(argv[2]);
	else
		test = -1;

	f = fatopen(filename, 0);
	if (f == NULL)
		return -1;

	fatsummary(f);

	r = fatgetrootbegin(f);
	printf("===========================================\n");

	switch (test) {
	case 0:
	case 1:
		printf("\n********* cache test\n");

		printf("initial state of sector cache:\n");
		fatunitdumpcache("sectors", f->sectors);

		printf("\nafter inserting two artificial sectors:\n");
		u = fatunitcreate(512);
		u->n = 100;
		strcpy((char *) fatunitgetdata(u), "example sector");
		fatunitinsert(&f->sectors, u, 0);

		w = fatunitcopy(u);
		w->n = 101;
		fatunitgetdata(w)[0] = 'X';
		fatunitinsert(&f->sectors, w, 0);
		fatunitdumpcache("sectors", f->sectors);

		printf("\nafter deleting the two:\n");
		u->dirty = 0;
		w->dirty = 0;
		fatunitdelete(&f->sectors, 100);
		fatunitdelete(&f->sectors, 101);
		fatunitdumpcache("sectors", f->sectors);

		printf("\ninserting three different ones:\n");
		u = fatunitcreate(512);
		u->n = 100;
		strcpy((char *) fatunitgetdata(u), "further sector");
		fatunitinsert(&f->sectors, u, 0);

		v = fatunitcopy(u);
		v->n = 101;
		strcpy((char *) fatunitgetdata(v), "some other sector");
		fatunitinsert(&f->sectors, v, 0);

		w = fatunitcopy(u);
		w->n = 102;
		strcpy((char *) fatunitgetdata(w), "yet another sector");
		fatunitinsert(&f->sectors, w, 0);
		fatunitdumpcache("sectors", f->sectors);

		printf("\nafter freeing data:\n");
		u->dirty = 0;
		v->dirty = 0;
		w->dirty = 0;
		fatunitfreecache(f->sectors);
		fatunitdumpcache("sectors", f->sectors);

		printf("\nafter inserting one and reading a real other:\n");
		u = fatunitcreate(512);
		u->n = 103;
		strcpy((char *) fatunitgetdata(u), "back again sector");
		fatunitinsert(&f->sectors, u, 0);
		v = fatunitget(&f->sectors, 0,
			fatgetbytespersector(f), 104, f->fd);
		fatunitdumpcache("sectors", f->sectors);

		printf("\nafter freeing data of the real one:\n");
		fatunitfreecache(f->sectors);
		fatunitdumpcache("sectors", f->sectors);

		printf("\nafter reading back data of 104:\n");
		fatunitgetdata(v);
		fatunitdumpcache("sectors", f->sectors);

		printf("\nexpect error on sectors on exit\n\n");

		break;

	case 2:
		printf("\n********* fat loading test\n");

		fattabledebug = 1;
		fatreadfat(f, 0);
		fatreadfat(f, 1);
		fatreadfat(f, FAT_ALL);
		fattabledebug = 0;

		break;

	case 3:
		printf("\n********* fat test\n");

		/* test for 12-bit fat: change first 3 entries in first fat */
		f->nfat = 0;
		fatgetnextcluster(f, 2);
		fatunitdumpcache("sectors", f->sectors);
		fatsetnextcluster(f, 2, 0xABC);
		fatunitdumpcache("sectors", f->sectors);
		fatsetnextcluster(f, 4, 0x123);
		fatunitdumpcache("sectors", f->sectors);
		fatsetnextcluster(f, 3, 0xDEF);
		fatunitdumpcache("sectors", f->sectors);
		break;

		/* change first fat, second, both */
		fattabledump(f, 0, 30);
		fatsetnextcluster(f, 5, 20);
		f->nfat = 1;
		fatsetnextcluster(f, 10, 20);
		f->nfat = FAT_ALL;
		fatsetnextcluster(f, 12, 20);
		fattabledump(f, 0, 30);
		fattabledump(f, 1, 30);

		printf("\nfilesystem now contains errors\n\n");

		break;

	case 4:
		printf("\n********* cluster test\n");

		/* read the cluster of the root directory */
		fatclusterposition(f, r, &origin, &size);
		root = fatunitget(&f->clusters, origin, size, r, f->fd);

		/* read cluster 2 and 3 with the single function */
		fatclusterread(f, 2);
		fatclusterread(f, 3);

		fatunitdumpcache("clusters", f->clusters);
		printf("expect error on deleting uncached cluster 4\n");
		if (fatunitdelete(&f->clusters, 2))
			printf("error deleting cluster 2\n");
		if (fatunitdelete(&f->clusters, 4))
			printf("error deleting cluster 4\n");
		fatunitdumpcache("clusters", f->clusters);

		break;

	case 5:
		printf("\n********* free clusters map\n");

		for (cl = 2; cl <= fatlastcluster(f); cl++)
			if (fatgetnextcluster(f, cl) == FAT_UNUSED)
				printf("%4d ", cl);
			else
				printf("   . ");
		printf("\n");

		break;

	case 6:
		printf("\n********* free cluster test\n");

		fattabledebug = 1;
		freecluster = fatclusterfindfree(f);
		printf("free cluster: %d\n", freecluster);
		fatsetnextcluster(f, freecluster, FAT_EOF);
		freecluster = fatclusterfindfree(f);
		printf("free cluster: %d\n", freecluster);

		freecluster = fatlastcluster(f);
		fatsetnextcluster(f, freecluster, FAT_EOF);
		f->last = freecluster;
		freecluster = fatclusterfindfree(f);
		printf("free cluster: %d\n", freecluster);

		fattabledebug = 0;
		if (fatbits(f) == 32)
			printf("free clusters: %d %d\n",
				f->free, fatclusternumfree(f));

		break;

	case 7:
		printf("\n********* cluster zone test\n");

		fattabledebug = 1;

		printf("number of free clusters: %d\n",
			fatclusternumfree(f));
		printf("\n");
		printf("number of free clusters between 100 and 200: %d\n",
			fatclusternumfreebetween(f, 100, 200));
		printf("\n");
		printf("number of free clusters between 240 and 20: %d\n",
			fatclusternumfreebetween(f, 240, 20));
		printf("\n");

		printf("free cluster: %d\n\n",
			fatclusterfindfree(f));

		freecluster =
			fatclusterfindfreebetween(f, 100, 190, 200);
		printf("free between 100 and 190 starting at 200: %d\n\n",
			freecluster);
		freecluster =
			fatclusterfindfreebetween(f, 100, 190, -1);
		printf("free between 100 and 190 starting at -1: %d\n\n",
			freecluster);
		fatsetnextcluster(f, freecluster, FAT_EOF);
		freecluster = 
			fatclusterfindfreebetween(f, 100, 190, -1);
		printf("free between 100 and 190 starting at -1: %d\n\n",
			freecluster);
		printf("free between 100 and 190 starting at 20: %d\n\n",
			fatclusterfindfreebetween(f, 100, 190, 20));
		printf("free between 100 and 190 starting at 180: %d\n\n",
			fatclusterfindfreebetween(f, 100, 190, 180));
		printf("free between 100 and 110 starting at 100: %d\n\n",
			fatclusterfindfreebetween(f, 100, 110, 100));
		printf("free between 100 and 110 starting at 105: %d\n\n",
			fatclusterfindfreebetween(f, 100, 110, 105));
		fatsetnextcluster(f, 248, FAT_EOF);
		printf("free between 240 and 100 starting at 248: %d\n\n",
			fatclusterfindfreebetween(f, 240, 100, 248));
		
		printf("free between 3 and 8 starting at -1: %d\n\n",
			fatclusterfindfreebetween(f, 3, 8, -1));

		break;

	case 8:
		printf("\n********* cluster free area test\n");

		fattabledebug = 1;
		printf("find allocated between 2 and 5: %d\n\n",
			fatclusterfindallocated(f, 2, 5));
		printf("find allocated between 5 and 10: %d\n\n",
			fatclusterfindallocated(f, 5, 10));
		printf("find allocated between 240 and 9: %d\n\n",
			fatclusterfindallocated(f, 240, 9));
		printf("find allocated between 240 and 9, "
			"starting at 2: %d\n\n",
			fatclusterfindallocatedbetween(f, 240, 9, 2)
			);


		break;

	case 9:
		printf("\n********* fat dump test\n");

		fatdump(f, NULL, 0, -1, 1, 1);

		break;

	case 10:
		printf("\n********* directory test\n");

		/* read directory entries */
		root = fatclusterread(f, r);
		printf("entry 0 exists? %d\n", fatentryexists(root, 0));
		printf("entry 9 exists? %d\n", fatentryexists(root, 9));

		fatentrygetshortname(root, 9, shortname);
		printf("entry 9 short name: %s\n", shortname);
		
		fatentrysetshortname(root, 9, "SAMPLEEE.ABC");
		printf("entry 9 short name: ");
		fatentryprintshortname(root, 9);
		printf("\n");
		printf("------------------------\n");

		fatdump(f, NULL, 0, -1, 1, 1);
		printf("------------------------\n");

		fatentrysetfirstcluster(root, 6, fatbits(f), 0x2AB00);
		printf("first cluster: set %X, get %X\n", 0x2AB00,
			fatentrygetfirstcluster(root, 6, fatbits(f)));
		fatdump(f, NULL, 0, -1, 1, 1);
		printf("------------------------\n");

		printf("\nnote: filesystem has errors now\n\n");

		break;

	case 11:
		printf("\n********* directory lookup test\n");

		fatdirectorydebug = 1;
		r = fatgetrootbegin(f);

		printf("\nlookup in a given directory:\n\n");

		printf("TABLE.C\n");
		if (fatlookupfile(f, r, "TABLE.C", &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		printf("AAA\n");
		if (fatlookupfile(f, r, "AAA", &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		printf("30\n");
		if (fatlookupfile(f, r, "30", &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		printf("lookup using a complete path\n\n");

		printf("UNIT.C\n");
		if (fatlookuppath(f, r, "UNIT.C", &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		printf("/UNIT.C\n");
		if (fatlookuppath(f, 95, "/UNIT.C", &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		path = "/AAA/CCC";
		printf("%s\n", path);
		if (fatlookuppath(f, 95, path, &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		path = "AAA/Z/MAKEFILE";
		printf("%s\n", path);
		if (fatlookuppath(f, r, path, &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		path = "/AAA/BBB/MAKEFILE";
		printf("%s\n", path);
		if (fatlookuppath(f, r, path, &u, &index))
			printf("not found\n\n");
		else
			printf("found: cluster=%d index=%d\n\n", u->n, index);

		printf("first cluster of file (not of directory entry): %d\n",
			fatlookuppathfirstcluster(f, r, path));
		printf("\n");

		break;

	case 12:
		printf("\n********* directory entry creation test\n");

		root = fatclusterread(f, r);
		index = -1;
		fattabledebug = 1;
		for (i = 0; i < 10000; i++) {
			if(fatfindfreeentry(f, &root, &index)) {
				printf("no more free directory entries\n");
				break;
			}

			printf("\nfirst available entry: %d,%d\n",
				root->n, index);

			sprintf(shortname, "%d", i);
			printf("creating file %s there\n", shortname);
			fatentrysetshortname(root, index, shortname);
			fatentrysetattributes(root, index, 0x20);
			fatentrysetfirstcluster(root, index, fatbits(f), 0);
			fatentrysetsize(root, index, 0);
		}

		break;

	case 13:
	case 14:
		printf("\n********* cluster move test\n");

		root = fatclusterread(f, r);
		fatdump(f, NULL, 0, -1, 1, 1);

		index = 0;
		while (! fatentryexists(root, index))
			fatnextentry(f, &root, &index);

		if (test == 13) {
			freecluster = fatclusterfindfree(f);
			printf("moving root/%d to %d\n", index, freecluster);
			fatclustermove(f, root, index, 0, freecluster, 1);
			freecluster = fatclusterfindfree(f);
			printf("moving next of 9 to %d\n", freecluster);
			fatclustermove(f, NULL, 0, 9, freecluster, 1);
		}
		else {
			printf("swapping root/%d with next of 10\n", index);
			fatclusterswap(f, root, index, 0, NULL, 0, 10, 1);
		}

		printf("----------------\n");
		fatdump(f, NULL, 0, -1, 1, 1);

		printf("the filesystem may have errors\n");

		fatflush(f);

		break;

	case 40:
		printf("\n********* cd test\n");

		path = "/AAA/BBB/XXX/YYY";
		for (i = 0; i < 5 ; i++) {
			printf("%-25.25s ", path);
			res = fatfollowfirst(f, &path,
				&directory, &index, &previous);
			printf("%-2d ", res);
			fatreferenceprint(directory, index, previous);
			printf(" left: |%s|\n", path);
		}
		printf("\n");

		path = "/AAA/BBB/NONE";
		for (i = 0; i < 5 ; i++) {
			printf("%-25.25s ", path);
			res = fatfollowfirst(f, &path,
				&directory, &index, &previous);
			printf("%-2d ", res);
			fatreferenceprint(directory, index, previous);
			printf(" left: |%s|\n", path);
		}
		printf("\n");

		path = "/AAA/BBB/MAKEFILE";
		for (i = 0; i < 5 ; i++) {
			printf("%-25.25s ", path);
			res = fatfollowfirst(f, &path,
				&directory, &index, &previous);
			printf("%-2d ", res);
			fatreferenceprint(directory, index, previous);
			printf(" left: |%s|\n", path);
		}
		printf("\n");

		path = "/";
		for (i = 0; i < 2 ; i++) {
			printf("%-25.25s ", path);
			res = fatfollowfirst(f, &path,
				&directory, &index, &previous);
			printf("%-2d ", res);
			fatreferenceprint(directory, index, previous);
			printf(" left: |%s|\n", path);
		}
		printf("\n");

		path = "////AAA////TESTFS";
		for (i = 0; i < 3 ; i++) {
			printf("%-25.25s ", path);
			res = fatfollowfirst(f, &path,
				&directory, &index, &previous);
			printf("%-2d ", res);
			fatreferenceprint(directory, index, previous);
			printf(" left: |%s|\n", path);
		}
		printf("\n");


		break;

	case 41:
		printf("\n********* path following test\n");

		path = "/AAA/BBB/XXX/YYY";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/AAA/CCC";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/AAA/CCC/";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/AAA/BBB/NONE";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/AAA/BBB/MAKEFILE";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/AAA/BBB/MAKEFILE/";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/AAA/BBB/MAKEFILE/ELSE";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/XXX";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "/AAA/XXX";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		path = "BBB";
		res = fatfollowpath(f, path,
			&left, &directory, &index, &previous);
		printf("%-25.25s %-2d ", path, res);
		fatreferenceprint(directory, index, previous);
		printf(" left: |%s|\n", left);

		break;

	case 15:
		printf("\n********* file execute test\n");

		fatfileexecute(f, NULL, 0, -1, fatprintentry, NULL);

		break;

	case 16:
		printf("\n********* fix dot files test\n");

		fatreferencedebug = 1;
		fatfixdot(f);

		break;

	case 17:
		printf("\n********* move area test\n");

		fatreferencedebug = 1;
		fatmovearea(f, 2, 50, 100, 150);

		break;

	case 18:
		printf("\n********* filesystem compaction test\n");

		fatcompact(f);

		break;

	case 19:
		printf("\n********* filesystem truncation test\n");

		fatreferencedebug = 1;
		fattruncate(f, 140);

		break;

	case 20:
		printf("\n********* file creation test\n");

		root = fatclusterread(f, r);
		index = -1;
		for (i = 0; i < 10000; i++)
			if(! fatfindfreeentry(f, &root, &index))
				break;

		size = -1; // how many clusters; -1 = fill the filesystem

		printf("creating file at root/%d\n", index);
		fatentrysetshortname(root, index, "NEW");
		fatentrysetattributes(root, index, 0x20);

		printf("clusters:");
		cl = fatclusterfindfree(f);
		if (cl == FAT_ERR)
			break;
		fatentrysetfirstcluster(root, index, fatbits(f), cl);
		fatsetnextcluster(f, cl, FAT_EOF);
		printf(" %d", cl);

		for (i = 0; i < size - 1 || size == -1; i++) {
			freecluster = fatclusterfindfree(f);
			if (freecluster == FAT_ERR)
				break;
			fatsetnextcluster(f, cl, freecluster);
			fatsetnextcluster(f, freecluster, FAT_EOF);
			printf(" %d", freecluster);
			cl = freecluster;
		}
		printf(" EOF\n");

		printf("size: %d clusters\n", i);
		fatentrysetsize(root, index,
			(i + 1) * fatgetbytespersector(f) *
			fatgetsectorspercluster(f));

		break;

	case 21:
	case 22:
	case 23:
		printf("\n********* inverse fat test\n");

		rev = fatinversecreate(f, 1); 
		if (test == 22)
			for (cl = 2; cl <= fatlastcluster(f); cl++) {
				fatinverseprint(f, rev, cl);
				printf("\n");
			}
		if (test == 21) {
			printf("checking the inverse fat\n");
			// introduce errors on purpuse, in first and last entry
			// rev[1].previous = 12;
			// rev[fatlastcluster(f)].previous = 12;
			fatinversecheck(f, rev, 0);
		}
		else {
			printf("deleting the inverse fat\n");
			fatinversedelete(f, rev);
			fatunitdumpcache("clusters", f->clusters);
		}

		break;

	case 24:
		printf("\n********* cluster to entry test\n");

		rev = fatinversecreate(f, 0);
		cl = 50;
		directory = NULL;
		index = 0;
		n = cl;
		if (fatinversereferencetoentry(rev, &directory, &index, &n))
			printf("cluster %d is not in any file\n", cl);
		else if (directory != NULL) {
			printf("entry of cluster %d ", cl);
			printf("is %d,%d\n", directory->n, index);
			printf("name: ");
			fatentryprintshortname(directory, index);
			printf("\n");
		}
		else
			printf("cluster %d is part of the root dir\n", cl);

		free(rev);
		break;

	case 25:
		printf("\n********* inverse fat update test\n");

		fatreferencedebug = 1;
		fatinversedebug = 1;
		rev = fatinversecreate(f, 0);
		fatinversecheck(f, rev, 0);

		printf("testing with isdir=-1, ");
		printf("which does not work in all cases; ");
		printf("better use 0 or 1\n");

				/* create an empty file */

		root = fatclusterread(f, r);
		index = -1;
		fatfindfreeentry(f, &root, &index);
		fatentrydelete(root, index);
		strcpy(shortname, "TEST");
		printf("creating file %s ", shortname);
		printf("at %d/%d\n", root->n, index);
		fatentrysetshortname(root, index, shortname);
		fatentrysetsize(root, index, 0);
		fatinversesettarget(f, rev, root, index, 0, FAT_UNUSED, -1);
		fatinversecheck(f, rev, 0);

				/* add a first cluster */

		freecluster = fatclusterfindfree(f);
		if (freecluster == FAT_ERR)
			break;
		fatinversesettarget(f, rev, root, index, 0, freecluster, -1);
		fatinversesettarget(f, rev, NULL, 0, freecluster,
			FAT_EOF, -1);

		cluster = fatclusterread(f, freecluster);
		fatentrysetsize(root, index,
			fatentrygetsize(root, index) + cluster->size);
		memset(fatunitgetdata(cluster), '.', cluster->size);
		strcpy((char *) fatunitgetdata(cluster), "this is a test");
		cluster->dirty = 1;
		fatinversecheck(f, rev, 0);

				/* add a second cluster */

		cl = freecluster;
		freecluster = fatclusterfindfree(f);
		if (freecluster == FAT_ERR)
			break;
		fatinversesettarget(f, rev, NULL, 0, cl, freecluster, -1);
		fatinversesettarget(f, rev, NULL, 0, freecluster,
				FAT_EOF, -1);

		cluster = fatclusterread(f, freecluster);
		fatentrysetsize(root, index,
			fatentrygetsize(root, index) + cluster->size);
		memset(fatunitgetdata(cluster), '.', cluster->size);
		strcpy((char *) fatunitgetdata(cluster), "this is a test");
		cluster->dirty = 1;
		fatinversecheck(f, rev, 0);

		// break;

				/* remove the second cluster */

		fatinversesettarget(f, rev, NULL, 0, cl,
			FAT_EOF, -1);
		fatinversesettarget(f, rev, NULL, 0, freecluster,
			FAT_UNUSED, -1);
		fatentrysetsize(root, index,
			fatentrygetsize(root, index) - cluster->size);
		fatinversecheck(f, rev, 0);

				/* remove the first */

		fatinversesettarget(f, rev, root, index, 0,
			FAT_UNUSED, -1);
		fatinversesettarget(f, rev, NULL, 0, cl,
			FAT_UNUSED, -1);
		fatentrysetsize(root, index,
			fatentrygetsize(root, index) - cluster->size);
		fatinversecheck(f, rev, 0);

		free(rev);
		break;

	case 26:
		printf("\n********* inverse fat move and swap test\n");

		fatreferencedebug = 1;
		fatinversedebug = 1;
		rev = fatinversecreate(f, 0);
		fatinversecheck(f, rev, 0);

		freecluster = fatclusterfindfree(f);
		printf("moving 12->%d\n", freecluster);
		fatinversemove(f, rev, 12, freecluster, 1);

		freecluster = fatclusterfindfree(f);
		printf("moving 18->%d\n", freecluster);
		fatinversemove(f, rev, 18, freecluster, 1);

		freecluster = fatclusterfindfree(f);
		printf("moving 24->%d\n", freecluster);
		fatinversemove(f, rev, 24, freecluster, 1);

		printf("swapping 34<->35\n");
		fatinverseswap(f, rev, 34, 35, 1);

		fatinversecheck(f, rev, 0);

		free(rev);
		break;

	case 27:
		printf("\n********* defragment test\n");

		fatreferencedebug = 1;
		fatinversedebug = 1;
		fatdefragment(f, 0, NULL);

		break;

	case 28:
	case 29:
	case 30:
		printf("\n********* one-step longname scan test\n");

		directory = fatclusterread(f, fatgetrootbegin(f));
		for (index = 0, fatlonginit(&scan);
		     (res = fatlongscan(directory, index, &scan)) != FAT_END;
		     fatnextentry(f, &directory, &index)) {
			if ((test == 30) && ! (res & FAT_SHORT))
				continue;
			printf("%d,%d\t", directory->n, index);
			if (res & FAT_SHORT || test == 28)
				fatentryprint(directory, index);
			else
				printf("\t\t\t");
			printf("\t%ls", scan.name);
			if (res & FAT_LONG_ALL)
				printf("\t(longname: %d,%d)",
					scan.longdirectory->n,
					scan.longindex);
			printf("\n");
		}

		break;

	case 31:
		printf("\n********* long filename lookup test\n");

		fatlongdebug = 1;

		if (fatlookupfilelong(f, r, L"directory.c",
				&directory, &index))
			printf("not found\n");
		else
			printf("found: %d,%d\n", directory->n, index);

		if (fatlookupfilelong(f, r, L"nonexistent.txt",
				&directory, &index))
			printf("not found\n");
		else
			printf("found: %d,%d\n", directory->n, index);

		break;

	case 32:
		printf("\n********* long directory name lookup test\n");

		fatlongdebug = 1;

		if (fatlookuppathlong(f, r, \
			L"alongdirectoryname/oneinsideit/alongfilename.text",
			&directory, &index))
			printf("not found\n");
		else
			printf("found: %d,%d\n", directory->n, index);

		break;

	case 33:
		printf("\n********* long name free entry test\n");

		fatlongdebug = 1;

		directory = fatclusterread(f, r);
		index = -1;
		if (fatfindfreelong(f, 14, &directory, &index,
				&longdirectory, &longindex))
			printf("not found\n");
		else
			printf("found: %d,%d\n", longdirectory->n, longindex);

		if (fatfindfreelongpath(f, r, L"/aaa", 14, &directory, &index,
				&longdirectory, &longindex))
			printf("not found\n");
		else
			printf("found: %d,%d\n", longdirectory->n, longindex);

		break;

	case 34:
		printf("\n********* long name creation test\n");

		fatlongdebug = 1;

		if (fatcreatefileshortlong(f, r,
				(unsigned char *) "ANAME   TXT", 0x10,
				L"alongfilenamehere.txt",
				&directory, &index,
				&longdirectory, &longindex))
			printf("cannot create file\n");
		else {
			printf("created: ");
			printf("%d,%d-", longdirectory->n, longindex);
			printf("%d,%d\n", directory->n, index);
		}

		printf("\n");

		if (fatcreatefilelongboth(f, r,
				L"longname.TXT",
				&directory, &index,
				&longdirectory, &longindex))
			printf("cannot create file\n");
		else {
			printf("created: ");
			printf("%d,%d-", longdirectory->n, longindex);
			printf("%d,%d\n", directory->n, index);
		}

		printf("\n");
		// break;

		for (i = 55; i < 82; i++) {
			swprintf(longname, 1000, 
					L"alongdirectoryname/"
					L"anotherfilestillquitesomehowlong"
					"%d.txt", i);
			if (fatcreatefilelongbothpath(f, r, longname,
					&directory, &index,
					&longdirectory, &longindex))
				printf("cannot create file\n");
			else {
				printf("created: ");
				printf("%d,%d-", longdirectory->n, longindex);
				printf("%d,%d\n", directory->n, index);
			}
		}

		break;

	case 35:
		printf("\n********* fatfilelong test\n");

		fatfileexecutelong(f, NULL, 0, -1, fatprintentrylong, NULL);

		break;

	case 36:
		printf("\n********* fat long inverse test\n");

		rev = fatinversecreate(f, 0);

		// directory = fatclusterread(f, 344);
		/*
		directory = fatclusterread(f, 512);
		index = 0;

		printf("%d,%d -> ", directory->n, index);
		res = fatinversepreventry(f, rev, &directory, &index);
		printf("%d,%d\t(%d)\n", directory->n, index, res);
		*/

		fatlookupfile(f, r, "ASOMEW~1.AND", &directory, &index);
		printf("%d,%d -> ", directory->n, index);
		res = fatshortentrytolong(f, rev, directory, index,
			&longdirectory, &longindex);
		if (res)
			printf("[no long name]\n");
		else
			printf("%d,%d\n", longdirectory->n, longindex);

		fatlookupfile(f, r, "COMPLEX.C", &directory, &index);
		printf("%d,%d -> ", directory->n, index);
		res = fatshortentrytolong(f, rev, directory, index,
			&longdirectory, &longindex);
		if (res)
			printf("[no long name]\n");
		else
			printf("%d,%d\n", longdirectory->n, longindex);

		printf("\n");

		directory = NULL;
		index = 0;
		cl = 42;
		fatreferenceprint(directory, index, cl);
		printf(" -> ");
		res = fatlongreferencetoentry(f, rev,
			&directory, &index, &cl, &longdirectory, &longindex);
		if (res == -2)
			printf("\tnot found\n");
		else {
			printf("\t");
			fatreferenceprint(directory, index, cl);
			if (res == -1)
				printf("\tno long name");
			else
				printf("\t%d,%d", longdirectory->n, longindex);
			printf("\n");
		}

		directory = NULL;
		index = 0;
		cl = 39;
		fatreferenceprint(directory, index, cl);
		printf(" -> ");
		res = fatlongreferencetoentry(f, rev,
			&directory, &index, &cl, &longdirectory, &longindex);
		if (res == -2)
			printf("\tnot found\n");
		else {
			printf("\t");
			fatreferenceprint(directory, index, cl);
			if (res == -1)
				printf("\tno long name");
			else
				printf("\t%d,%d", longdirectory->n, longindex);
			printf("\n");
		}

		directory = NULL;
		index = 0;
		cl = 1;
		fatreferenceprint(directory, index, cl);
		printf(" -> ");
		res = fatlongreferencetoentry(f, rev,
			&directory, &index, &cl, &longdirectory, &longindex);
		if (res == -2)
			printf("\tnot found\n");
		else {
			printf("\t");
			fatreferenceprint(directory, index, cl);
			if (res == -1)
				printf("\tno long name");
			else
				printf("\t%d,%d", longdirectory->n, longindex);
			printf("\n");
		}

		fatinversedelete(f, rev);

		break;
	}

	printf("===========================================\n");
	fatdebug = 1;
	fatunitdebug = 1;
	fatclose(f);

	return 0;
}

