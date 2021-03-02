/*
 * reference.h
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
 * reference.h
 *
 * a reference to a cluster
 *
 * is whatever in the filesystem _points_ to a cluster; is necessary to change
 * the chains of clusters (for example: to add clusters at the end of a file,
 * to swap clusters, etc)
 *
 * is a triple cluster,index,number:
 *
 * - cluster,index,* is a directory entry;
 *   it points to the first cluster of the file
 *
 * - NULL,*,number is a cluster;
 *   it points to its next cluster according to the file allocation table
 *
 * - NULL,*,-1 is the boot sector;
 *   it points to the first cluster of the root directory:
 *   always FAT_ROOT (=1) for fat12/fat16, usually 2 (can be changed) for fat32
 *
 * - NULL,*,0 is the null reference;
 *   it points to no cluster
 *
 * many operations are implemented atop a function that calls another function
 * on every cluster reference in the filesystem
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "reference.h"
#include "directory.h"
#include "entry.h"
#include "table.h"
#include "unit.h"

int fatreferencedebug = 0;
#define dprintf if (fatreferencedebug) printf

/*
 * get and set the cluster that is the target of a reference
 */

int32_t fatreferencegettarget(fat *f,
		unit *directory, int index, int32_t previous) {
	if (directory != NULL)
		return fatentrygetfirstcluster(directory, index, fatbits(f));
	else if (previous == -1)
		return fatgetrootbegin(f);
	else if (previous >= FAT_FIRST)
		return fatgetnextcluster(f, previous);
	else
		return FAT_ERR;
}

int fatreferencesettarget(fat *f,
		unit *directory, int index, int32_t previous, int32_t new) {
	if (directory != NULL && new == FAT_EOF)
		new = FAT_UNUSED;

	if (directory != NULL)
		return fatentrysetfirstcluster(directory, index, f->bits, new);
 	else if (previous == -1)
		return fatsetrootbegin(f, new);
	else
		return fatsetnextcluster(f, previous, new);
}

/*
 * check conditions on a cluster reference
 */

int fatreferenceiscluster(unit *directory, int __attribute__((unused)) index,
		int32_t previous) {
	return directory == NULL && previous > 0;
}

int fatreferenceisentry(unit *directory, int __attribute__((unused)) index,
		int32_t __attribute__((unused)) previous) {
	return directory != NULL;
}

int fatreferenceisboot(unit *directory, int __attribute__((unused)) index,
		int32_t previous) {
	return directory == NULL && previous == -1;
}

int fatreferenceisvoid(unit *directory, int __attribute__((unused)) index,
		int32_t previous) {
	return directory == NULL && previous == 0;
}

/*
 * check if a cluster reference points to the directory entry of a directory
 */

int fatreferenceisdirectory(unit *directory, int index, int32_t previous) {
	if (fatreferenceisboot(directory, index, previous))
		return 1;
	if (directory != NULL && fatentryisdirectory(directory, index))
		return 1;
	return 0;
}

/*
 * check whether a reference points to a dot or dotdot file
 */
int fatreferenceisdotfile(unit *directory, int index,
		int32_t __attribute__((unused)) previous) {
	return directory != NULL && fatentryisdotfile(directory, index);
}

/*
 * advance a cluster reference
 */
int32_t fatreferencenext(fat *f,
		unit **directory, int *index, int32_t *previous) {
	*previous = fatreferencegettarget(f, *directory, *index, *previous);
	*directory = NULL;
	*index = 0;
	return fatreferencegettarget(f, *directory, *index, *previous);
}

/*
 * follow a chain of clusters to its end; return lenght of chain in clusters
 */
int fatreferencelast(fat *f,
		unit **directory, int *index, int32_t *previous) {
	int32_t cl;
	int n;

	n = 0;
	for (cl = fatreferencegettarget(f, *directory, *index, *previous);
	     cl >= FAT_FIRST;
	     cl = fatreferencenext(f, directory, index, previous))
		n++;
	return n;
}

/*
 * print a cluster reference
 */

