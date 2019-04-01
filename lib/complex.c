/*
 * complex.c
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
 * complex.c
 *
 * operations that cannot be arbitrarily interrupted, so that handling signals
 * and IO errors is necessary
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "fs.h"
#include "table.h"
#include "entry.h"
#include "reference.h"
#include "inverse.h"
#include "complex.h"

int fatcomplexdebug = 0;
#define dprintf if (fatcomplexdebug) printf

/*
 * uninterruptible flush
 */

FATINTERRUPTIBLEGLOBAL(uflush);

int fatuflush(fat *f) {
	FATINTERRUPTIBLEINIT(uflush);
	fatflush(f);
	FATINTERRUPTIBLEFINISH(uflush);
	return FATINTERRUPTIBLECHECK(uflush) ? -1 : 0;
}

/*
 * move the clusters in an area to another, in cluster reference order
 * libllfat.txt: [Cluster reference order]
 */

FATINTERRUPTIBLEGLOBAL(movearea);

struct moveareastruct {
	/* clusters to be moved */
	int32_t srcbegin;
	int32_t srcend;

	/* where to move them */
	int32_t dstbegin;
	int32_t dstend;

	/* first cluster of a directory moved? */
	int dirmoved;
};

int _fatmovearea(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit *dirdirectory, int dirindex, int32_t dirprevious,
		int direction, void *user) {
	int32_t cl, target, dest;
	struct moveareastruct *s;

	if(FATINTERRUPTIBLECHECK(movearea))
		return 0;

	if (fatreferenceisboot(directory, index, previous) && fatbits(f) != 32)
		return FAT_REFERENCE_NORMAL;

	target = fatreferencegettarget(f, directory, index, previous);
	if (target < FAT_FIRST)
		return FAT_REFERENCE_NORMAL;
	if (fatgetnextcluster(f, target) == FAT_BAD) {
		fatreferencesettarget(f, directory, index, previous, FAT_EOF);
		return 0;
	}

	s = (struct moveareastruct *) user;
	if (fatcomplexdebug)
		FATEXECUTEDEBUG

			/* fix . and .. */

	FATEXECUTEFIXDOT

				/* cluster outside source area: do nothing */

	if (! fatclusterisbetween(target, s->srcbegin, s->srcend))
		return FAT_REFERENCE_NORMAL;

				/* destination of move: a free cluster */
	
	dest = fatclusterfindfreebetween(f, s->dstbegin, s->dstend, -1);
	if (dest == FAT_ERR)
		return 0;
	/* following is for filesystem compaction, harmless otherwise */
	if (fatclusterisbetween(target, s->dstbegin, s->dstend) &&
			dest > target)
		return FAT_REFERENCE_NORMAL;

				/* move target of reference to free cluster */

	if (fatcomplexdebug)
		fatreferenceprint(directory, index, previous);
	dprintf(" %d -> %d = ", target, dest);
	if(fatclustermove(f, directory, index, previous, dest, 1) < -1) {
		printf("IO error reading cluster %d\n", target);
		FATINTERRUPTIBLEABORT(movearea, FATINTERRUPTIBLEIOERROR);
		return 0;
	}
	dprintf("%d",
		fatreferencegettarget(f, directory, index, previous));
	dprintf("\n");

				/* delete cluseter from cache to save memory */

	fatunitdelete(&f->clusters, dest);

	if (fatreferenceisdirectory(directory, index, previous))
		s->dirmoved = 1;

	return FAT_REFERENCE_NORMAL;
}

int fatmovearea(fat *f,
		int32_t srcbegin, int32_t srcend,
		int32_t dstbegin, int32_t dstend) {
	struct moveareastruct s;
	int res;

	s.srcbegin = srcbegin;
	s.srcend = srcend;
	s.dstbegin = dstbegin;
	s.dstend = dstend;
	s.dirmoved = 0;

	f->last = dstbegin;

	FATINTERRUPTIBLEINIT(movearea);

	res = fatreferenceexecute(f, NULL, 0, -1, _fatmovearea, &s);

	if (FATINTERRUPTIBLECHECK(movearea) && s.dirmoved)
		fatfixdot(f);

	fatflush(f);
	FATINTERRUPTIBLEFINISH(movearea);
	return res;
}

