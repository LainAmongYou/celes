#include <math.h>

#include "hash.h"

struct bucket_header {
	uint64_t    hash;
	struct dstr key;
};

static uint64_t get_hash(const char *key, const size_t len)
{
	const uint64_t polynomial     = 29791;      /* ? */
	const uint64_t prime          = 1000000007; /* large prime */
	uint64_t       polynomial_pow = 1;
	uint64_t       hash           = 0;
	size_t         i;

	for (i = 0; i < len; i++) {
		const char ch     = key[i];
		uint64_t   ch_val = ch;

		if (i != 0)
			polynomial_pow *= polynomial;

		ch_val *= polynomial_pow;
		hash += ch_val;
	}

	return hash % prime;
}

static inline struct bucket_header *get_bucket(hash_table_t *ht, size_t idx)
{
	void *ptr = ht->buckets + (idx * (ht->type_size + sizeof(struct bucket_header)));
	return ptr;
}

void hash_table_free(hash_table_t *ht)
{
	size_t i;
	for (i = 0; i < ht->size; i++) {
		struct bucket_header *bucket = get_bucket(ht, i);
		if (!dstr_is_empty(&bucket->key) && ht->type_size && ht->on_free) {
			ht->on_free(++bucket);
		}
	}

	bfree(ht->buckets);
	memset(ht, 0, sizeof(*ht));
}

static void hash_table_upsize(hash_table_t *ht);

#define BUCKET_LIMIT(size) (size >> 1 | size >> 2)
#define STARTING_CAPACITY 16

static void *hash_table_set_internal(hash_table_t *ht, char *key, size_t len, bool copy, void *val)
{
	if (!ht->size) {
		ht->buckets      = bzalloc((ht->type_size + sizeof(struct bucket_header)) * STARTING_CAPACITY);
		ht->size         = STARTING_CAPACITY;
		ht->bucket_limit = BUCKET_LIMIT(STARTING_CAPACITY);
	}

	uint64_t hash = get_hash(key, len);
	size_t   idx  = (size_t)(hash % (uint64_t)ht->size);

	for (;;) {
		struct bucket_header *bucket = get_bucket(ht, idx);

		/* insert */
		if (dstr_is_empty(&bucket->key)) {
			bucket->hash = hash;
			if (copy) {
				dstr_ncopy(&bucket->key, key, len);
			} else {
				bucket->key.array    = key;
				bucket->key.size     = len;
				bucket->key.capacity = len + 1;
			}

			if (ht->type_size) {
				memcpy(++bucket, val, ht->type_size);
			}

			if (++ht->bucket_count == ht->bucket_limit) {
				hash_table_upsize(ht);
			}
			return ht->type_size ? bucket : NULL;
		}

		/* set */
		if (bucket->hash == hash) {
			if (ht->type_size) {
				bucket++;
				if (ht->on_free) {
					ht->on_free(bucket);
				}
				memcpy(bucket, val, ht->type_size);
				return bucket;
			}
			break;
		}

		idx++;
		idx &= ht->size - 1;
	}

	return NULL;
}

static void hash_table_upsize(hash_table_t *ht)
{
	size_t       i;
	const size_t new_size  = (ht->size << 1);
	hash_table_t new_table = {
	        bzalloc((ht->type_size + sizeof(struct bucket_header)) * new_size),
	        new_size,
	        BUCKET_LIMIT(new_size),
	        ht->bucket_count,
	        ht->type_size,
	        ht->on_free,
	};

	for (i = 0; i < ht->size; i++) {
		struct bucket_header *bucket = get_bucket(ht, i);
		if (!dstr_is_empty(&bucket->key)) {
			hash_table_set_internal(&new_table, bucket->key.array, bucket->key.size, false, bucket + 1);
		}
	}

	bfree(ht->buckets);
	*ht = new_table;
}

void *hash_table_set(hash_table_t *ht, const char *key, void *val)
{
	return hash_table_set_internal(ht, (char *)key, strlen(key), true, val);
}

void *hash_table_set_n(hash_table_t *ht, const char *key, size_t len, void *val)
{
	return hash_table_set_internal(ht, (char *)key, len, true, val);
}

void *hash_table_get(hash_table_t *ht, const char *key)
{
	if (!ht->size) {
		return NULL;
	}

	size_t   len  = strlen(key);
	uint64_t hash = get_hash(key, len);
	size_t   idx  = (size_t)(hash % (uint64_t)ht->size);

	for (;;) {
		struct bucket_header *bucket = get_bucket(ht, idx);

		if (dstr_is_empty(&bucket->key)) {
			break;
		}

		if (bucket->hash == hash) {
			return ++bucket;
		}

		idx++;
		idx &= ~(ht->size - 1);
	}

	return NULL;
}

void *hash_table_get_idx(hash_table_t *ht, size_t idx, const char **key)
{
	if (idx >= ht->size) {
		if (key) {
			*key = NULL;
		}
		return NULL;
	}

	struct bucket_header *bucket = get_bucket(ht, idx);
	if (key) {
		*key = bucket->key.array;
	}

	return ++bucket;
}
