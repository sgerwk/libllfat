/*
 * unit.h
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
 * unit.h
 *
 * a unit is a block of data in the filesystem: a sector or a cluster
 * (including the root directory, which may be larger than a regular cluster)
 *
 *	fd	the file descriptor of the filesystem this unit belongs to
 *	n	the progressive number of the unit:
 *			number of sector
 *			1 for the root directory megacluster for fat12/fat16
 *			>=2 number of cluster
 *	size	the size of the data
 *	origin	position of unit 0 in fd, expressed in bytes
 *	data	the actual data
 *	dirty	the unit in cache differs from that in the filesystem
 *	refer	usage counter; the unit cannot be removed if > 0
 *	user 	free for program use
 */

#ifdef _UNIT_H
#else
#define _UNIT_H

#include <stdint.h>

#define FAT_READ  1
#define FAT_WRITE 2
#define FAT_SEEK  4

typedef struct {
	int fd;			/* filesystem this unit belongs to */
	int32_t n;		/* index of sector/cluster */
	int size;		/* size of this unit, in bytes */
	uint64_t origin;	/* position of unit 0, in bytes */
	unsigned char *data;	/* the data */
	int error;		/* error: read(1), write(2), seek(4) */
	int dirty;		/* cached unit differs from file */
	int refer;		/* usage counter; no-remove if > 0 */
	void *user;		/* free for program use */
} unit;

/* a 8/16/32 bit integer at some byte offset in a unit */
#define _unitoffset(unit, offset) &fatunitgetdata(unit)[offset]

#define  _unit8int(unit, offset) (* (( int8_t  *) _unitoffset(unit, offset)))
#define _unit16int(unit, offset) (* ((int16_t  *) _unitoffset(unit, offset)))
#define _unit32int(unit, offset) (* ((int32_t  *) _unitoffset(unit, offset)))

#define  _unit8uint(unit, offset) (* (( uint8_t  *) _unitoffset(unit, offset)))
#define _unit16uint(unit, offset) (* ((uint16_t  *) _unitoffset(unit, offset)))
#define _unit32uint(unit, offset) (* ((uint32_t  *) _unitoffset(unit, offset)))

/* create, copy and destroy an unit */
unit *fatunitcreate(int size);
unit *fatunitcopy(unit *u);
void fatunitdestroy(unit *u);

/* get, insert, detach, move, swap, writeback and delete a unit from a cache */
unit *fatunitget(unit **cache, uint64_t origin, int size, long n, int fd);
int fatunitinsert(unit **cache, unit *u, int replace);
int fatunitdetach(unit **cache, long n);
void fatunitmove(unit **cache, unit *u, int dest);
void fatunitswap(unit **cache, unit *u, unit *w);
int fatunitwriteback(unit *u);
int fatunitdelete(unit **cache, long n);

/* flush all dirty units to filesystem */
void fatunitflush(unit *cache);

/* deal with deallocated units (data only); README: Note 1 **/
unsigned char *fatunitgetdata(unit *u);
void fatunitfree(unit *u);
void fatunitfreecache(unit *cache);

/* deallocate cache */
void fatunitdeallocate(unit *cache);

/* dump a unit to stdout */
void fatunitdump(unit *u, int hex);

/* dump all cached units, for debugging */
void fatunitdumpcache(char *which, unit *cache);

/* simulated errors */
struct fat_simulate_errors_s {
	int fd;
	int type;
	int n;
	int iscluster;
	int res;
};
extern struct fat_simulate_errors_s *fat_simulate_errors;
void fatsimulateinit();
int fatsimulateread(char *filename, int fd);

#endif