/*
 * compact a filesystem by moving cluster at the beginning
 */
int fatcompact(fat *f) {
	return
	fatmovearea(f, FAT_FIRST, fatlastcluster(f), 2, fatlastcluster(f));
}

/*
 * truncate a filesystem to a certan number of clusters
 *
 * the complication is that when a cluster is over the bound, all subsequent
 * clusters, even in subdirectories, have to be cut as well
 *
 * solution: when a cluster is over the bound the reference to the current file
 * is saved; from this point on every cluster is freed (no matter if it is in
 * the same chain or in a subdirectory); this stops when the saved file ends
 *
 * this function does not save any change before the final flush; therefore,
 * only that needs to be non-interruptible
 */

struct truncatestruct {
	int bound;		/* delete all cluster over this bound */

	int num;		/* size (in clusters) of the current file */

	int32_t cutdirectory;	/* entry for the file where the cut begun */
	int32_t cutindex;

	int cuttype;		/* what is being cut */
};

int _fattruncate(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit *dirdirectory, int dirindex, int32_t dirprevious,
		int direction, void *user) {
	struct truncatestruct *s;
	int32_t target;
	uint32_t size;
	int prevcut;
	unit *cluster;

	if (directory == NULL && previous == -1)
		return FAT_REFERENCE_NORMAL | FAT_REFERENCE_ALL;

	s = (struct truncatestruct *) user;

	target = fatreferencegettarget(f, directory, index, previous);
	if (target < FAT_FIRST)
		return FAT_REFERENCE_NORMAL | FAT_REFERENCE_ALL;
	if (fatgetnextcluster(f, target) == FAT_BAD)
		fatreferencesettarget(f, directory, index, previous, FAT_EOF);
		// do not return here: a bad cluster means that the chain
		// ended, which may imply the end of a cut

			/* skip initial scan of directories */
	
	if (fatreferenceisdirectory(directory, index, previous) &&
			direction == 0)
		return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;

	if (fatcomplexdebug)
		FATEXECUTEDEBUG

			/* end of dir scanning: check if end of cut */

	if (direction == -1) {
		if (s->cutdirectory == directory->n &&
				s->cutindex == index) {
			printf("end of cut (type %d) of ", s->cuttype);
			cluster = fatclusterread(f, s->cutdirectory),
			fatreferenceprint(cluster, s->cutindex, 0);
			printf("\n\n");
			s->cuttype = 0;
		}
		return FAT_REFERENCE_NORMAL | FAT_REFERENCE_ALL;
	}

			/* initialize size */

	if (directory != NULL)
		s->num = 0;

			/* deallocate cluster */

	if (s->cuttype && previous >= FAT_FIRST) {
		printf("freeing cluster %d\n", previous);
		fatsetnextcluster(f, previous, FAT_UNUSED);
	}

	prevcut = s->cuttype;

			/* middle of a cluster chain */

	if (! s->cuttype &&
			target > s->bound &&
			directory == NULL &&
			(! fatreferenceisdirectory(startdirectory,
					startindex, startprevious) ||
				direction == -2)) {
		s->cutdirectory = startprevious == -1 ?
			FAT_ROOT : startdirectory->n;
		s->cutindex = startindex;
		s->cuttype = 1;
	}

			/* begin of a cluster chain */
		
	if (! s->cuttype &&
			target > s->bound &&
			directory != NULL &&
			fatentryexists(directory, index) &&
			(! fatreferenceisdirectory(directory, index, previous)
				|| direction == -2)) {
		s->cutdirectory = directory->n;
		s->cutindex = index;
		s->cuttype = 2;
	}

			/* directory entry */

	if (! s->cuttype &&
			fatreferenceisdirectory(directory, index, previous) &&
			directory->n > s->bound) {
		s->cutdirectory = startdirectory->n;
		s->cutindex = startindex;
		s->cuttype = 3;
	}

			/* cut point */

	if (!prevcut && s->cuttype) {
		printf("\nstart cut (type %d) ", s->cuttype);
		printf("at cluster %d of file ", target);
		fatreferenceprint(s->cutdirectory != FAT_ROOT ?
					fatclusterread(f, s->cutdirectory) :
					NULL,
				s->cutindex,
				s->cutdirectory == FAT_ROOT ? -1 : 0);
		printf(" (%d,%d)\n", s->cutdirectory, s->cutindex);

		printf("cutting reference ");
		fatreferenceprint(directory, index, previous);
		printf("\n");

		fatreferencesettarget(f, directory, index, previous, FAT_EOF);
	}

	if (fatreferenceisdirectory(directory, index, previous) &&
			! fatentryexists(directory, index))
		return FAT_REFERENCE_NORMAL |
			FAT_REFERENCE_ALL | FAT_REFERENCE_ORIG;

	if (fatreferenceisdirectory(directory, index, previous) &&
			fatentryisdotfile(directory, index))
		return 0;
	
			/* not cutting: end here */

	if (! s->cuttype) {
		s->num++;
		return FAT_REFERENCE_NORMAL | FAT_REFERENCE_ALL;
	}

			/* not the end of a file: end here */

	if (fatgetnextcluster(f, target) >= FAT_FIRST)
		return FAT_REFERENCE_NORMAL | FAT_REFERENCE_ORIG |
			FAT_REFERENCE_ALL;

			/* end of file: update size */

	printf("freeing cluster %d\n", target);
	fatsetnextcluster(f, target, FAT_UNUSED);
	size = s->num * fatgetbytespersector(f) * fatgetsectorspercluster(f);
	fatentrysetsize(fatclusterread(f, s->cutdirectory), s->cutindex, size);

			/* end of root on fat32 */

	if (directory == NULL && startprevious == -1 && direction == -2)
		return 0;

			/* check end of cut */

	if (s->cuttype <= 2 && (
			/* first cluster is also last */
			(directory != NULL &&
				s->cutdirectory == directory->n &&
				s->cutindex == index)
			||
			/* otherwise */
			(directory == NULL &&
				s->cutdirectory == startdirectory->n &&
				s->cutindex == startindex)
			)) {
		printf("end of cut (type %d) of ", s->cuttype);
		cluster = fatclusterread(f, s->cutdirectory),
		fatreferenceprint(cluster, s->cutindex, 0);
		printf("\n\n");
		s->cuttype = 0;
	}

	if (direction == -2 && size == 0) {
		printf("deleting directory ");
		fatentryprintshortname(directory, index);
		printf("\n");
		fatentrydelete(directory, index);
	}

	return FAT_REFERENCE_NORMAL | FAT_REFERENCE_ORIG | FAT_REFERENCE_ALL;
}

