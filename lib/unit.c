/*
 * unit.c
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

#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <search.h>
#include <ctype.h>
#include "unit.h"

int fatunitdebug = 0;
#define dprintf if (fatunitdebug) printf

#define UNUSED_DEPTH int __attribute__((unused)) depth

#define NO_ORIGIN ((uint64_t) -1)

/*
 * create, copy and deallocate a unit
 */

unit *fatunitcreate(int size) {
	unit *u;

	u = malloc(sizeof(unit));
	if (u == NULL) {
		printf("cannot allocate memory\n");
		exit(1);
	}

	u->size = size;
	u->origin = NO_ORIGIN;
	u->data = malloc(size);
	if (u->data == NULL) {
		printf("cannot allocate memory\n");
		exit(1);
	}
	u->refer = 0;
	u->dirty = 0;
	u->user = NULL;

	return u;
}

unit *fatunitcopy(unit *u) {
	unit *c;

	c = malloc(sizeof(unit));
	if (c == NULL) {
		printf("cannot allocate memory\n");
		exit(1);
	}
	memcpy(c, u, sizeof(unit));
	if (u->data) {
		c->data = malloc(u->size);
		if (c->data == NULL) {
			printf("cannot allocate memory\n");
			exit(1);
		}
		memcpy(c->data, u->data, u->size);
	}

	return c;
}

void fatunitdestroy(unit *u) {
	dprintf("deleting unit %d\n", u->n);
	if (u == NULL)
		return;
	free(u->data);
	free(u);
}

/*
 * simulate I/O errors
 *	- on read (type=FAT_READ), write (FAT_WRITE) or seek (FAT_SEEK)
 *	- of unit n
 *	- that is a sector (iscluster=0) or cluster (iscluster!=0)
 *	- with error (res=-1) or short read/write (0<=res<u->size))
 *
 * a program can define its own simulation error array, but it's easier to just
 * read them from a file via fatsimulationread(filename)
 */

#define SIMULATE_ERROR(t, u) if (_fatsimulateerror(t, u)) return -1
#define noSIMULATE_ERROR(t, u)

struct fat_simulate_errors_s *fat_simulate_errors = NULL;

char *typestring[] = {"NONE", "READ", "WRITE", "kqpwers", "SEEK"};

int _fatsimulateerror(int type, unit *u) {
	int i;

	if (fat_simulate_errors == NULL)
		return 0;

	for (i = 0; fat_simulate_errors[i].type; i++)
		if ((fat_simulate_errors[i].fd == u->fd ||
			(fat_simulate_errors[i].fd == -1)) &&
			fat_simulate_errors[i].n == u->n &&
			(fat_simulate_errors[i].type & type) &&
		    	((fat_simulate_errors[i].iscluster == 0 &&
				u->origin == 0) ||
			(fat_simulate_errors[i].iscluster != 0 &&
		     		u->origin != 0))) {
			printf("simulated error ");
			printf("(%s) ", fat_simulate_errors[i].res ?
					"fail" : "short");
			printf("on %s ", typestring[type]);
			printf("for %s ", fat_simulate_errors[i].iscluster ?
					"CLUSTER" : "SECTOR");
			printf("%d\n", u->n);
			return fat_simulate_errors[i].res;
		}

	return 0;
}

void fatsimulateinit() {
	int single;
	single = sizeof(struct fat_simulate_errors_s);
	fat_simulate_errors = malloc(100 * single);
	fat_simulate_errors[0].type = 0;
}

