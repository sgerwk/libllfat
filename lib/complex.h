/*
 * complex.h
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
 * complex.h
 *
 * operations that may long to execute and cannot arbitrarily interrupted, so
 * that some signal handling is necessary
 *
 *	FATINTERRUPTIBLEGLOBALS(name)
 *		declare some globals; choose an arbitrary name that is then 
 *		used in the following macros
 *
 *	FATINTERRUPTIBLESTART(name)
 * 		before starting the entire complex operation, stop SIGTERM and
 *		SIGINT
 *
 *	FATINTERRUPTIBLECHECK(name)
 *		tells whether operation has to be aborted; if different than
 *		zero, execution should be cut short as soon as possible
 *
 *		check it also at the end if some cleanup code has to be run due
 *		to the aborted operation
 *
 *	FATINTERRUPTIBLEABORT(name, condition)
 * 		used to abort the operation, as if a signal arrived; condition
 * 		is a number >0 that FATINTERRUPTIBLECHECK(name) returns; -1
 * 		signals abortion due to signal
 *
 *	FATINTERRUPTIBLEFINISH(name) enable the signals again
 *
 * due to globals, a function using these macros is not reentrant
 */

#ifdef _COMPLEX_H
#else
#define _COMPLEX_H

#include "fs.h"

/*
 * uninterruptible flush
 */
int fatuflush(fat *f);

/*
 * move the clusters in an area to another, in cluster reference order
 * libllfat.txt: [Cluster reference order]
 */
int fatmovearea(fat *f,
		int32_t srcbegin, int32_t srcend,
		int32_t dstbegin, int32_t dstend);

/*
 * compact a filesystem by moving used clusters at the beginning
 */
int fatcompact(fat *f);

/*
 * truncate a filesystem
 */
int fattruncate(fat *f, int numclusters);

/*
 * defragment a part of a filesystem, or all of it
 */
int fatlinearize(fat *f, unit *directory, int index, int32_t previous,
		int32_t start, int recur, int testonly, int *nchanges);
int fatdefragment(fat *f, int testonly, int *nchanges);

/*
 * macros for dealing with signals (see top of file)
 */

#define FATINTERRUPTIBLEINTERRUPTED -1
#define FATINTERRUPTIBLENORMAL 0
#define FATINTERRUPTIBLEIOERROR 1

#define FATINTERRUPTIBLEGLOBAL(name)					\
	int _##name##killed;						\
	void _##name##handler(int __attribute__((unused)) signum) {	\
		_##name##killed = FATINTERRUPTIBLEINTERRUPTED;		\
	}
#define FATINTERRUPTIBLEINIT(name)					\
	_##name##killed = FATINTERRUPTIBLENORMAL;			\
	signal(SIGINT, _##name##handler);				\
	signal(SIGTERM, _##name##handler);
#define FATINTERRUPTIBLECHECK(name) (_##name##killed)
#define FATINTERRUPTIBLEABORT(name, condition) {_##name##killed = condition;}
#define FATINTERRUPTIBLEFINISH(name)					\
	signal(SIGINT, SIG_DFL);					\
	signal(SIGTERM, SIG_DFL);

#endif

