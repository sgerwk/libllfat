Summary: a library for low-level access to a FAT12/16/32 filesystem
Name: libllfat
Version: 0.1.3
Release: 1
License: GPLv3
Group: System/Filesystems
URL: http://libllfat.sourceforge.net

%description
While libllfat is able to access files in a FAT12/16/32
filesystem, its intended application is in low-level manipulation
of it. It contains functions for accessing a specific file
allocation table, get and change the successor of a cluster, etc.

It allows changing the filesystem in a way that makes it
out-of-spec, such as creating hard links and cyclic files and
directories.

Includes fatview (show the internal structure of a filesystem),
fatshrink (reduces the number of sectors in a filesystem,
possibly moving or cutting clusters that do not fit in the new
size) and fattool (various low-level operations on a filesystem).

%post
ldconfig /usr/local/lib

%postun
ldconfig /usr/local/lib

%files
/usr/local/bin/fattool
/usr/local/bin/fatview
/usr/local/bin/fatshrink
/usr/local/bin/fatbackup
/usr/local/include/llfat.h
/usr/local/lib/libllfat.so
%doc /usr/local/man/man1/fatshrink.1
%doc /usr/local/man/man1/fatbackup.1
%doc /usr/local/man/man1/fatview.1
%doc /usr/local/man/man1/fattool.1
%doc /usr/local/man/man3/fat_lib.3
%doc /usr/local/man/man3/fat_functions.3
%doc /usr/local/man/man5/fat.5
%doc /usr/share/doc/packages/libllfat