int fatsimulateread(char *filename, int fd) {
	FILE *errfd;
	struct fat_simulate_errors_s cur;
	int size, single, n;
	char line[100], type[100], iscluster[100];
	int res;

	printf("read simulated errors from file %s\n", filename);

	errfd = fopen(filename, "r");
	if (errfd == NULL) {
		printf("opening file %s: %s\n", filename, strerror(errno));
		return -1;
	}

	single = sizeof(struct fat_simulate_errors_s);
	size = 0;
 	for (n = 0; fat_simulate_errors[n].type != 0; n++)
		size++;

	for (n = 0; fgets(line, 99, errfd); n++) {

		if (line[0] == '#' || line[0] == '\n') {
			n--;
			continue;
		}

		res = sscanf(line, "%99s %d %s %d",
				type, &cur.n, iscluster, &cur.res);

		if (res < 2) {
			printf("error in entry %d of %s\n", n + 1, filename);
			n--;
			continue;
		}
		cur.iscluster = -1;
		if (res < 3)
			cur.iscluster = 1;
		if (res < 4)
			cur.res = -1;

		cur.fd = fd;

		cur.type = 0;
		if (strstr(type, "READ"))
			cur.type |= FAT_READ;
		if (strstr(type, "WRITE"))
			cur.type |= FAT_WRITE;
		if (strstr(type, "SEEK"))
			cur.type |= FAT_SEEK;
		if (cur.type == 0)
			printf("error reading entry %d of sim file\n", n);

		if (! strcmp(iscluster, "SECTOR") || ! strcmp(iscluster, "0"))
			cur.iscluster = 0;
		if (! strcmp(iscluster, "CLUSTER") || ! strcmp(iscluster, "1"))
			cur.iscluster = 1;
		if (cur.iscluster == -1)
			printf("error reading entry %d of sim file\n", n);

		if (n >= size) {
			size += 100;
			fat_simulate_errors =
				realloc(fat_simulate_errors, size * single);
		}
		fat_simulate_errors[n] = cur;
	}

	fat_simulate_errors[n].type = 0;

	fclose(errfd);
	return 0;
}

/*
 * read and write a unit from the filesystem
 */

int _fatunitseek(unit *u) {
	off_t res, pos;

	pos = u->origin + ((uint64_t) u->n) * u->size;
	dprintf("lseek %" PRIu64 "\n", pos);

	res = lseek(u->fd, pos, SEEK_SET);
	SIMULATE_ERROR(FAT_SEEK, u);
	if (res == (off_t) -1) {
		if (u->origin == NO_ORIGIN)
			printf("unspecified origin of unit %d\n", u->n);
		else {
			printf("error in lseek to unit %d, ", u->n);
			printf("position %" PRId64 ", ", pos);
			printf("description: %s\n", strerror(errno));
		}
		u->error |= FAT_SEEK;
		return -1;
	}

	return 0;
}

int _fatunitread(unit *u) {
	off_t res;
	dprintf("reading unit %d, origin %" PRId64 "\n", u->n, u->origin);

	if (_fatunitseek(u))
		return -1;

	res = read(u->fd, u->data, u->size);
	SIMULATE_ERROR(FAT_READ, u);
	if (res != u->size) {
		if (res == -1)
			printf("error in read: %s\n", strerror(errno));
		else
			printf("short read: %" PRId64 " < %d\n", res, u->size);
		u->error |= FAT_READ;
		return -1;
	}
	u->dirty = 0;
	return 0;
}

int _fatunitwrite(unit *u) {
	off_t res;
	dprintf("writing unit %d, origin %" PRId64 "\n", u->n, u->origin);

	if (_fatunitseek(u))
		return -1;

	res = write(u->fd, u->data, u->size);
	SIMULATE_ERROR(FAT_WRITE, u);
	if (res != u->size) {
		if (res == -1)
			printf("error in write: %s\n", strerror(errno));
		else
			printf("short write: %" PRId64 " < %d\n", res, u->size);
		u->error |= FAT_WRITE;
		return -1;
	}
	u->dirty = 0;
	return 0;
}

/*
 * order of units in the tree
 */

int _compareunit(const void *a, const void *b) {
	if (((unit *) a)->n < ((unit *) b)->n)
		return -1;
	else if (((unit *) a)->n == ((unit *) b)->n)
		return 0;
	else
		return 1;
}

/*
 * get, insert, move, swap, writeback and delete a unit from the cache
 */

unit *fatunitget(unit **cache, uint64_t origin, int size, long n, int fd) {
	unit k, **s, *i, *r;

	k.n = n;
	s = (unit **) tfind(&k, (void **) cache, _compareunit);
	if (s != NULL && (*s)->data !=NULL)
		return *s;

	if (s == NULL)
		i = fatunitcreate(size);
	else {
		i = *s;
		i->size = size;
		i->data = malloc(size);
		if (i->data == NULL) {
			printf("cannot allocate memory\n");
			exit(1);
		}
	}
	i->origin = origin;
	i->n = n;
	i->fd = fd;

	if (_fatunitread(i))
		return NULL;
	r = * (unit **) tsearch(i, (void **) cache, _compareunit);
	if (r == NULL) {
		printf("insufficient memory to store cluster %d ", i->n);
		printf("in cache\n");
	}
	return r;
}

int fatunitinsert(unit **cache, unit *u, int replace) {
	unit **f;

	f = (unit **) tsearch(u, (void **) cache, _compareunit);
	if (*f == u) {
		u->dirty = 1;
		return 0;
	}
	if (! replace && (*f)->data != NULL)
		return -1;

	fatunitdestroy(*f);
	*f = u;
	u->dirty = 1;
	return 0;
}

