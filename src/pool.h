/** @license 2021 Neil Edelman, distributed under the terms of the
 [MIT License](https://opensource.org/licenses/MIT).

 @abstract Header <src/pool.h> depends on <src/heap.h> and <src/array.h>;
 examples <test/test_pool.c>; article <doc/pool.pdf>. If on a compatible
 workstation, `make` creates the test suite of the examples.

 @subtitle Stable pool

 ![Example of Pool](../doc/pool.png)

 <tag:<P>pool> is a memory pool that stores only one type, <typedef:<PP>type>,
 using [slab allocation](https://en.wikipedia.org/wiki/Slab_allocation).
 A free-heap in the active-slab allows random-access insertions and deletions.
 Pointers to valid items in the pool are stable, but not generally in any
 order. When removal is ongoing and uniformly sampled while reaching a
 steady-state size, it will eventually settle in one contiguous region.

 @param[POOL_NAME, POOL_TYPE]
 `<P>` that satisfies `C` naming conventions when mangled and a valid tag type,
 <typedef:<PP>type>, associated therewith; required. `<PP>` is private, whose
 names are prefixed in a manner to avoid collisions.

 @depend [array](https://github.com/neil-edelman/array)
 @depend [heap](https://github.com/neil-edelman/heap)
 @std C89; however, when compiling for segmented memory models, C99 with
 `uintptr_t` is recommended because of it's implementation-defined instead of
 undefined-behaviour when comparing pointers from different objects. */

/* `POOL_EXPECT_TRAIT`, `POOL_TO_STRING_NAME`, `POOL_TO_STRING` are
 undocumented because this container is only iterable in the first slab, so
 this is not very useful except for debugging. */

#if !defined(POOL_NAME) || !defined(POOL_TYPE)
#error Name POOL_NAME undefined or tag type POOL_TYPE undefined.
#endif
#if defined(POOL_TO_STRING_NAME) || defined(POOL_TO_STRING)
#define POOL_TO_STRING_TRAIT 1
#else
#define POOL_TO_STRING_TRAIT 0
#endif
#define POOL_TRAITS POOL_TO_STRING_TRAIT
#if POOL_TRAITS > 1
#error Only one trait per include is allowed; use POOL_EXPECT_TRAIT.
#endif
#if defined(POOL_TO_STRING_NAME) && !defined(POOL_TO_STRING)
#error POOL_TO_STRING_NAME requires POOL_TO_STRING.
#endif

#ifndef POOL_H /* <!-- idempotent */
#define POOL_H
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#if defined(POOL_CAT_) || defined(POOL_CAT) || defined(P_) || defined(PP_)
#error Unexpected defines.
#endif
/* <Kernighan and Ritchie, 1988, p. 231>. */
#define POOL_CAT_(n, m) n ## _ ## m
#define POOL_CAT(n, m) POOL_CAT_(n, m)
#define P_(n) POOL_CAT(POOL_NAME, n)
#define PP_(n) POOL_CAT(pool, P_(n))
/** @return An order on `a`, `b` which specifies a max-heap. */
static int pool_index_compare(const size_t a, const size_t b) { return a < b; }
#define HEAP_NAME poolfree
#define HEAP_TYPE size_t
#define HEAP_COMPARE &pool_index_compare
#include "heap.h"
#if !defined(__STDC__) || !defined(__STDC_VERSION__) \
	|| __STDC_VERSION__ < 199901L /* < C99 */
#define POOL_PTR (const void *)
#else /* < C99 --><!-- >= C99 */
#include <stdint.h>
#define POOL_PTR (const uintptr_t)(const void *)
#endif /* >= C99 --> */
#endif /* idempotent --> */


#if POOL_TRAITS == 0 /* <!-- base code */


/* Undocumented: set the initial size. */
#ifndef POOL_SLAB_MIN_CAPACITY /* <!-- !min */
#define POOL_SLAB_MIN_CAPACITY 8
#endif /* !min --> */
#if POOL_SLAB_MIN_CAPACITY < 2
#error Pool slab capacity error.
#endif

/** A valid tag type set by `POOL_TYPE`. */
typedef POOL_TYPE PP_(type);
typedef const POOL_TYPE PP_(type_c);

/* Goes into a slab-sorted array. */
struct PP_(slot) { size_t size; PP_(type) *slab; };
#define ARRAY_NAME PP_(slot)
#define ARRAY_TYPE struct PP_(slot)
#include "array.h"

