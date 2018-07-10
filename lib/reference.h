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

#ifdef _REFERENCE_H
#else
#define _REFERENCE_H

#include "fs.h"

/*
 * next of a cluster reference (a pointer to a cluster): get and set
 */
int32_t fatreferencegettarget(fat *f,
		unit *directory, int index, int32_t previous);
int fatreferencesettarget(fat *f,
		unit *directory, int index, int32_t previous, int32_t new);

/*
 * check conditions on a cluster reference
 */

int fatreferenceiscluster(unit *directory, int index, int32_t previous);
int fatreferenceisentry(unit *directory, int index, int32_t previous);
int fatreferenceisdotfile(unit *directory, int index, int32_t previous);
int fatreferenceisboot(unit *directory, int index, int32_t previous);
int fatreferenceisvoid(unit *directory, int index, int32_t previous);

/*
 * advance a cluster reference
 */
int32_t fatreferencenext(fat *f,
		unit **directory, int *index, int32_t *previous);
int fatreferencelast(fat *f,
		unit **directory, int *index, int32_t *previous);

/*
 * check if a cluster reference points to the directory entry of a directory
 */
int fatreferenceisdirectory(unit *directory, int index, int32_t previous);

/*
 * print a cluster reference, for debugging
 */
void fatreferenceprint(unit *directory, int index, int32_t previous);

/*
 * cluster chain change
 */
int fatclustermove(fat *f,
		unit *directory, int index, int32_t previous,
		int32_t new,
		int writeback);
int fatclusterswap(fat *f,
		unit *dfirst, int ifirst, int32_t pfirst,
		unit *dsecond, int isecond, int32_t psecond,
		int writeback);

/*
 * follow the first part of a path or all of it as much as possible
 */
int fatfollowfirst(fat *f, char **path,
		unit **directory, int *index, int32_t *previous);
int fatfollowpath(fat *f, const char *path,
		char **left, unit **directory, int *index, int32_t *previous);

/*
 * execute a refrun function on every cluster _reference_
 * 
 * the refrun function is called with:
 * - the cluster reference
 * - a reference to the directory entry of the file containing the cluster;
 *   in other words: the target cluster is part of a chain that belongs to a
 *   file; this is a reference to the directory entry of this file
 * - a reference to the directory entry of the directory containing this file
 * - the direction, which distinguishes when the refrun function has been
 *   called, since it is called:
 *	. on every cluster reference, with direction=0
 *	. if the reference is a directory entry (that is, a file) of a
 *	  directory (that is, this file is a directory):
 *	  	_ with direction=1 before entering the directory
 *		_ with direction=-1 when leaving it
 *		_ with direction=-2 again on every cluster reference of the
 *		  chain
 * - a void * parameter, free for program use
 *
 * the return value of refrun tells:
 *	a. whether to follow the chain of clusters (FAT_REFERENCE_CHAIN)
 *	b. whether to recurse if this is a directory (FAT_REFERENCE_RECUR)
 *	c. whether to follow the original chain, even if the function changed
 *	   the next of the cluster (FAT_REFERENCE_ORIG)
 *	d. whether to call on all directory entries, including long name parts
 *	   and deleted entries (FAT_REFERENCE_ALL)
 */
#define FAT_REFERENCE_CHAIN  0x01
#define FAT_REFERENCE_RECUR  0x02
#define FAT_REFERENCE_ORIG   0x04
#define FAT_REFERENCE_ALL    0x08
#define FAT_REFERENCE_DELETE 0x10

#define FAT_REFERENCE_NORMAL \
	FAT_REFERENCE_CHAIN | FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE
#define FAT_REFERENCE_COND(r) \
	FAT_REFERENCE_CHAIN | FAT_REFERENCE_DELETE | \
	((r) ? FAT_REFERENCE_RECUR : 0)
#define FAT_REFERENCE_ABORT 0

typedef int(* refrun)(fat *f,
	unit *directory, int index, int32_t previous,
	unit *startdirectory, int startindex, int32_t startprevious,
	unit *dirdirectory, int dirindex, int32_t dirprevious,
	int direction, void *user);
int fatreferenceexecute(fat *f,
		unit *directory, int index, int32_t previous,
		refrun act, void *user);

/*
 * execute a function on every file
 */
typedef void (* filerun)(fat *f, char *path, unit *directory, int index,
		void *user);
int fatfileexecute(fat *f,
		unit *directory, int index, int32_t previous,
		filerun act, void *user);

/*
 * print a summary of the filesystem
 */
void fatdump(fat *f, unit* directory, int index, int32_t previous,
		int recur, int all, int chains);
void fatcalls(fat *f, int all);

/*
 * count number of clusters
 */
int32_t fatcountclusters(fat *f,
	unit *directory, int index, int32_t previous, int recur);

/*
 * fix the dot and dotdot files
 */
int fatfixdot(fat *f);

/*
 * cut the chains at FAT_BAD; call fatcleanunused() if they return 1
 */
int fatcutbadstart(fat *f,
	unit *directory, int index, uint32_t previous);
int fatcutbad(fat *f, int verbose);

/*
 * use in a refrun function for debugging
 */

#define FATEXECUTEDEBUG							\
	{								\
		fatreferenceprint(dirdirectory, dirindex, dirprevious);	\
		printf("->");						\
		fatreferenceprint(startdirectory, startindex, startprevious); \
		printf("->");						\
		fatreferenceprint(directory, index, previous);		\
		printf(" (%d)\n", direction);				\
		fflush(NULL);						\
	}

/*
 * use in a refrun function for fixing . and .. in case of cluster moves/swaps
 */

#define FATEXECUTEFIXDOT						\
	if (directory != NULL && fatentryisdotfile(directory, index)) {	\
		cl = fatentrycompareshortname(directory, index, ".") ?	\
			fatreferencegettarget(f,			\
				dirdirectory, dirindex, dirprevious) :	\
			fatreferencegettarget(f,			\
				startdirectory, startindex, startprevious); \
		cl = cl == fatgetrootbegin(f) ? 0 : cl;			\
		fatreferencesettarget(f,				\
			directory, index, previous, cl);		\
		dprintf(" (.%c->%d)", directory->data[index * 32 + 1], cl); \
		return 0;						\
	}

#endif

/*
 * use in a refrun to determine whether the reference is to a directory cluster
 */

#define FATEXECUTEISDIR							\
	(directory == NULL &&						\
	fatreferenceisdirectory(startdirectory, startindex, startprevious)) \
	||								\
	(directory != NULL &&						\
	fatreferenceisdirectory(directory, index, previous));

