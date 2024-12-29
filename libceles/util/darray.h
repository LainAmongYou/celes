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

#include "util-defs.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "bmem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dynamic array.
 *
 * NOTE: Not type-safe when using directly.
 *       Specifying size per call with inline maximizes compiler optimizations
 *
 *       See DARRAY macro at the bottom of the file for slightly safer usage.
 */

#define DARRAY_INVALID ((size_t)-1)

struct darray {
	void  *array;
	size_t size;
	size_t capacity;
};

static inline void darray_init(struct darray *dst)
{
	dst->array    = NULL;
	dst->size     = 0;
	dst->capacity = 0;
}

static inline void darray_free(struct darray *dst)
{
	bfree(dst->array);
	dst->array    = NULL;
	dst->size     = 0;
	dst->capacity = 0;
}

static inline size_t darray_alloc_size(const size_t element_size, const struct darray *da)
{
	return element_size * da->size;
}

static inline void *darray_item(const size_t element_size, const struct darray *da, size_t idx)
{
	return (void *)(((uint8_t *)da->array) + element_size * idx);
}

static inline void *darray_end(const size_t element_size, const struct darray *da)
{
	if (!da->size)
		return NULL;

	return darray_item(element_size, da, da->size - 1);
}

static inline void darray_reserve(const size_t element_size, struct darray *dst, const size_t capacity)
{
	void *ptr;
	if (capacity == 0 || capacity <= dst->capacity)
		return;

	ptr = bmalloc(element_size * capacity);
	if (dst->array) {
		if (dst->size)
			memcpy(ptr, dst->array, element_size * dst->size);

		bfree(dst->array);
	}
	dst->array    = ptr;
	dst->capacity = capacity;
}

static inline void darray_ensure_capacity(const size_t element_size, struct darray *dst, const size_t new_size)
{
	size_t new_cap;
	void  *ptr;
	if (new_size <= dst->capacity)
		return;

	new_cap = (!dst->capacity) ? new_size : dst->capacity * 2;
	if (new_size > new_cap)
		new_cap = new_size;
	ptr = bmalloc(element_size * new_cap);
	if (dst->array) {
		if (dst->capacity)
			memcpy(ptr, dst->array, element_size * dst->capacity);

		bfree(dst->array);
	}
	dst->array    = ptr;
	dst->capacity = new_cap;
}

static inline void darray_clear(struct darray *dst)
{
	dst->size = 0;
}

static inline void darray_resize(const size_t element_size, struct darray *dst, const size_t size)
{
	int    b_clear;
	size_t old_size;

	if (size == dst->size) {
		return;
	} else if (size == 0) {
		dst->size = 0;
		return;
	}

	b_clear  = size > dst->size;
	old_size = dst->size;

	darray_ensure_capacity(element_size, dst, size);
	dst->size = size;

	if (b_clear)
		memset(darray_item(element_size, dst, old_size), 0, element_size * (dst->size - old_size));
}

static inline void darray_copy(const size_t element_size, struct darray *dst, const struct darray *da)
{
	if (da->size == 0) {
		darray_free(dst);
	} else {
		darray_resize(element_size, dst, da->size);
		memcpy(dst->array, da->array, element_size * da->size);
	}
}

static inline void darray_copy_array(const size_t   element_size,
                                     struct darray *dst,
                                     const void    *array,
                                     const size_t   size)
{
	darray_resize(element_size, dst, size);
	memcpy(dst->array, array, element_size * dst->size);
}

static inline void darray_move(struct darray *dst, struct darray *src)
{
	darray_free(dst);
	memcpy(dst, src, sizeof(struct darray));
	src->array    = NULL;
	src->capacity = 0;
	src->size     = 0;
}

static inline size_t darray_find(const size_t element_size, const struct darray *da, const void *item, const size_t idx)
{
	size_t i;

	assert(idx <= da->size);

	for (i = idx; i < da->size; i++) {
		void *compare = darray_item(element_size, da, i);
		if (memcmp(compare, item, element_size) == 0)
			return i;
	}

	return DARRAY_INVALID;
}

static inline size_t darray_push_back(const size_t element_size, struct darray *dst, const void *item)
{
	darray_ensure_capacity(element_size, dst, ++dst->size);
	memcpy(darray_end(element_size, dst), item, element_size);

	return dst->size - 1;
}

static inline void *darray_push_back_new(const size_t element_size, struct darray *dst)
{
	void *last;

	darray_ensure_capacity(element_size, dst, ++dst->size);

	last = darray_end(element_size, dst);
	memset(last, 0, element_size);
	return last;
}