void fatreferenceprint(unit *directory, int index, int32_t previous) {
	if (directory != NULL) {
		printf("[");
		fatentryprint(directory, index);
		// printf(" %d %d", directory->n, index);
		printf("]");
	}
	else if (previous == -1)
		printf("[boot sector]");
	else if (previous == 0)
		printf("[void]");
	else
		printf(" %d", previous);
}

/*
 * move a cluster to a different position
 *
 * the cluster to be moved is identified by its reference
 * the destination position is the number of a free cluster
 *
 * the cluster itself is not changed other than cluster->n; if a pointer to
 * this cluster has been obtained before calling this function, it is still
 * valid (see README)
 */

int fatclustermove(fat *f,
		unit *directory, int index, int32_t previous, int32_t new,
		int writeback) {
	int32_t current, next;
	unit *cluster;

			/* find and check source and target of move */

	current = fatreferencegettarget(f, directory, index, previous);
	if (current < FAT_FIRST)
		return -1;

	next = fatgetnextcluster(f, current);
	if (next == FAT_BAD)
		printf("warning: moving bad cluster %d\n", current);

	if (fatgetnextcluster(f, new) == FAT_BAD)
		printf("warning: moving to bad cluster %d\n", new);

	if (fatgetnextcluster(f, new) != FAT_UNUSED)
		return -1;

			/* move the cluster itself */

	cluster = fatclusterread(f, current);
	if (cluster == NULL)
		return -2;
	fatunitmove(&f->clusters, cluster, new);
	if (writeback)
		if (fatunitwriteback(cluster)) {
			fatunitmove(&f->clusters, cluster, current);
			return -5;
		}

			/* change the references */

	fatreferencesettarget(f, directory, index, previous, new);
	fatsetnextcluster(f, new, next);
	if (new != current)
		fatsetnextcluster(f, current, FAT_UNUSED);

	return 0;
}

/*
 * swap clusters
 *
 * same notes as above
 */

int fatclusterswap(fat *f,
		unit *dfirst, int ifirst, int32_t pfirst,
		unit *dsecond, int isecond, int32_t psecond,
		int writeback) {
	int32_t numfirst, nextfirst;
	unit *clusterfirst;
	int32_t numsecond, nextsecond;
	unit *clustersecond;

			/* find position of targets and their successors */

	numfirst = fatreferencegettarget(f, dfirst, ifirst, pfirst);
	if (numfirst < FAT_FIRST)
		return -1;
	nextfirst = fatgetnextcluster(f, numfirst);
	if (nextfirst == FAT_BAD)
		printf("warning: swapping bad cluster %d\n", numfirst);

	numsecond = fatreferencegettarget(f, dsecond, isecond, psecond);
	if (numsecond < FAT_FIRST)
		return -1;
	nextsecond = fatgetnextcluster(f, numsecond);
	if (nextsecond == FAT_BAD)
		printf("warning: swapping bad cluster %d\n", numsecond);

			/* special case: consecutive clusters in a chain */

	if (nextfirst == numsecond)
		nextfirst = numfirst;
	if (nextsecond == numfirst)
		nextsecond = numsecond;

			/* swap the clusters themselves */

	clusterfirst = fatclusterread(f, numfirst);
	if (clusterfirst == NULL)
		return -2;
	clustersecond = fatclusterread(f, numsecond);
	if (clustersecond == NULL)
		return -4;
	fatunitswap(&f->clusters, clusterfirst, clustersecond);
	if (writeback) {
		if (fatunitwriteback(clusterfirst)) {
			fatunitswap(&f->clusters, clusterfirst, clustersecond);
			return -5;	// write was on cluster numsecond
		}
		if (fatunitwriteback(clustersecond)) {
			fatunitswap(&f->clusters, clusterfirst, clustersecond);
			fatunitwriteback(clustersecond);
			return -3;
		}
	}

			/* change the references */

	fatreferencesettarget(f, dfirst, ifirst, pfirst, numsecond);
	fatreferencesettarget(f, dsecond, isecond, psecond, numfirst);

	fatsetnextcluster(f, numsecond, nextfirst);
	fatsetnextcluster(f, numfirst, nextsecond);

	return 0;
}