void fatunitmove(unit **cache, unit *u, int dest) {
	fatunitdetach(cache, u->n);
	u->n = dest;
	fatunitinsert(cache, u, 1);
}

void fatunitswap(unit **cache, unit *u, unit *w) {
	int32_t un, wn;

	un = u->n;
	wn = w->n;
	fatunitdetach(cache, un);
	fatunitdetach(cache, wn);
	u->n = wn;
	w->n = un;
	fatunitinsert(cache, u, 0);
	fatunitinsert(cache, w, 0);
}

int fatunitwriteback(unit *u) {
	if (! u->dirty)
		return 0;

	return _fatunitwrite(u);
}

int _fatunitdeleteordetach(unit **cache, long n, int destroy) {
	unit k, **s, *u;

	k.n = n;
	s = (unit **) tfind(&k, (void **) cache, _compareunit);
	if (s == NULL)
		return -1;
	u = *s;

	if (destroy && (u->refer > 0 || u->dirty))
		return -1;

	k.n = n;
	if (NULL == tdelete(&k, (void **) cache, _compareunit))
		return -1;

	if (destroy)
		fatunitdestroy(u);

	return 0;
}

int fatunitdetach(unit **cache, long n) {
	return _fatunitdeleteordetach(cache, n, 0);
}

int fatunitdelete(unit **cache, long n) {
	return _fatunitdeleteordetach(cache, n, 1);
}

/*
 * flush units in cache to filesystem
 */

void _fatunitflush(const void *nodep, const VISIT which, UNUSED_DEPTH) {
	if (which != preorder && which != leaf)
		return;
	
	fatunitwriteback(* (unit **) nodep);
}

void fatunitflush(unit *cache) {
	twalk(cache, _fatunitflush);
}

/*
 * deallocated units have u->data freed and set to NULL
 */

unsigned char *fatunitgetdata(unit *u) {
	if (u->data == NULL) {
		u->data = malloc(u->size);
		if (u->data == NULL) {
			printf("cannot allocate memory\n");
			exit(1);
		}
		if (_fatunitread(u)) {
			printf("unit %d no longer readable\n", u->n);
			exit(1);
		}
	}

	return u->data;
}

void fatunitfree(unit *u) {
	if (u->dirty || u->refer > 0)
		return;
	free(u->data);
	u->data = NULL;
}

void _fatunitfreecache(const void *nodep, const VISIT which, UNUSED_DEPTH) {
	if (which != preorder && which != leaf)
		return;
	
	fatunitfree(* (unit **) nodep);
}

void fatunitfreecache(unit *cache) {
	twalk(cache, _fatunitfreecache);
}

/*
 * dellocate the entire cache
 */

void _fatunitdeallocate(void *nodep) {
	fatunitdestroy((unit *) nodep);
}

void fatunitdeallocate(unit *cache) {
	tdestroy(cache, _fatunitdeallocate);
}

/*
 * dump a unit to stdout
 */
void fatunitdump(unit *u, int hex) {
	int i, j;

	fatunitgetdata(u);

	if (! hex)
		for (i = 0; i < u->size; i++)
			printf("%c", u->data[i]);
	else
		for (i = 0; i < u->size; i+=16) {
			printf("%04X	", i);
			for (j = i; j < i + 16 && j < u->size; j++)
				printf("%02X ", u->data[j]);
			printf("    ");
			for (j = i; j < i + 16 && j < u->size; j++)
				if (isprint(u->data[j]))
					printf("%c", u->data[j]);
				else
					printf(".");
			printf("\n");
		}
}

/*
 * dump all units for debugging
 */

void _fatunitprint(const void *nodep, const VISIT which, UNUSED_DEPTH) {
	unit *u;
	int i;

	if (which != postorder && which != leaf)
		return;
	u = * (unit **) nodep;

	printf("%7d:  ", u->n);
	// printf("%6d %d:  ", u->n, u->refer);

	if (u->data == NULL) {
		printf("NULL\n");
		return;
	}

	for (i = 0; i < 16; i++)
		printf("%02X ", u->data[i]);
	printf("  ");
	for (i = 0; i < 16; i++)
		printf("%c", isprint(u->data[i]) ? u->data[i] : '.');
	printf("\n");
}

void fatunitdumpcache(char *which, unit *cache) {
	printf("==== %s dump:\n", which);
	twalk(cache, _fatunitprint);
}

