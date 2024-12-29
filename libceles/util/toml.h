/*
 * Copyright (c) 2024 Lain Bailey <lain@obsproject.com>
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

#include "util-defs.h"

/*
 * Generic ini-style toml file functions
 *
 * NOTE: It is highly recommended to use the default value functions (bottom of
 * the file) before reading any variables from toml files.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct toml_table;
struct toml_value;
struct toml_array;
typedef struct toml_table toml_t;
typedef struct toml_value toml_value_t;
typedef struct toml_array toml_array_t;

struct toml_pair {
	const char   *key;
	toml_value_t *value;
};

enum toml_type {
	TOML_TYPE_INVALID,
	TOML_TYPE_STRING,
	TOML_TYPE_INTEGER,
	TOML_TYPE_REAL,
	TOML_TYPE_BOOLEAN,
	TOML_TYPE_TABLE,
	TOML_TYPE_ARRAY,
};

#define TOML_SUCCESS 0
#define TOML_FILE_NOT_FOUND -1
#define TOML_ERROR -2

EXPORT int toml_open(toml_t **toml, const char *file, char **errors);

/* ------------------------------------------------------------------------- */
/* Tables                                                                    */

EXPORT toml_t *toml_addref(toml_t *toml);
EXPORT long    toml_release(toml_t *toml);

EXPORT size_t           toml_table_get_pair_count(toml_t *toml);
EXPORT struct toml_pair toml_table_get_pair(toml_t *toml, size_t idx);
EXPORT toml_value_t    *toml_table_get_value(toml_t *toml, const char *key);

EXPORT enum toml_type toml_table_get_type(toml_t *toml, const char *key);
EXPORT const char    *toml_table_get_string(toml_t *toml, const char *key);
EXPORT int64_t        toml_table_get_int(toml_t *toml, const char *key);
EXPORT bool           toml_table_get_bool(toml_t *toml, const char *key);
EXPORT double         toml_table_get_double(toml_t *toml, const char *key);
EXPORT toml_t        *toml_table_get_table(toml_t *toml, const char *key);
EXPORT toml_array_t  *toml_table_get_array(toml_t *toml, const char *key);

EXPORT bool toml_table_has_value(toml_t *toml, const char *key);

/* ------------------------------------------------------------------------- */
/* Arrays                                                                    */

EXPORT toml_array_t *toml_array_addref(toml_array_t *array);
EXPORT long          toml_array_release(toml_array_t *array);

EXPORT size_t        toml_array_count(toml_array_t *array);
EXPORT toml_value_t *toml_array_get_value(toml_array_t *array, size_t idx);
EXPORT const char   *toml_array_get_string(toml_array_t *array, size_t idx);
EXPORT int64_t       toml_array_get_int(toml_array_t *array, size_t idx);
EXPORT bool          toml_array_get_bool(toml_array_t *array, size_t idx);
EXPORT double        toml_array_get_double(toml_array_t *array, size_t idx);
EXPORT toml_t       *toml_array_get_table(toml_array_t *array, size_t idx);
EXPORT toml_array_t *toml_array_get_array(toml_array_t *array, size_t idx);

/* ------------------------------------------------------------------------- */
/* Items                                                                     */

EXPORT enum toml_type toml_value_get_type(toml_value_t *value);
EXPORT const char    *toml_value_get_string(toml_value_t *value);
EXPORT int64_t        toml_value_get_int(toml_value_t *value);
EXPORT bool           toml_value_get_bool(toml_value_t *value);
EXPORT double         toml_value_get_double(toml_value_t *value);
EXPORT toml_t        *toml_value_get_table(toml_value_t *value);
EXPORT toml_array_t  *toml_value_get_array(toml_value_t *value);

/* ------------------------------------------------------------------------- */
/* Sub-table access (just helper functions, usually used for the base table) */

EXPORT const char   *toml_get_string(toml_t *toml, const char *table, const char *key);
EXPORT int64_t       toml_get_int(toml_t *toml, const char *table, const char *key);
EXPORT bool          toml_get_bool(toml_t *toml, const char *table, const char *key);
EXPORT double        toml_get_double(toml_t *toml, const char *table, const char *key);
EXPORT toml_t       *toml_get_table(toml_t *toml, const char *table, const char *key);
EXPORT toml_array_t *toml_get_array(toml_t *toml, const char *table, const char *key);

EXPORT bool toml_has_user_value(toml_t *toml, const char *table, const char *key);

#ifdef __cplusplus
}
#endif
