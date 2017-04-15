/*
 * table.h
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
 * table.h
 *
 * functions for the file allocation table, which contains the next of each
 * cluster and whether the cluster is unused
 */

#ifdef _TABLE_H
#else
#define _TABLE_H

#include <stdint.h>
#include "unit.h"
#include "fs.h"

/* all FATs when writing, FAT0 when reading */
#define FAT_ALL (-1)

/*
 * load in cache a whole FAT, or all of them if nfat == FAT_ALL
 */
int fatreadfat(fat *f, int nfat);

/* specific values for a cluster number */
#define FAT_FIRST (2)
#define FAT_ROOT (1)
#define FAT_UNUSED (0)
#define FAT_EOF (-1)
#define FAT_BAD (-2)
#define FAT_ERR (-1000)

/*
 * access a fat entry
 *
 * this is for raw access to the elements of a specific file allocation table:
 * the resulting element is returned as-is, not converted into FAT_EOF or
 * FAT_BAD; also, FAT_ALL is invalid here
 *
 * below are the functions for getting/setting the successor of a cluster
 */
int32_t fatgetfat(fat *f, int nfat, int32_t n);
int fatsetfat(fat *f, int nfat, int32_t n, int32_t next);

/*
 * set the first two entries in a fat (the table "header")
 */
int fatfixtableheader(fat *f, int nfat);

/*
 * dirty bits
 * they are inverted and shifted to make them the same as the
 * dirty bits in the boot sector
 */
int fatgetfatdirtybits(fat *f, int nfat);
int fatsetfatdirtybits(fat *f, int nfat, uint32_t dirty);

/*
 * get and set the next (or eof/unused) of a cluster
 */
int32_t fatgetnextcluster(fat *f, int32_t cluster);
int fatsetnextcluster(fat *f, int32_t cluster, int32_t next);

/*
 * initialize a file allocation table
 */
int fatinittable(fat *f, int nfat);

/*
 * check if a cluster is within an interval; wrap if end < begin
 */
int fatclusterisbetween(int32_t cluster, int32_t begin, int32_t end);

/*
 * wrapping decrease/increase of a cluster number in an interval
 */
int32_t fatclusterintervalprev(fat *f, int32_t c, int32_t begin, int32_t end);
int32_t fatclusterintervalnext(fat *f, int32_t c, int32_t begin, int32_t end);

/*
 * number of free clusters; wrap if end < begin
 */
int32_t fatclusternumfreebetween(fat *f, int32_t begin, int32_t end);
int32_t fatclusternumfree(fat *f);

/*
 * find a free cluster
 */
int32_t fatclusterfindfreebetween(fat *f,
		int32_t begin, int32_t end, int32_t start);
int32_t fatclusterfindfree(fat *f);

/*
 * presence and count of bad clusters in an area
 */
int fatclusterareaisbad(fat *f, int32_t begin, int32_t end);
int fatclusternumbadbetween(fat *f, int32_t begin, int32_t end);

/*
 * find the "most free" area of a given size
 * if allowbad==0, an area is discarded if it has even a single bad sector
 * return: start of the best area, size of this area (in maxfree)
 */
int32_t fatclustermostfree(fat *f, int size, int allowbad, int *maxfree);

/*
 * find the first longest linear subchain of a chain
 * return: first cluster of the subchain; maxindex is its ordinal in the chain
 * (starting from zero); maxlen is the length of the longest subchain
 */
int32_t fatclusterlongestlinear(fat *f, int32_t start,
		int *maxlen, int *maxindex);

/*
 * find an allocated cluster
 */
int fatclusterfindallocatedbetween(fat *f,
		int32_t begin, int32_t end, int32_t start);
int fatclusterfindallocated(fat *f, int32_t begin, int32_t end);

/*
 * free a chain of clusters
 */
int fatclusterfreechain(fat *f, int32_t begin);

/*
 * create and read a cluster; writeback is done by fatunitwriteback(unit *)
 */
int fatclusterposition(fat *f, int32_t cl, uint64_t *origin, int *size);
unit *fatclustercreate(fat *f, int32_t cl);
unit *fatclusterread(fat *f, int32_t cl);

/*
 * the cluster that contains a sector
 */
int32_t fatsectorposition(fat *f, uint32_t sector);

#endif