static inline size_t darray_push_back_array(const size_t   element_size,
                                            struct darray *dst,
                                            const void    *array,
                                            const size_t   size)
{
	size_t old_size;
	if (!dst)
		return 0;
	if (!array || !size)
		return dst->size;

	old_size = dst->size;
	darray_resize(element_size, dst, dst->size + size);
	memcpy(darray_item(element_size, dst, old_size), array, element_size * size);

	return old_size;
}

static inline size_t darray_push_back_darray(const size_t element_size, struct darray *dst, const struct darray *da)
{
	return darray_push_back_array(element_size, dst, da->array, da->size);
}

static inline void darray_insert(const size_t element_size, struct darray *dst, const size_t idx, const void *item)
{
	void  *new_item;
	size_t move_count;

	assert(idx <= dst->size);

	if (idx == dst->size) {
		darray_push_back(element_size, dst, item);
		return;
	}

	move_count = dst->size - idx;
	darray_ensure_capacity(element_size, dst, ++dst->size);

	new_item = darray_item(element_size, dst, idx);

	memmove(darray_item(element_size, dst, idx + 1), new_item, move_count * element_size);
	memcpy(new_item, item, element_size);
}

static inline void *darray_insert_new(const size_t element_size, struct darray *dst, const size_t idx)
{
	void  *item;
	size_t move_count;

	assert(idx <= dst->size);
	if (idx == dst->size)
		return darray_push_back_new(element_size, dst);

	move_count = dst->size - idx;
	darray_ensure_capacity(element_size, dst, ++dst->size);

	item = darray_item(element_size, dst, idx);
	memmove(darray_item(element_size, dst, idx + 1), item, move_count * element_size);

	memset(item, 0, element_size);
	return item;
}

static inline void darray_insert_array(const size_t   element_size,
                                       struct darray *dst,
                                       const size_t   idx,
                                       const void    *array,
                                       const size_t   size)
{
	size_t old_size;

	assert(array != NULL);
	assert(size != 0);
	assert(idx <= dst->size);

	old_size = dst->size;
	darray_resize(element_size, dst, dst->size + size);

	memmove(darray_item(element_size, dst, idx + size),
	        darray_item(element_size, dst, idx),
	        element_size * (old_size - idx));
	memcpy(darray_item(element_size, dst, idx), array, element_size * size);
}

static inline void darray_insert_darray(const size_t         element_size,
                                        struct darray       *dst,
                                        const size_t         idx,
                                        const struct darray *da)
{
	darray_insert_array(element_size, dst, idx, da->array, da->size);
}

static inline void darray_erase(const size_t element_size, struct darray *dst, const size_t idx)
{
	assert(idx < dst->size);

	if (idx >= dst->size || !--dst->size)
		return;

	memmove(darray_item(element_size, dst, idx),
	        darray_item(element_size, dst, idx + 1),
	        element_size * (dst->size - idx));
}

static inline void darray_erase_item(const size_t element_size, struct darray *dst, const void *item)
{
	size_t idx = darray_find(element_size, dst, item, 0);
	if (idx != DARRAY_INVALID)
		darray_erase(element_size, dst, idx);
}

static inline void darray_erase_range(const size_t   element_size,
                                      struct darray *dst,
                                      const size_t   start,
                                      const size_t   end)
{
	size_t count, move_count;

	assert(start <= dst->size);
	assert(end <= dst->size);
	assert(end > start);

	count = end - start;
	if (count == 1) {
		darray_erase(element_size, dst, start);
		return;
	} else if (count == dst->size) {
		dst->size = 0;
		return;
	}

	move_count = dst->size - end;
	if (move_count)
		memmove(darray_item(element_size, dst, start),
		        darray_item(element_size, dst, end),
		        move_count * element_size);

	dst->size -= count;
}

static inline void darray_pop_back(const size_t element_size, struct darray *dst)
{
	assert(dst->size != 0);

	if (dst->size)
		darray_erase(element_size, dst, dst->size - 1);
}

static inline void darray_join(const size_t element_size, struct darray *dst, struct darray *da)
{
	darray_push_back_darray(element_size, dst, da);
	darray_free(da);
}

static inline void darray_split(const size_t         element_size,
                                struct darray       *dst1,
                                struct darray       *dst2,
                                const struct darray *da,
                                const size_t         idx)
{
	struct darray temp;

	assert(da->size >= idx);
	assert(dst1 != dst2);

	darray_init(&temp);
	darray_copy(element_size, &temp, da);

	darray_free(dst1);
	darray_free(dst2);

	if (da->size) {
		if (idx)
			darray_copy_array(element_size, dst1, temp.array, temp.size);
		if (idx < temp.size - 1)
			darray_copy_array(element_size, dst2, darray_item(element_size, &temp, idx), temp.size - idx);
	}

	darray_free(&temp);
}