/*
 * change directory
 *
 * if the current directory is directory,index,previous this function changes
 * it like 'cd name'
 */
int fatreferencecd(fat *f, char *name,
		unit **directory, int *index, int32_t *previous) {
	int32_t dir;

	if (*name == '\0')
		return 0;

	if (*name == '/') {
		*directory = NULL;
		*index = 0;
		*previous = -1;
		return 0;
	}

	if (! fatreferenceisdirectory(*directory, *index, *previous))
		return -1;
	dir = fatreferencegettarget(f, *directory, *index, *previous);
	if (dir == FAT_ERR)
		return -1;

	if (fatlookupfile(f, dir, name, directory, index))
		return -1;
	*previous = 0;
	return 0;
}

/*
 * follow the first part of a path
 *
 * if the current directory is directory,index,previous and the path is
 * 'FIRST/SOME/ELSE', it is like executing 'cd FIRST'
 */
int fatfollowfirst(fat *f, char **path,
		unit **directory, int *index, int32_t *previous) {
	char *slash, *first;

	dprintf("path: %s\n", *path);

	if (**path == '\0')
		return 0;

	slash = **path == '/' ? *path + 1 : strchr(*path, '/');
	first = slash == NULL ? strdup(*path) : strndup(*path, slash - *path);
	dprintf("first: %s\n", first);
	if (fatreferencecd(f, first, directory, index, previous)) {
		free(first);
		return -1;
	}
	free(first);

	*path = **path == '/' ? *path : slash ? slash : *path + strlen(*path);
	if (**path == '/') {
		if (! fatreferenceisdirectory(*directory, *index, *previous))
			return -1;
		while (**path == '/')
			(*path)++;
	}

	if (fatreferencedebug)
		fatreferenceprint(*directory, *index, *previous);
	dprintf("\tleft: %s\n", *path);

	return 0;
}

/*
 * follow a path as much as possible
 *
 * starting from directory,index,previous, move along "path" until the end
 * (return 0) or some part of the path does not match (return -1); the output
 * *left points to the start of the part of "path" that could not be followed,
 * possibly its terminator '\0' if it all could be followed
 *
 * the input directory,index,previous is ignored and taken to be NULL,0,-1 if
 * "path" begins with '/'
 *
 * this is the common functionality of fatlookuppath and fatcreatefile: the
 * first function could check whether the entire path matches (this function
 * returns 0) and ignores "left"; the second could use *left as the filename if
 * contains no slash and the function returns -1; in the same way, a function
 * can use it to create a sequence of directories
 *
 * not currently used by these other functions
 */
int fatfollowpath(fat *f, const char *path,
		char **left, unit **directory, int *index, int32_t *previous) {
	int res;

	*left = (char *) path;

	do {
		res = fatfollowfirst(f, left, directory, index, previous);
		dprintf("%d %s\n", res, *left);
	} while (res == 0 && **left != '\0');

	return res;
}

/*
 * execute a function on every cluster reference starting from some one
 * see reference.h for details about the function, below for examples
 */

