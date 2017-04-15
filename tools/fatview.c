/*
 * fatview.c
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <llfat.h>

int main(int argn, char *argv[]) {
	char *filename;
	fat *f;
	int all = 0;

	if (argn - 1 < 1) {
		printf("usage:\n\tfattest filename [test] [all]\n");
		exit(1);
	}
	filename = argv[1];

	f = fatopen(filename, 0);
	if (f == NULL)
		return -1;

	if (0 != fatcheck(f)) {
		printf("invalid filesystem in %s\n", filename);
		return -1;
	}

	fatsummary(f);

	while (argn - 1 > 1) {
		if (! strcmp(argv[2], "test")) {
			fatreferencedebug = 1;
			fattabledebug = 1;
			fatunitdebug = 1;
		}
		else if (! strcmp(argv[2], "all"))
			all = 1;
		argn--;
		argv++;
	}

	printf("=========================\n");
	fatdumplong(f, NULL, 0, -1, 1, all);
	printf("=========================\n");

	fatclose(f);

	return 0;
}