static inline void darray_move_item(const size_t element_size, struct darray *dst, const size_t from, const size_t to)
{
	void *temp, *p_from, *p_to;

	if (from == to)
		return;

	temp = malloc(element_size);
	if (!temp) {
		crash("darray_move_item: out of memory");
		return;
	}

	p_from = darray_item(element_size, dst, from);
	p_to   = darray_item(element_size, dst, to);

	memcpy(temp, p_from, element_size);

	if (to < from)
		memmove(darray_item(element_size, dst, to + 1), p_to, element_size * (from - to));
	else
		memmove(p_from, darray_item(element_size, dst, from + 1), element_size * (to - from));

	memcpy(p_to, temp, element_size);
	free(temp);
}

static inline void darray_swap(const size_t element_size, struct darray *dst, const size_t a, const size_t b)
{
	void *temp, *a_ptr, *b_ptr;

	assert(a < dst->size);
	assert(b < dst->size);

	if (a == b)
		return;

	temp = malloc(element_size);
	if (!temp)
		crash("darray_swap: out of memory");

	a_ptr = darray_item(element_size, dst, a);
	b_ptr = darray_item(element_size, dst, b);

	memcpy(temp, a_ptr, element_size);
	memcpy(a_ptr, b_ptr, element_size);
	memcpy(b_ptr, temp, element_size);

	free(temp);
}

/*
 * Defines to make dynamic arrays more type-safe.
 * Note: Still not 100% type-safe but much better than using darray directly
 *       Makes it a little easier to use as well.
 *
 *       I did -not- want to use a gigantic macro to generate a crapload of
 *       typesafe inline functions per type.  It just feels like a mess to me.
 */

#define DARRAY(type)                                                                                                   \
	struct {                                                                                                       \
		type  *array;                                                                                          \
		size_t size;                                                                                           \
		size_t capacity;                                                                                       \
	}

#define da_init(v) darray_init((struct darray *)&(v))

#define da_free(v) darray_free((struct darray *)&(v))

#define da_alloc_size(v) (sizeof(*(v).array) * (v).size)

#define da_end(v) darray_end(sizeof(*(v).array), (struct darray *)&(v))

#define da_reserve(v, capacity) darray_reserve(sizeof(*(v).array), (struct darray *)&(v), capacity)

#define da_resize(v, size) darray_resize(sizeof(*(v).array), (struct darray *)&(v), size)

#define da_clear(v) darray_clear((struct darray *)&(v))

#define da_copy(dst, src) darray_copy(sizeof(*(dst).array), (struct darray *)&(dst), &(src))

#define da_copy_array(dst, src_array, n) darray_copy_array(sizeof(*(dst).array), (struct darray *)&(dst), src_array, n)

#define da_move(dst, src) darray_move((struct darray *)&(dst), (struct darray *)&(src))

#ifdef ENABLE_DARRAY_TYPE_TEST
#ifdef __cplusplus
#define da_type_test(v, item)                                                                                          \
	({                                                                                                             \
		if (false) {                                                                                           \
			auto _t = (v).array;                                                                           \
			_t      = (item);                                                                              \
			(void)_t;                                                                                      \
			*(v).array = *(item);                                                                          \
		}                                                                                                      \
	})
#else
#define da_type_test(v, item)                                                                                          \
	({                                                                                                             \
		if (false) {                                                                                           \
			const typeof(*(v).array) *_t;                                                                  \
			_t = (item);                                                                                   \
			(void)_t;                                                                                      \
			*(v).array = *(item);                                                                          \
		}                                                                                                      \
	})
#endif
#endif // ENABLE_DARRAY_TYPE_TEST

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_find(v, item, idx)                                                                                          \
	({                                                                                                             \
		da_type_test(v, item);                                                                                 \
		darray_find(sizeof(*(v).array), &(v), item, idx);                                                      \
	})
#else
#define da_find(v, item, idx) darray_find(sizeof(*(v).array), &(v), item, idx)
#endif

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_push_back(v, item)                                                                                          \
	({                                                                                                             \
		da_type_test(v, item);                                                                                 \
		darray_push_back(sizeof(*(v).array), &(v), item);                                                      \
	})
#else
#define da_push_back(v, item) darray_push_back(sizeof(*(v).array), (struct darray *)&(v), item)
#endif