int fattruncate(fat *f, int numclusters) {
	struct truncatestruct s;
	int res;

	/* e.g., for 1 cluster the last cluster is 2 */
	s.bound = numclusters + 2 - 1;
	dprintf("keeping clusters %d-%d\n", 0, s.bound);

	s.cuttype = 0;

	res = fatreferenceexecute(f, NULL, 0, -1, _fattruncate, &s);
	fatuflush(f);

	return res;
}

/*
 * defragment a filesystem
 *
 * start with d->cl=2
 * for every cluster reference, in cluster order:
 *	- swap the cluster with d->cl (or move to if d->cl is currently unused)
 *	- increase d->cl
 *
 * it requires the inverse fat because swapping with d->cl requires a reference
 * to d->cl
 *
 * more generally, move all clusters starting from a reference to an area
 * starting from a certain cluster number, in order
 */

FATINTERRUPTIBLEGLOBAL(defragment);

struct defragmentstruct {
	fatinverse *rev;
	int32_t cl;
	int recur;
	int dirmoved;
	int nchanges;
	int testonly;
};

int _fatdefragment(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit *dirdirectory, int dirindex, int32_t dirprevious,
		int direction, void *user) {
	struct defragmentstruct *d;
	int32_t target, cl;
	int targetfree;
	int res;
	int isdir;

	if(FATINTERRUPTIBLECHECK(defragment))
		return 0;

	if (direction != 0)
		return 0;

	d = (struct defragmentstruct *) user;

	target = fatreferencegettarget(f, directory, index, previous);
	if (target < FAT_FIRST)
		return FAT_REFERENCE_COND(d->recur);
	if (fatgetnextcluster(f, target) == FAT_BAD) {
		fatreferencesettarget(f, directory, index, previous, FAT_EOF);
		return 0;
	}

	if (d->cl > fatlastcluster(f))
		return 0;

	if (d->cl == target) {
		d->cl++;
		return FAT_REFERENCE_COND(d->recur);
	}

	if (fatcomplexdebug)
		FATEXECUTEDEBUG;

			/* fix . and .. */

	FATEXECUTEFIXDOT

			/* is this reference part of a directory? */

	isdir = FATEXECUTEISDIR;

			/* move or swap clusters */

	d->nchanges++;
	targetfree = fatgetnextcluster(f, d->cl) == FAT_UNUSED;

	if (d->testonly) {
		dprintf("%d %s-> %d\n", target, targetfree ? "" : "<", d->cl);
	}
	else if (targetfree) {
		dprintf("%d -> %d\n", target, d->cl);
		res = fatinversemovereference(f, d->rev,
			directory, index, previous, isdir, d->cl, 1);
		if (res < -1) {
			printf("move: IO error ");
			printf("%s ", res < -2 ? "writing" : "reading");
			printf("cluster %d\n", res % 2 ? d->cl : target);
			FATINTERRUPTIBLEABORT(defragment,
				FATINTERRUPTIBLEIOERROR);
			return 0;
		}
		dprintf("deallocate cluster %d\n", d->cl);
		fatunitdelete(&f->clusters, d->cl);
	}
	else {
		if (fatcomplexdebug) {
			fatinverseprint(f, d->rev, target);
			printf(" <-> ");
			fatinverseprint(f, d->rev, d->cl);
			printf("\n");
		}

		res = fatinverseswapreference(f, d->rev,
			directory, index, previous, isdir,
			d->rev[d->cl].directory, d->rev[d->cl].index,
			d->rev[d->cl].previous, d->rev[d->cl].isdir,
			1);
		if (res < -1) {
			printf("swap: IO error ");
			printf("%s ", res < -2 ? "writing" : "reading");
			printf("cluster %d\n", res % 2 ? d->cl : target);
			FATINTERRUPTIBLEABORT(defragment,
				FATINTERRUPTIBLEIOERROR);
			return 0;
		}

		if (d->rev[target].isdir && d->rev[target].previous == 0)
			d->dirmoved = 1;

		dprintf("deallocate cluster %d\n", d->cl);
		fatunitdelete(&f->clusters, d->cl);
		dprintf("deallocate cluster %d\n", target);
		fatunitdelete(&f->clusters, target);
	}

	// fatinversecheck(f, d->rev);

			/* increase target cluster */

	do {
		d->cl++;
	} while (fatgetnextcluster(f, d->cl) == FAT_BAD &&
		d->cl <= fatlastcluster(f));
	return FAT_REFERENCE_COND(d->recur);
}

