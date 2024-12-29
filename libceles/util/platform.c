/*
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "platform.h"
#include "dstr.h"
#include "utf8.h"

#include <stdarg.h>
#include <stdlib.h>
#include <locale.h>

#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

NORETURN void bcrash(const char *format, ...)
{
	static bool crashing = false;
	va_list     args;

	if (crashing) {
		fputs("Crashed in the crash handler", stderr);
		exit(2);
	}

	crashing = true;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	exit(-1);
}

FILE *os_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
	wchar_t *wpath = NULL;
	FILE    *file  = NULL;

	if (path) {
		os_utf8_to_wcs_ptr(path, 0, &wpath);
		file = os_wfopen(wpath, mode);
		bfree(wpath);
	}

	return file;
#else
	return path ? fopen(path, mode) : NULL;
#endif
}

int64_t os_ftelli64(FILE *file)
{
#ifdef _MSC_VER
	return _ftelli64(file);
#else
	return ftello(file);
#endif
}

size_t os_fread_utf8(FILE *file, char **pstr)
{
	size_t size = 0;
	size_t len  = 0;

	*pstr = NULL;

	fseek(file, 0, SEEK_END);
	size = (size_t)os_ftelli64(file);

	if (size > 0) {
		char  bom[3];
		char *utf8str;
		off_t offset;

		bom[0] = 0;
		bom[1] = 0;
		bom[2] = 0;

		/* remove the ghastly BOM if present */
		fseek(file, 0, SEEK_SET);
		size_t size_read = fread(bom, 1, 3, file);
		(void)size_read;

		offset = (astrcmp_n(bom, "\xEF\xBB\xBF", 3) == 0) ? 3 : 0;

		size -= offset;
		if (size == 0)
			return 0;

		utf8str = bmalloc(size + 1);
		fseek(file, offset, SEEK_SET);

		size = fread(utf8str, 1, size, file);
		if (size == 0) {
			bfree(utf8str);
			return 0;
		}

		utf8str[size] = 0;

		*pstr = utf8str;
	}

	return len;
}

char *os_quick_read_utf8_file(const char *path, size_t *p_size)
{
	FILE  *f           = os_fopen(path, "rb");
	char  *file_string = NULL;
	size_t size;

	if (!f)
		return NULL;

	size = os_fread_utf8(f, &file_string);
	fclose(f);

	if (p_size)
		*p_size = size;

	return file_string;
}

bool os_quick_write_utf8_file(const char *path, const char *str, size_t len, bool marker)
{
	FILE *f = os_fopen(path, "wb");
	if (!f)
		return false;

	if (marker) {
		if (fwrite("\xEF\xBB\xBF", 3, 1, f) != 1) {
			fclose(f);
			return false;
		}
	}

	if (len) {
		if (fwrite(str, len, 1, f) != 1) {
			fclose(f);
			return false;
		}
	}
	fflush(f);
	fclose(f);

	return true;
}

size_t os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst, size_t dst_size)
{
	size_t in_len;
	size_t out_len;

	if (!str)
		return 0;

	in_len  = len ? len : strlen(str);
	out_len = dst ? (dst_size - 1) : utf8_to_wchar(str, in_len, NULL, 0, 0);

	if (dst) {
		if (!dst_size)
			return 0;

		if (out_len)
			out_len = utf8_to_wchar(str, in_len, dst, out_len + 1, 0);

		dst[out_len] = 0;
	}

	return out_len;
}

size_t os_wcs_to_utf8(const wchar_t *str, size_t len, char *dst, size_t dst_size)
{
	size_t in_len;
	size_t out_len;

	if (!str)
		return 0;

	in_len  = (len != 0) ? len : wcslen(str);
	out_len = dst ? (dst_size - 1) : wchar_to_utf8(str, in_len, NULL, 0, 0);

	if (dst) {
		if (!dst_size)
			return 0;

		if (out_len)
			out_len = wchar_to_utf8(str, in_len, dst, out_len, 0);

		dst[out_len] = 0;
	}

	return out_len;
}

size_t os_utf8_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr)
{
	if (str) {
		size_t out_len = os_utf8_to_wcs(str, len, NULL, 0);

		*pstr = bmalloc((out_len + 1) * sizeof(wchar_t));
		return os_utf8_to_wcs(str, len, *pstr, out_len + 1);
	} else {
		*pstr = NULL;
		return 0;
	}
}

size_t os_wcs_to_utf8_ptr(const wchar_t *str, size_t len, char **pstr)
{
	if (str) {
		size_t out_len = os_wcs_to_utf8(str, len, NULL, 0);

		*pstr = bmalloc((out_len + 1) * sizeof(char));
		return os_wcs_to_utf8(str, len, *pstr, out_len + 1);
	} else {
		*pstr = NULL;
		return 0;
	}
}

/* locale independent double conversion from jansson, credit goes to them */

static inline void to_locale(char *str)
{
	const char *point;
	char *pos;

	point = localeconv()->decimal_point;
	if (*point == '.') {
		/* No conversion needed */
		return;
	}

	pos = strchr(str, '.');
	if (pos)
		*pos = *point;
}

static inline void from_locale(char *buffer)
{
	const char *point;
	char *pos;

	point = localeconv()->decimal_point;
	if (*point == '.') {
		/* No conversion needed */
		return;
	}

	pos = strchr(buffer, *point);
	if (pos)
		*pos = '.';
}

double os_strtod(const char *str)
{
	char buf[64];
	strncpy(buf, str, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	to_locale(buf);
	return strtod(buf, NULL);
}

int os_dtostr(double value, char *dst, size_t size)
{
	int    ret;
	char  *start, *end;
	size_t length;

	ret = snprintf(dst, size, "%.17g", value);
	if (ret < 0)
		return -1;

	length = (size_t)ret;
	if (length >= size)
		return -1;

	from_locale(dst);

	/* Make sure there's a dot or 'e' in the output. Otherwise
	   a real is converted to an integer when decoding */
	if (strchr(dst, '.') == NULL && strchr(dst, 'e') == NULL) {
		if (length + 3 >= size) {
			/* No space to append ".0" */
			return -1;
		}
		dst[length]     = '.';
		dst[length + 1] = '0';
		dst[length + 2] = '\0';
		length += 2;
	}

	/* Remove leading '+' from positive exponent. Also remove leading
	   zeros from exponents (added by some printf() implementations) */
	start = strchr(dst, 'e');
	if (start) {
		start++;
		end = start + 1;

		if (*start == '-')
			start++;

		while (*end == '0')
			end++;

		if (end != start) {
			memmove(start, end, length - (size_t)(end - dst));
			length -= (size_t)(end - start);
		}
	}

	return (int)length;
}