#ifdef __GNUC__
/* GCC 12 with -O2 generates a warning -Wstringop-overflow in da_push_back_new,
 * which could be false positive. Extract the macro here to avoid the warning.
 */
#define da_push_back_new(v)                                                                                            \
	({                                                                                                             \
		__typeof__(v) *d = &(v);                                                                               \
		darray_ensure_capacity(sizeof(*d->array), (struct darray *)d, ++d->size);                              \
		memset(&d->array[d->size - 1], 0, sizeof(*d->array));                                                  \
		&d->array[d->size - 1];                                                                                \
	})
#else
#define da_push_back_new(v) darray_push_back_new(sizeof(*(v).array), (struct darray *)&(v))
#endif

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_push_back_array(dst, src_array, n)                                                                          \
	({                                                                                                             \
		da_type_test(dst, src_array);                                                                          \
		darray_push_back_array(sizeof(*(dst).array), (struct darray *)&(dst), src_array, n);                   \
	})
#else
#define da_push_back_array(dst, src_array, n)                                                                          \
	darray_push_back_array(sizeof(*(dst).array), (struct darray *)&(dst), src_array, n)
#endif

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_push_back_da(dst, src)                                                                                      \
	({                                                                                                             \
		da_type_test(dst, (src).array);                                                                        \
		darray_push_back_darray(sizeof(*(dst).array), (struct darray *)&(dst), (struct darray *)&(src));       \
	})
#else
#define da_push_back_da(dst, src)                                                                                      \
	darray_push_back_darray(sizeof(*(dst).array), (struct darray *)&(dst), (struct darray *)&(src))
#endif

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_insert(v, idx, item)                                                                                        \
	({                                                                                                             \
		da_type_test(v, item);                                                                                 \
		darray_insert(sizeof(*(v).array), (struct darray *)&(v), idx, item);                                   \
	})
#else
#define da_insert(v, idx, item) darray_insert(sizeof(*(v).array), (struct darray *)&(v), idx, item)
#endif

#define da_insert_new(v, idx) darray_insert_new(sizeof(*(v).array), (struct darray *)&(v), idx)

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_insert_array(dst, idx, src_array, n)                                                                        \
	({                                                                                                             \
		da_type_test(dst, src_array);                                                                          \
		darray_insert_array(sizeof(*(dst).array), (struct darray *)&(dst), idx, src_array, n);                 \
	})
#else
#define da_insert_array(dst, idx, src_array, n)                                                                        \
	darray_insert_array(sizeof(*(dst).array), (struct darray *)&(dst), idx, src_array, n)
#endif

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_insert_da(dst, idx, src)                                                                                    \
	({                                                                                                             \
		da_type_test(dst, (src).array);                                                                        \
		darray_insert_darray(sizeof(*(dst).array), (struct darray *)&(dst), idx, (struct darray *)&(src));     \
	})
#else
#define da_insert_da(dst, idx, src)                                                                                    \
	darray_insert_darray(sizeof(*(dst).array), (struct darray *)&(dst), idx, (struct darray *)&(src))
#endif

#define da_erase(dst, idx) darray_erase(sizeof(*(dst).array), (struct darray *)&(dst), idx)

#ifdef ENABLE_DARRAY_TYPE_TEST
#define da_erase_item(dst, item)                                                                                       \
	({                                                                                                             \
		da_type_test(dst, item);                                                                               \
		darray_erase_item(sizeof(*(dst).array), (struct darray *)&(dst), item);                                \
	})
#else
#define da_erase_item(dst, item) darray_erase_item(sizeof(*(dst).array), (struct darray *)&(dst), item)
#endif

#define da_erase_range(dst, from, to) darray_erase_range(sizeof(*(dst).array), (struct darray *)&(dst), from, to)

#define da_pop_back(dst) darray_pop_back(sizeof(*(dst).array), (struct darray *)&(dst));

#define da_join(dst, src) darray_join(sizeof(*(dst).array), (struct darray *)&(dst), (struct darray *)&(src))

#define da_split(dst1, dst2, src, idx)                                                                                 \
	darray_split(sizeof(*(src).array),                                                                             \
	             (struct darray *)&(dst1),                                                                         \
	             (struct darray *)&(dst2),                                                                         \
	             (struct darray *)&(src),                                                                          \
	             idx)

#define da_move_item(v, from, to) darray_move_item(sizeof(*(v).array), (struct darray *)&(v), from, to)

#define da_swap(v, idx1, idx2) darray_swap(sizeof(*(v).array), (struct darray *)&(v), idx1, idx2)

#ifdef __cplusplus
}
#endif
