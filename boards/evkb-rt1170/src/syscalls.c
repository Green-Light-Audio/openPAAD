/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Minimal newlib system calls: standard output goes to the board's debug
 * console (LPUART1, the MCU-Link virtual COM port); there is no file system.
 */

#include <errno.h>
#include <sys/stat.h>

#include "fsl_debug_console.h"

int _write(int fd, const char *buf, int len);
int _read(int fd, char *buf, int len);
int _close(int fd);
int _fstat(int fd, struct stat *st);
int _isatty(int fd);
int _lseek(int fd, int offset, int whence);
void *_sbrk(int increment);
void _exit(int status);

/* Heap bounds from the linker script (.heap section in DTCM). */
extern char __HeapBase[];
extern char __HeapLimit[];

int _write(int fd, const char *buf, int len)
{
	(void)fd;

	for (int i = 0; i < len; i++)
		DbgConsole_Putchar(buf[i]);

	return len;
}

int _read(int fd, char *buf, int len)
{
	(void)fd;
	(void)buf;
	(void)len;
	errno = ENOSYS;
	return -1;
}

int _close(int fd)
{
	(void)fd;
	return -1;
}

int _fstat(int fd, struct stat *st)
{
	(void)fd;
	st->st_mode = S_IFCHR;
	return 0;
}

int _isatty(int fd)
{
	(void)fd;
	return 1;
}

int _lseek(int fd, int offset, int whence)
{
	(void)fd;
	(void)offset;
	(void)whence;
	return 0;
}

/* newlib's malloc() grows its arena through _sbrk(). FreeRTOS uses its own
 * heap (see freertos_heap.c); this small arena only serves the C library. */
void *_sbrk(int increment)
{
	static char *brk = __HeapBase;
	char *prev = brk;

	if (increment < 0 || (__HeapLimit - brk) < increment) {
		errno = ENOMEM;
		return (void *)-1;
	}

	brk += increment;
	return prev;
}

void _exit(int status)
{
	(void)status;

	for (;;) {
	}
}
