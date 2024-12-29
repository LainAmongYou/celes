#pragma once

#include "util-defs.h"
#include "darray.h"
#include "lexer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hash_table_val_free_cb)(void *val);

typedef struct {
	uint8_t               *buckets;
	size_t                 size;
	size_t                 bucket_limit;
	size_t                 bucket_count;
	size_t                 type_size;
	hash_table_val_free_cb on_free;
} hash_table_t;

static inline void hash_table_init(hash_table_t *ht, size_t type_size, hash_table_val_free_cb on_free)
{
	ht->buckets      = NULL;
	ht->size         = 0;
	ht->bucket_limit = 0;
	ht->bucket_count = 0;
	ht->type_size    = type_size;
	ht->on_free      = on_free;
}

EXPORT void  hash_table_free(hash_table_t *ht);
EXPORT void *hash_table_set(hash_table_t *ht, const char *key, void *val);
EXPORT void *hash_table_set_n(hash_table_t *ht, const char *key, size_t len, void *val);
EXPORT void *hash_table_get(hash_table_t *ht, const char *key);
EXPORT void *hash_table_get_idx(hash_table_t *ht, size_t idx, const char **key);

#ifdef __cplusplus
}
#endif