int fatlinearize(fat *f, unit *directory, int index, int32_t previous,
		int32_t start, int recur, int testonly, int *nchanges) {
	struct defragmentstruct d;
	char *dummy;
	int i;
	int res;

	d.rev = fatinversecreate(f, 0);
	if (d.rev == NULL)
		return -1;

	dummy = malloc(100000);
	if (dummy == NULL)
		return -1;
	for (i = 0; i < 100000; i++)
		dummy[i] = i;
	free(dummy);
		
	d.cl = start;
	d.recur = recur;
	d.dirmoved = 0;
	d.nchanges = 0;
	d.testonly = testonly;

	FATINTERRUPTIBLEINIT(defragment);

	res = fatreferenceexecute(f, directory, index, previous,
			_fatdefragment, &d);

	if (nchanges != NULL)
		*nchanges = d.nchanges;

	if ((! fatreferenceisboot(directory, index, previous)) && d.dirmoved)
		fatfixdot(f);
	if (FATINTERRUPTIBLECHECK(defragment) && d.dirmoved)
		fatfixdot(f);

	if (fatcomplexdebug)
		fatinversecheck(f, d.rev, 0);

	fatflush(f);

	FATINTERRUPTIBLEFINISH(defragment);

	fatinversedelete(f, d.rev);
	return res != 0 ? res : FATINTERRUPTIBLECHECK(defragment) - 1;
}

int fatdefragment(fat *f, int testonly, int *nchanges) {
	return fatlinearize(f, NULL, 0, -1, 2, 1, testonly, nchanges);
}