/** This is a slab memory-manager and free-heap for slab zero. A zeroed pool is
 a valid state. To instantiate to an idle state, see <fn:<P>pool>, `{0}`
 (`C99`,) or being `static`.

 ![States.](../doc/states.png) */
struct P_(pool) {
	struct PP_(slot_array) slots;
	struct poolfree_heap free0; /* Free-heap in slab-zero. */
	size_t capacity0; /* Capacity of slab-zero. */
};

#define BOX_CONTENT PP_(type_c) *
/** Is `x` not null? @implements `is_content` */
static int PP_(is_element_c)(PP_(type_c) *const x) { return !!x; }
/* It is very useful in debugging, is required for `BOX_CONTENT`. Only iterates
 on `slot0` and ignores the free-heap. We don't have enough information to do
 otherwise, since (presumably) the memory address is in local variables and
 will be freed (hopefully.) Unreliable. */
struct PP_(forward) { struct PP_(slot) *slot0; size_t i; };
/** @return Before `p`. @implements `forward` */
static struct PP_(forward) PP_(forward)(const struct P_(pool) *const p)
	{ struct PP_(forward) it; it.slot0 = p && p->slots.size
	? p->slots.data + 0 : 0, it.i = 0; return it; }
/** Move to next `it`. @return Element or null. @implements `next_c` */
static PP_(type_c) *PP_(next_c)(struct PP_(forward) *const it)
	{ return assert(it), it->slot0 && it->i < it->slot0->size
	? it->slot0->slab + it->i++ : 0; }

/* Box override information. */
#define BOX_ PP_
#define BOX struct P_(pool)

/** @return Index of slot that is higher than `x` in `slots`, but treating zero
 as special. @order \O(\log `slots`) */
static size_t PP_(upper)(const struct PP_(slot_array) *const slots,
	const PP_(type) *const x) {
	const struct PP_(slot) *const base = slots->data;
	size_t n, b0, b1;
	assert(slots && x);
	if(!(n = slots->size)) return 0;
	assert(base);
	if(!--n) return 1;
	/* The last one is a special case: it doesn't have an upper bound. */
	for(b0 = 1, --n; n; n /= 2) {
		b1 = b0 + n / 2;
		/* Cast to `void *` then `uintptr_t` if available. */
		if(POOL_PTR x < POOL_PTR base[b1].slab)
			{ continue; }
		else if(POOL_PTR base[b1 + 1].slab <= POOL_PTR x)
			{ b0 = b1 + 1; n--; continue; }
		else
			{ return b1 + 1; }
	}
	return b0 + ((const void *)x >= (const void *)base[slots->size - 1].slab);
}

/** Which slot contains the slab that has `x` in `pool`?
 @order \O(\log `slots`), \O(\log \log `size`)? */
static size_t PP_(slot_idx)(const struct P_(pool) *const pool,
	const PP_(type) *const x) {
	struct PP_(slot) *const base = pool->slots.data;
	size_t up;
	assert(pool && pool->slots.size && base && x);
	/* There's only one option. */
	if(pool->slots.size <= 1
		|| ((const void *)x >= (const void *)base[0].slab
		&& (const void *)x < (const void *)(base[0].slab + pool->capacity0)))
		return assert(pool->slots.size >= 1
		&& (const void *)x >= (const void *)base[0].slab
		&& (const void *)x < (const void *)(base[0].slab + pool->capacity0)), 0;
	up = PP_(upper)(&pool->slots, x);
	return assert(up), up - 1;
}

/** Makes sure there are space for `n` further items in `pool`.
 @return Success. */
