/*
 * directory.h
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
 * directory.h
 *
 * access a directory
 *
 * all these function have a "int32_t dir" argument, which is the number of the
 * first cluster of the directory to search
 *
 * a path may or may not be preceded by '/'; if it does, the directory argument
 * is ignored and the lookup starts from the root directory
 */

#ifdef _LOOKUP_H
#else
#define _LOOKUP_H

#include "fs.h"

/*
 * number of clusters of a file
 */
int fatentrynumclusters(fat *f, unit *directory, int index);

/*
 * next entry of a directory
 */
int fatnextentry(fat *f, unit **directory, int *index);

/*
 * cluster/index pair of a file, given its short name
 */
int fatlookupfile(fat *f, int32_t dir,
		const char *shortname, unit **directory, int *index);

/*
 * number of the first cluster of a file, given its short name
 */
int32_t fatlookupfirstcluster(fat *f, int32_t dir, const char *shortname);

/*
 * cluster/index of file, given its path
 */
int fatlookuppath(fat *f, int32_t dir,
		const char *path, unit **directory, int *index);

/*
 * first cluster of file, given its path
 */
int32_t fatlookuppathfirstcluster(fat *f, int32_t dir, const char *path);

/*
 * find first available directory entry
 */
int fatfindfreeentry(fat *f, unit **directory, int *index);

/*
 * find first free entry, given the path of the directory
 */
int fatfindfreeentrypath(fat *f, int32_t dir, const char *path, \
		unit **directory, int *index);

/*
 * file names
 */
int fatinvalidname(const char *name);
int fatinvalidpath(const char *path);
char *fatstoragename(const char *name);
char *fatstoragepath(const char *name);

/*
 * create a file given its path
 */
int fatcreatefile(fat *f, int32_t dir, char *path,
		unit **directory, int *index);

#endif

