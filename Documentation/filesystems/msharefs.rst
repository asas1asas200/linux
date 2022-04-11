.. SPDX-License-Identifier: GPL-2.0

=====================================================
msharefs - a filesystem to support shared page tables
=====================================================

msharefs is a ram-based filesystem that allows multiple processes to
share page table entries for shared pages.

msharefs is typically mounted like this::

	mount -t msharefs none /sys/fs/mshare

When a process calls mshare syscall with a name for the shared address
range, a file with the same name is created under msharefs with that
name. This file can be opened by another process, if permissions
allow, to query the addresses shared under this range. These files are
removed by mshare_unlink syscall and can not be deleted directly.
Hence these files are created as immutable files.