static int PP_(buffer)(struct P_(pool) *const pool, const size_t n) {
	const size_t min_size = POOL_SLAB_MIN_CAPACITY,
		max_size = (size_t)-1 / sizeof(PP_(type));
	struct PP_(slot) *base = pool->slots.data, *slot;
	PP_(type) *slab;
	size_t c, insert;
	int is_recycled = 0;
	assert(pool && min_size <= max_size && pool->capacity0 <= max_size &&
		(!pool->slots.size && !pool->free0._.size /* !slots[0] -> !free0 */
		|| pool->slots.size && base
		&& base[0].size <= pool->capacity0
		&& (!pool->free0._.size
		|| pool->free0._.size < base[0].size
		&& pool->free0._.data[0] < base[0].size)));

	/* Ensure space for new slot. */
	if(!n || pool->slots.size && n <= pool->capacity0
		- base[0].size + pool->free0._.size) return 1; /* Already enough. */
	if(max_size < n) return errno = ERANGE, 1; /* Request unsatisfiable. */
	if(!PP_(slot_array_buffer)(&pool->slots, 1)) return 0;
	base = pool->slots.data; /* It may have moved! */

	/* Figure out the capacity of the next slab. */
	c = pool->capacity0;
	if(pool->slots.size && base[0].size) { /* ~Golden ratio. */
		size_t c1 = c + (c >> 1) + (c >> 3);
		c = (c1 < c || c1 > max_size) ? max_size : c1;
	}
	if(c < min_size) c = min_size;
	if(c < n) c = n;

	/* Allocate it; check if the current one is empty. */
	if(pool->slots.size && !base[0].size)
		is_recycled = 1, slab = realloc(base[0].slab, c * sizeof *slab);
	else slab = malloc(c * sizeof *slab);
	if(!slab) { if(!errno) errno = ERANGE; return 0; }
	pool->capacity0 = c; /* We only need to store the capacity of slab 0. */
	if(is_recycled) return base[0].size = 0, base[0].slab = slab, 1;

	/* Evict slot 0. */
	if(!pool->slots.size) insert = 0;
	else insert = PP_(upper)(&pool->slots, base[0].slab);
	assert(insert <= pool->slots.size);
	slot = PP_(slot_array_insert)(&pool->slots, 1, insert);
	assert(slot); /* Made space for it before. */
	slot->slab = base[0].slab, slot->size = base[0].size;
	base[0].slab = slab, base[0].size = 0;
	return 1;
}

/** Either `data` in `pool` is in a secondary slab, in which case it decrements
 the size, or it's the zero-slab, where it gets added to the free-heap.
 @return Success. It may fail due to a free-heap memory allocation error.
 @order Amortized \O(\log \log `items`) @throws[realloc] */
static int PP_(remove)(struct P_(pool) *const pool,
	const PP_(type) *const data) {
	size_t c = PP_(slot_idx)(pool, data);
	struct PP_(slot) *slot = pool->slots.data + c;
	assert(pool && pool->slots.size && data);
	if(!c) { /* It's in the zero-slot, we need to deal with the free-heap. */
		const size_t idx = (size_t)(data - slot->slab);
		assert(pool->capacity0 && slot->size <= pool->capacity0
			&& idx < slot->size);
		if(idx + 1 == slot->size) {
			/* Keep shrinking going while item on the free-heap are exposed. */
			while(--slot->size && poolfree_heap_size(&pool->free0)) {
				const size_t free = *poolfree_heap_peek(&pool->free0);
				if(free < slot->size - 1) break;
				assert(free == slot->size - 1);
				poolfree_heap_pop(&pool->free0);
			}
		} else if(!poolfree_heap_add(&pool->free0, idx)) return 0;
	} else if(assert(slot->size), !--slot->size) {
		PP_(type) *const slab = slot->slab;
		PP_(slot_array_remove)(&pool->slots, pool->slots.data + c);
		free(slab);
	}
	return 1;
}

/** @return An idle pool. @order \Theta(1) @allow */
static struct P_(pool) P_(pool)(void) { struct P_(pool) p;
	p.slots = PP_(slot_array)(), p.free0 = poolfree_heap(), p.capacity0 = 0;
	return p; }

/** Destroys `pool` and returns it to idle. @order \O(\log `data`) @allow */
static void P_(pool_)(struct P_(pool) *const pool) {
	struct PP_(slot) *s, *s_end;
	if(!pool) return;
	for(s = pool->slots.data, s_end = s + pool->slots.size; s < s_end; s++)
		assert(s->slab), free(s->slab);
	PP_(slot_array_)(&pool->slots);
	poolfree_heap_(&pool->free0);
	*pool = P_(pool)();
}

/** Ensure capacity of at least `n` further items in `pool`. Pre-sizing is
 better for contiguous blocks, but takes up that memory.
 @return Success. @throws[ERANGE, malloc] @allow */
static int P_(pool_buffer)(struct P_(pool) *const pool, const size_t n) {
	return assert(pool), PP_(buffer)(pool, n);
}