int _fatreferenceexecute(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit *dirdirectory, int dirindex, int32_t dirprevious,
		refrun act, void *user) {
	int res;
	int32_t scan, next;
	unit *dir, *prevdir;
	int ind;
	int err, status = 0;

			/* if reference is a directory cluster, mark as used */

	if (directory != NULL)
		directory->refer++;

			/* call function on cluster reference */

	next = fatreferencegettarget(f, directory, index, previous);
	res = act(f,
		directory, index, previous,
		startdirectory, startindex, startprevious,
		dirdirectory, dirindex, dirprevious,
		0, user);
	scan = (res & FAT_REFERENCE_ORIG) ? next :
		fatreferencegettarget(f, directory, index, previous);

			/* longname parts and deleted entries end here */

	if (fatreferenceisentry(directory, index, previous) &&
			(! fatentryexists(directory, index) ||
			fatentryislongpart(directory, index)))
		goto endexecute;

			/* call on the rest of the chain */

	while ((res & FAT_REFERENCE_CHAIN) && scan >= FAT_ROOT) {
		next = fatgetnextcluster(f, scan);
		res = act(f,
			NULL, 0, scan,
			directory, index, previous,
			startdirectory, startindex, startprevious,
			0, user);
		scan = (res & FAT_REFERENCE_ORIG) ? next :
			fatgetnextcluster(f, scan);
	}

			/* check whether to call recursively */

	if (! (res & FAT_REFERENCE_RECUR))
		goto endexecute;
	if (! fatreferenceisdirectory(directory, index, previous))
		goto endexecute;
	if (directory != NULL && fatentryisdotfile(directory, index))
		goto endexecute;

			/* directory: cycle over its entries */

	act(f,
		directory, index, previous,
		startdirectory, startindex, startprevious,
		dirdirectory, dirindex, dirprevious,
		1, user);

	next = fatreferencegettarget(f, directory, index, previous);
	if (next < FAT_ROOT)
		goto leavedir;

	dir = fatclusterread(f, next);
	if (dir == NULL) {
		status = -1;
		goto leavedir;
	}

	prevdir = dir;
	for (ind = -1; ! (err = fatnextentry(f, &dir, &ind)); prevdir = dir) {
		if ((res & FAT_REFERENCE_DELETE) &&
				prevdir != dir && prevdir->refer == 0) {
			fatunitwriteback(prevdir);
			fatunitdelete(&f->clusters, prevdir->n);
		}

		if (! (res & FAT_REFERENCE_ALL) &&
				(! fatentryexists(dir, ind) ||
				fatentryislongpart(dir, ind)))
			continue;

		err = _fatreferenceexecute(f,
				dir, ind, 0,
				directory, index, previous,
				startdirectory, startindex, startprevious,
				act, user);
		if (err) {
			status = -1;
			break;
		}
	}
	if ((res & FAT_REFERENCE_DELETE) && prevdir->refer == 0) {
		fatunitwriteback(prevdir);
		fatunitdelete(&f->clusters, prevdir->n);
	}
	if (err < -1) {
		printf("error in fatnextentry: %d\n", err);
		status = -1;
	}

leavedir:
	act(f,
		directory, index, previous,
		startdirectory, startindex, startprevious,
		dirdirectory, dirindex, dirprevious,
		-1, user);

			/* call again with direction = -2 */

	next = fatreferencegettarget(f, directory, index, previous);
	res = act(f,
		directory, index, previous,
		startdirectory, startindex, startprevious,
		dirdirectory, dirindex, dirprevious,
		-2, user);
	scan = (res & FAT_REFERENCE_ORIG) ? next :
		fatreferencegettarget(f, directory, index, previous);

	while ((res & FAT_REFERENCE_CHAIN) && scan >= FAT_FIRST) {
		next = fatreferencegettarget(f, NULL, 0, scan);
		res = act(f,
			NULL, 0, scan,
			directory, index, previous,
			startdirectory, startindex, startprevious,
			-2, user);
		scan = (res & FAT_REFERENCE_ORIG) ? next :
			fatreferencegettarget(f, NULL, 0, scan);
	}

			/* no longer this directory cluster is used here */

endexecute:
	if (directory != NULL)
		directory->refer--;

	return status;
}

int fatreferenceexecute(fat *f,
		unit *directory, int index, int32_t previous,
		refrun act, void *user) {
	return _fatreferenceexecute(f,
			directory, index, previous,
			directory, index, previous,
			directory, index, previous,
			act, user);
}

/*
 * trace the calls to fatreferenceexecute, for debugging
 */

int _fatcalls(fat __attribute__((unused)) *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit *dirdirectory, int dirindex, int32_t dirprevious,
		int direction, void *user) {

	FATEXECUTEDEBUG

	return FAT_REFERENCE_NORMAL |
		((* (int *) user) ? FAT_REFERENCE_ALL : 0);
}

