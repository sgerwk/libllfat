/*
 * inverse.h
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
 * inverse.h
 *
 * an inverse fat is a table linking each clusters to its reference, if any
 *
 * it is the inverse of fatreferencegetnext(); it also tells whether the chain
 * of a cluster is of a directory (as opposed to a regular file), that is, it
 * contains directory entries (rather than the content of a regular file)
 */

#ifdef _inverse_H
#else
#define _inverse_H

#include "unit.h"

typedef struct {
	unit *directory;
	int index;
	int32_t previous;
	int isdir;
} fatinverse;

/*
 * create (free when done), update and print an inverse fat
 */
fatinverse *fatinversecreate(fat *f, int file);
int fatinversedelete(fat *f, fatinverse *rev);
void fatinverseclear(fatinverse *rev, int32_t cluster);
int fatinverseisvoid(fatinverse *rev, int32_t cluster);
int32_t fatinverseset(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous,
		int isdir);
void fatinverseprint(fat *f, fatinverse *rev, int32_t cl);

/*
 * check if an updated inverse fat is the same as a recalculated one;
 * intended for debugging
 */
int fatinversecheck(fat *f, fatinverse *rev, int file);

/*
 * set target, move and swap clusters, updating the inverse fat
 */
void fatinversesettarget(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous,
		int32_t new, int isdir);
int fatinversemovereference(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous, int isdir,
		int32_t dst, int writeback);
int fatinversemove(fat *f, fatinverse *rev,
		int32_t src, int32_t dst, int writeback);
int fatinverseswapreference(fat *f, fatinverse *rev,
		unit *srcdir, int srcindex, int32_t srcprevious, int srcisdir,
		unit *dstdir, int dstindex, int32_t dstprevious, int dstisdir,
		int writeback);
int fatinverseswap(fat *f, fatinverse *rev,
		int32_t src, int32_t dst, int writeback);

/*
 * go back from clusters or directory entries
 */
int fatinversereferencetoentry(fatinverse *rev,
	unit **directory, int *index, int32_t *previous);
char *fatinversepath(fatinverse *rev,
	unit *directory, int index, int32_t previous);
int fatinversepreventry(fat *f, fatinverse *rev, unit **directory, int *index);

/*
 * mark unused all unused clusters
 */
int fatcleanunused(fat *f);

#endif