/** This pointer is constant until it gets <fn:<P>pool_remove>.
 @return A pointer to a new uninitialized element from `pool`.
 @throws[ERANGE, malloc] @order amortised O(1) @allow */
static PP_(type) *P_(pool_new)(struct P_(pool) *const pool) {
	struct PP_(slot) *slot0;
	assert(pool);
	if(!PP_(buffer)(pool, 1)) return 0;
	assert(pool->slots.size && (pool->free0._.size ||
		pool->slots.data[0].size < pool->capacity0));
	if(poolfree_heap_size(&pool->free0)) {
		/* Cheating: we prefer the minimum index from a max-heap, but it
		 doesn't really matter, so take the one off the array used for heap. */
		size_t *free;
		free = heap_poolfree_node_array_pop(&pool->free0._);
		return assert(free), pool->slots.data[0].slab + *free;
	}
	/* The free-heap is empty; guaranteed by <fn:<PP>buffer>. */
	slot0 = pool->slots.data + 0;
	assert(slot0 && slot0->size < pool->capacity0);
	return slot0->slab + slot0->size++;
}

/** Deletes `data` from `pool`. Do not remove data that is not in `pool`.
 @return Success. @order \O(\log \log `items`) @allow */
static int P_(pool_remove)(struct P_(pool) *const pool,
	PP_(type) *const data) { return PP_(remove)(pool, data); }

/** Removes all from `pool`, but keeps it's active state, only freeing the
 smaller blocks. @order \O(\log `items`) @allow */
static void P_(pool_clear)(struct P_(pool) *const pool) {
	struct PP_(slot) *s, *s_end;
	assert(pool);
	if(!pool->slots.size) { assert(!pool->free0._.size); return; }
	for(s = pool->slots.data + 1, s_end = s - 1 + pool->slots.size;
		s < s_end; s++) assert(s->slab && s->size), free(s->slab);
	pool->slots.data[0].size = 0;
	pool->slots.size = 1;
	poolfree_heap_clear(&pool->free0);
}

#ifdef POOL_TEST /* <!-- test */
/* Forward-declare. */
static void (*PP_(to_string))(const PP_(type) *, char (*)[12]);
static const char *(*PP_(pool_to_string))(const struct P_(pool) *);
#include "../test/test_pool.h"
#endif /* test --> */

static void PP_(unused_base_coda)(void);
static void PP_(unused_base)(void) {
	PP_(is_element_c)(0); PP_(forward)(0); PP_(next_c)(0);
	P_(pool)(); P_(pool_)(0); P_(pool_buffer)(0, 0); P_(pool_new)(0);
	P_(pool_remove)(0, 0); P_(pool_clear)(0); PP_(unused_base_coda)();
}
static void PP_(unused_base_coda)(void) { PP_(unused_base)(); }


#elif defined(POOL_TO_STRING) /* base code --><!-- to string trait */


#ifdef POOL_TO_STRING_NAME /* <!-- name */
#define STR_(n) POOL_CAT(P_(pool), POOL_CAT(POOL_TO_STRING_NAME, n))
#else /* name --><!-- !name */
#define STR_(n) POOL_CAT(P_(pool), n)
#endif /* !name --> */
#define TO_STRING POOL_TO_STRING
#include "to_string.h"
#ifdef POOL_TEST /* <!-- expect: greedy satisfy forward-declared. */
#undef POOL_TEST
static PSTR_(to_string_fn) PP_(to_string) = PSTR_(to_string);
static const char *(*PP_(pool_to_string))(const struct P_(pool) *)
	= &STR_(to_string);
#endif /* expect --> */
#undef STR_
#undef POOL_TO_STRING
#ifdef POOL_TO_STRING_NAME
#undef POOL_TO_STRING_NAME
#endif


#endif /* traits --> */


#ifdef POOL_EXPECT_TRAIT /* <!-- trait */
#undef POOL_EXPECT_TRAIT
#else /* trait --><!-- !trait */
#ifdef POOL_TEST
#error No POOL_TO_STRING traits defined for POOL_TEST.
#endif
#undef POOL_NAME
#undef POOL_TYPE
#undef BOX_
#undef BOX
#undef BOX_CONTENT
#endif /* !trait --> */
#undef POOL_TO_STRING_TRAIT
#undef POOL_TRAITS