void fatcalls(fat *f, int all) {
	int old;

	old = fatreferencedebug;
	fatreferencedebug = 1;
	fatreferenceexecute(f, NULL, 0, -1, _fatcalls, &all);
	fatreferencedebug = old;
}

/*
 * dump a filesystem summary
 */

struct fatdumpstruct {
	int level;
	int recur;
	int all;
	int clusters;
	int consecutive;
	int32_t chain;
};

int _fatdump(fat __attribute__((unused)) *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	int i;
	struct fatdumpstruct *s;
	int32_t target;

	s = (struct fatdumpstruct *) user;

	switch (direction) {
	case 1:
		s->level++;
		return 0;
	case -1:
		s->level--;
		return 0;
	case -2:
		return 0;
	default:
		if (directory != NULL) {
			if (s->all)
				fatentryprintpos(directory, index, 10);
			for (i = 0; i < s->level; i++)
				printf("    ");
		}

		target = fatreferencegettarget(f, directory, index, previous);

		if (s->chain == FAT_ERR - 1)
			fatreferenceprint(directory, index, previous);
		else if (fatreferenceiscluster(directory, index, previous)) {
			s->clusters++;
			if (target != previous + 1) {
				if (previous == s->chain)
					printf(" %d", previous);
				else
					printf(" %d-%d", s->chain, previous);
				s->chain = target;
				s->consecutive++;
			}
		}
		else {
			fatreferenceprint(directory, index, previous);
			s->chain = target;
			s->clusters = 0;
			s->consecutive = 0;
		}

		if (target == FAT_EOF || 
		    target == FAT_UNUSED ||
		    target == FAT_ERR ||
		    (directory != NULL &&
		     ! fatentryexists(directory, index) &&
		     s->all)) {
			if (s->chain == FAT_ERR - 1)
				printf("\n");
			else
				printf(" (%d/%d)\n",
					s->consecutive, s->clusters);
		}
		return FAT_REFERENCE_COND(s->recur) |
			(s->all ? FAT_REFERENCE_ALL : 0);
	}
}

void fatdump(fat *f, unit* directory, int index, int32_t previous,
		int recur, int all, int chains) {
	struct fatdumpstruct s;

	s.level = 0;
	s.recur = recur;
	s.all = all;
	s.clusters = 0;
	s.consecutive = 0;
	s.chain = chains == 1 ? FAT_EOF : FAT_ERR - 1;
	fatreferenceexecute(f, directory, index, previous, _fatdump, &s);
}

/*
 * count number of clusters
 */

struct fatcountclustersstruct {
	int32_t n;
	int recur;
};

int _fatcountclusters(fat __attribute__((unused)) *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	struct fatcountclustersstruct *s;
	if (direction != 0)
		return 0;
	if (fatreferenceisdotfile(directory, index, previous))
		return FAT_REFERENCE_DELETE;

	s = (struct fatcountclustersstruct *) user;

	if (fatreferenceiscluster(directory, index, previous))
		s->n++;

	return FAT_REFERENCE_COND(s->recur);
}

int32_t fatcountclusters(fat *f,
		unit *directory, int index, int32_t previous, int recur) {
	struct fatcountclustersstruct s;
	s.n = 0;
	s.recur = recur;
	if (fatreferenceexecute(f, directory, index, previous,
			_fatcountclusters, &s))
		return FAT_ERR;
	return s.n;
}

/*
 * fix the dot and dotdot files
 */

int _fatfixdot(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit *dirdirectory, int dirindex, int32_t dirprevious,
		int direction, void *user) {
	int32_t cl;

	if (fatreferencedebug)
		_fatdump(f, directory, index, previous,
			startdirectory, startindex, startprevious,
			dirdirectory, dirindex, dirprevious,
			direction, user);

	FATEXECUTEFIXDOT

	return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;
}

