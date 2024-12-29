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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void *bmalloc(size_t size)
{
	void *mem = malloc(size);
	if (!mem) {
		os_breakpoint();
		printf("Out of memory while trying to allocate %lu bytes", (unsigned long)size);
	}
	return mem;
}

static inline void *brealloc(void *ptr, size_t size)
{
	void *mem = realloc(ptr, size);
	if (!mem) {
		os_breakpoint();
		printf("Out of memory while trying to allocate %lu bytes", (unsigned long)size);
	}
	return mem;
}

#define bfree free

static inline void *bmemdup(const void *ptr, size_t size)
{
	void *mem = bmalloc(size);
	if (size)
		memcpy(mem, ptr, size);

	return mem;
}

static inline void *bzalloc(size_t size)
{
	void *mem = bmalloc(size);
	memset(mem, 0, size);
	return mem;
}

static inline char *bstrdup_n(const char *str, size_t n)
{
	char *dup;
	if (!str)
		return NULL;

	dup    = (char *)bmemdup(str, n + 1);
	dup[n] = 0;

	return dup;
}

static inline char *bstrdup(const char *str)
{
	if (!str)
		return NULL;

	return bstrdup_n(str, strlen(str));
}

#ifdef __cplusplus
}
#endif
