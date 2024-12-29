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

#pragma once

#include <stdio.h>
#include "util-defs.h"

#ifdef __cplusplus
extern "C" {
#endif

EXPORT void os_breakpoint(void);
EXPORT void crash(const char *format, ...);

EXPORT FILE   *os_fopen(const char *path, const char *mode);
EXPORT int64_t os_ftelli64(FILE *file);
EXPORT size_t  os_fread_utf8(FILE *file, char **pstr);

/* functions purely for convenience */
EXPORT char *os_quick_read_utf8_file(const char *path, size_t *size);
EXPORT bool  os_quick_write_utf8_file(const char *path, const char *str, size_t len, bool marker);

EXPORT size_t os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst, size_t dst_size);
EXPORT size_t os_wcs_to_utf8(const wchar_t *str, size_t len, char *dst, size_t dst_size);

EXPORT size_t os_utf8_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr);
EXPORT size_t os_wcs_to_utf8_ptr(const wchar_t *str, size_t len, char **pstr);

EXPORT double os_strtod(const char *str);
EXPORT int os_dtostr(double value, char *dst, size_t size);

#ifdef __cplusplus
}
#endif