int fatfixdot(fat *f) {
	struct fatdumpstruct s;
	int res;

	s.level = 0;
	s.all = 0;
	res = fatreferenceexecute(f, NULL, 0, -1, _fatfixdot, &s);
	dprintf("\n");
	return res;
}

/*
 * cut the chains at FAT_BAD; call fatcleanunused() if they return 1
 */

struct cutbadstruct {
	int cut;	// was some chain cut?
	int len;	// length of current file, in clusters
	int verbose;	// print which files are changed
};

int _fatcutbad(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	struct cutbadstruct *c;
	int32_t target;

	if (direction != 0)
		return 0;

	c = (struct cutbadstruct *) user;

	if (directory != NULL)
		c->len = 1;
	else
		c->len++;

	target = fatreferencegettarget(f, directory, index, previous);
	if (target < FAT_FIRST)
		return FAT_REFERENCE_RECUR;

	if (fatgetnextcluster(f, target) != FAT_BAD)
		return FAT_REFERENCE_NORMAL;

	fatreferencesettarget(f, directory, index, previous, FAT_EOF);

	if (directory != NULL) {
		if (fatentryisdirectory(directory, index)) {
			if (c->verbose) {
				printf("deleting dir ");
				fatentryprintshortname(directory, index);
				printf("\n");
			}
			// a directory cannot be empty
			fatentrydelete(directory, index);
		}
		else {
			if (c->verbose) {
				printf("emptying file ");
				fatentryprintshortname(directory, index);
				printf("\n");
			}
			fatentrysetsize(directory, index, 0);
		}
	}
	else if (startdirectory != NULL &&
			! fatentryisdirectory(startdirectory, startindex)) {
		if (c->verbose) {
			printf("resizing ");
			fatentryprintshortname(startdirectory, startindex);
			printf(" to %d clusters\n", c->len - 1);
		}
		fatentrysetsize(startdirectory, startindex,
			fatbytespercluster(f) * (c->len - 1));
	}

	c->cut = 1;
	return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;
}

int fatcutbadstart(fat *f,
		unit *directory, int index, uint32_t previous) {
	struct cutbadstruct c;
	c.cut = 0;
	c.verbose = 0;
	if (fatreferenceexecute(f, directory, index, previous,
			_fatcutbad, &c))
		return -1;
	return c.cut;
}

int fatcutbad(fat *f, int verbose) {
	struct cutbadstruct c;
	c.cut = 0;
	c.verbose = verbose;
	if (fatreferenceexecute(f, NULL, 0, -1, _fatcutbad, &c))
		return -1;
	return c.cut;
}

/*
 * execute a function for every file
 */

#define MAX_PATH (4096 - 1)

struct fileexecutestruct {
	void *user;
	char path[MAX_PATH + 1];
	filerun act;
};

int _fatfileexecute(fat *f,
		unit *directory,
		int index,
		int32_t __attribute__((unused)) previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		int direction, void *user) {
	struct fileexecutestruct *s;
	char shortname[13];
	char *pos;

	if (directory == NULL && previous == -1)
		return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;

	if (directory == NULL)
		return 0;

	s = (struct fileexecutestruct *) user;

	if (direction == 1) {
		fatentrygetshortname(directory, index, shortname);
		strncat(s->path, shortname, MAX_PATH);
		strncat(s->path, "/", MAX_PATH);
		return 0;
	}
	if (direction == -1) {
		s->path[strlen(s->path) - 1] = '\0';
		pos = rindex(s->path, '/');
		if (pos == NULL)
			s->path[0] = '\0';
		else
			*(pos + 1) = '\0';
		return 0;
	}
	if (direction == -2)
		return 0;

	s->act(f, s->path, directory, index, s->user);

	return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;
}

int fatfileexecute(fat *f,
		unit *directory, int index, int32_t previous,
		filerun act, void *user) {
	struct fileexecutestruct s;

	s.user = user;
	s.path[0] = '\0';
	s.act = act;

	return fatreferenceexecute(f, directory, index, previous,
		_fatfileexecute, &s);
}

