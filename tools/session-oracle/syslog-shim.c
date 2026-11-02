/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static void write_line(const char *format, va_list args)
{
	FILE *out = fopen(getenv("POLO_ORACLE_LOG"), "a");

	if (out) {
		vfprintf(out, format, args);
		fputc('\n', out);
		fclose(out);
	}
}

void syslog(int priority, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	write_line(format, args);
	va_end(args);
}

void __syslog_chk(int priority, int flag, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	write_line(format, args);
	va_end(args);
}
