/*
 * boot.h
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
 * boot.h
 *
 * access data stored in the boot and information sectors
 */

#ifdef _BOOT_H
#else
#define _BOOT_H

#include <stdint.h>
#include "unit.h"

/*
 * boot sector
 */
int fatbootcheck(unit *boot);
int fatbootbits(unit *boot);
int fatbootsignaturebits(unit *boot);
int fatbootgetbytespersector(unit *boot);
int fatbootsetbytespersector(unit *boot, int bytes);
uint8_t fatbootgetsectorspercluster(unit *boot);
int fatbootsetsectorspercluster(unit *boot, int sectors);
int fatbootbytespercluster(unit *boot);
int fatbootgetreservedsectors(unit *boot);
int fatbootsetreservedsectors(unit *boot, int sectors);
int fatbootgetnumfats(unit *boot);
int fatbootsetnumfats(unit *boot, int nfat);
uint32_t fatbootgetnumsectors(unit *boot, int *bits);
int fatbootsetnumsectors(unit *boot, int *bits, uint32_t sectors);
int fatbootgetfatsize16(unit *boot);
int fatbootsetfatsize16(unit *boot, int size);
int fatbootgetfatsize32(unit *boot);
int fatbootsetfatsize32(unit *boot, int size);
int fatbootgetfatsize(unit *boot, int *bits);
int fatbootsetfatsize(unit *boot, int *bits, int size);
int fatbootminfatsize(unit *boot, int32_t nclusters);
int fatbitsfromclusters(int32_t nclusters);
int fatbootbestfatsize(unit *boot);
int fatbootconsistentsize(unit *boot, int *bits);
int32_t fatbootgetrootbegin(unit *boot, int *bits);
int fatbootsetrootbegin(unit *boot, int *bits, int32_t num);
int fatbootgetrootentries(unit *boot, int *bits);
int fatbootsetrootentries(unit *boot, int *bits, int entries);
int32_t fatbootnumrootsectors(unit *boot, int *bits);
int32_t fatbootnumdataclusters(unit *boot, int *bits);
int32_t fatbootlastcluster(unit *boot, int *bits);
int fatbootisvalidcluster(unit *boot, int *bits, int32_t n);
#define fatboot_UNCLEAN 0x01
#define fatboot_IOERROR 0x02
int fatbootgetdirtybits(unit *boot, int *bits);
int fatbootsetdirtybits(unit *boot, int *bits, int dirty);
int fatbootgetextendedbootsignature(unit *boot, int *bits);
int fatbootaddextendedbootsignature(unit *boot, int *bits);
int fatbootdeleteextendedbootsignature(unit *boot, int *bits);
uint32_t fatbootgetserialnumber(unit *boot, int *bits);
int fatbootsetserialnumber(unit *boot, int *bits, uint32_t serial);
char* fatbootgetvolumelabel(unit *boot, int *bits);
int fatbootsetvolumelabel(unit *boot, int *bits, const char l[11]);
char* fatbootgetfilesystemtype(unit *boot, int *bits);
int fatbootsetfilesystemtype(unit *boot, int *bits, char t[8]);
int fatbootgetbackupsector(unit *boot);
int fatbootsetbackupsector(unit *boot, int sector);
int fatbootgetinfopos(unit *boot);
int fatbootsetinfopos(unit *boot, int pos);
uint8_t fatbootgetmedia(unit *boot);
int fatbootsetmedia(unit *boot, uint8_t media);
int fatbootgetbootsignature(unit *boot);
int fatbootsetbootsignature(unit *boot);

/*
 * fs information sector
 */
int fatinfogetinfosignatures(unit *info);
int fatinfosetinfosignatures(unit *info);
int32_t fatinfogetlastallocatedcluster(unit *info);
void fatinfosetlastallocatedcluster(unit *info, int32_t last);
int32_t fatinfogetfreeclusters(unit *info);
void fatinfosetfreeclusters(unit *info, int32_t last);

#endif

