#include "automaton.h"

/**
 * @file
 * @brief Transition table packing
 *
 * Each state may get a parent (template): a state whose transitions are mostly the same, so only
 * the differences are stored. Without a parent, only the transitions to non-dead states are stored
 *
 * The stored transitions of all states are placed in shared arrays (a 'comb vector'), first-fit,
 * the states with the most stored transitions first
 *
 * Parents are chosen among states processed earlier, which keeps chains acyclic, and chains are at
 * most @ref DFA_PACK_MAX_DEPTH long
 */

/** @brief Number of most-targeted states, and of recently processed states, tried as parents */
#define CANDIDATES 16

/** @brief Values sorted by @ref compare_by_key (qsort has no context argument) */
static const size_t* sort_keys;

/** @brief Order indices by decreasing key, then by index */
[[nodiscard]] static int
compare_by_key(const void* a, const void* b)
{
	const size_t x = *(const size_t*)a;
	const size_t y = *(const size_t*)b;
	if (sort_keys[x] != sort_keys[y])
		return sort_keys[x] > sort_keys[y] ? -1 : 1;
	return (x > y) - (x < y);
}

/** @brief States sorted by decreasing @p keys */
[[nodiscard]] static size_t*
sorted_states(const size_t* keys, size_t n)
{
	size_t* order = xmalloc(n * sizeof(size_t));
	for (size_t s = 0; s < n; ++s)
		order[s] = s;
	sort_keys = keys;
	qsort(order, n, sizeof(size_t), compare_by_key);
	return order;
}

/** @brief Number of classes where rows @p a and @p b differ */
[[nodiscard]] static size_t
row_distance(const size_t* a, const size_t* b, size_t k)
{
	size_t distance = 0;
	for (size_t c = 0; c < k; ++c)
		distance += a[c] != b[c];
	return distance;
}

/** @brief Most frequent non-dead target of @p row, @ref DFA_DEAD if none */
[[nodiscard]] static size_t
most_frequent_target(const size_t* row, size_t k)
{
	size_t best = DFA_DEAD;
	size_t best_count = 0;
	for (size_t c = 0; c < k; ++c) {
		if (row[c] == DFA_DEAD || row[c] == best)
			continue;
		size_t count = 0;
		for (size_t j = 0; j < k; ++j)
			count += row[j] == row[c];
		if (count > best_count) {
			best = row[c];
			best_count = count;
		}
	}
	return best;
}

/** @brief Choose the parent of every state, and count the transitions each state must store */
static void
choose_parents(const dfa* d, size_t* parent, size_t* stored)
{
	const size_t n = d->state_count;
	const size_t k = d->class_count;

	/* Popular states first: they are the likeliest parents, and must be processed before */
	size_t* in_degree = xmalloc(n * sizeof(size_t));
	for (size_t s = 0; s < n; ++s)
		in_degree[s] = 0;
	for (size_t i = 0; i < n * k; ++i)
		if (d->transitions[i] != DFA_DEAD)
			++in_degree[d->transitions[i]];
	size_t* order = sorted_states(in_degree, n);
	free(in_degree);

	bool* processed = xmalloc(n * sizeof(bool));
	size_t* depth = xmalloc(n * sizeof(size_t));
	for (size_t s = 0; s < n; ++s)
		processed[s] = false;

	for (size_t i = 0; i < n; ++i) {
		const size_t s = order[i];
		const size_t* row = d->transitions + s * k;

		/* Without parent: the transitions to non-dead states */
		size_t best = DFA_PACK_NONE;
		size_t best_cost = 0;
		for (size_t c = 0; c < k; ++c)
			best_cost += row[c] != DFA_DEAD;

		/* Candidates: the most frequent target, the most targeted states, recent states */
		size_t candidates[2 * CANDIDATES + 1];
		size_t count = 0;
		candidates[count++] = most_frequent_target(row, k);
		for (size_t j = 0; j < CANDIDATES && j < i; ++j)
			candidates[count++] = order[j];
		for (size_t j = i > CANDIDATES ? i - CANDIDATES : 0; j < i; ++j)
			candidates[count++] = order[j];

		for (size_t j = 0; j < count; ++j) {
			const size_t candidate = candidates[j];
			if (candidate == DFA_DEAD || !processed[candidate] ||
			    depth[candidate] + 1 > DFA_PACK_MAX_DEPTH)
				continue;
			const size_t cost = row_distance(row, d->transitions + candidate * k, k);
			if (cost < best_cost) {
				best = candidate;
				best_cost = cost;
			}
		}

		parent[s] = best;
		stored[s] = best_cost;
		depth[s] = best == DFA_PACK_NONE ? 0 : depth[best] + 1;
		processed[s] = true;
	}
	free(order);
	free(processed);
	free(depth);
}

/** @brief Arrays being filled */
typedef struct
{
	dfa_packed* packed;
	size_t capacity;
	/**
	 * @brief Next free entry: `free_next[i] == i` when entry `i` is free, otherwise an entry after
	 * it to search from. Entries past @ref capacity are free.
	 *
	 * Searches compress the paths they follow (as in union-find), so finding the first free entry
	 * after a position takes amortized near-constant time
	 */
	size_t* free_next;
} packing;

/** @brief Make the arrays hold at least @p size entries */
static void
reserve(packing* p, size_t size)
{
	if (size <= p->capacity)
		return;
	const size_t old = p->capacity;
	p->capacity = size * 2;
	p->packed->next = xrealloc(p->packed->next, p->capacity * sizeof(size_t));
	p->packed->check = xrealloc(p->packed->check, p->capacity * sizeof(size_t));
	p->free_next = xrealloc(p->free_next, p->capacity * sizeof(size_t));
	for (size_t j = old; j < p->capacity; ++j) {
		p->packed->next[j] = DFA_DEAD;
		p->packed->check[j] = DFA_PACK_NONE;
		p->free_next[j] = j;
	}
}

/** @brief First free entry at or after @p i */
[[nodiscard]] static size_t
find_free(packing* p, size_t i)
{
	size_t root = i;
	while (root < p->capacity && p->free_next[root] != root)
		root = p->free_next[root];
	while (i < p->capacity && p->free_next[i] != i) {
		const size_t next = p->free_next[i];
		p->free_next[i] = root;
		i = next;
	}
	return root;
}

dfa_packed
dfa_pack(const dfa* d)
{
	const size_t n = d->state_count;
	const size_t k = d->class_count;
	dfa_packed packed = {
		.base = xmalloc(n * sizeof(size_t)),
		.parent = xmalloc(n * sizeof(size_t)),
		.size = k,
	};

	size_t* stored = xmalloc(n * sizeof(size_t));
	choose_parents(d, packed.parent, stored);
	size_t* order = sorted_states(stored, n);

	/* First-fit placement. Every lookup reads `base + class`, so the arrays always span `k`
	 * entries past each base */
	packing p = { .packed = &packed };
	reserve(&p, k);
	size_t* classes = xmalloc(k * sizeof(size_t));
	for (size_t i = 0; i < n; ++i) {
		const size_t s = order[i];
		const size_t* row = d->transitions + s * k;
		const size_t* parent_row =
		  packed.parent[s] == DFA_PACK_NONE ? NULL : d->transitions + packed.parent[s] * k;

		size_t count = 0;
		for (size_t c = 0; c < k; ++c)
			if (parent_row ? row[c] != parent_row[c] : row[c] != DFA_DEAD)
				classes[count++] = c;
		if (count == 0) {
			packed.base[s] = 0;
			continue;
		}

		/* Lowest base where the first stored class has a free entry, then where all of them do */
		size_t base = 0;
		for (;; ++base) {
			base = find_free(&p, base + classes[0]) - classes[0];
			reserve(&p, base + k);
			bool fits = true;
			for (size_t j = 1; fits && j < count; ++j)
				fits = packed.check[base + classes[j]] == DFA_PACK_NONE;
			if (fits)
				break;
		}

		packed.base[s] = base;
		for (size_t j = 0; j < count; ++j) {
			const size_t entry = base + classes[j];
			packed.next[entry] = row[classes[j]];
			packed.check[entry] = s;
			p.free_next[entry] = entry + 1;
		}
		if (base + k > packed.size)
			packed.size = base + k;
	}
	free(classes);
	free(p.free_next);
	free(order);
	free(stored);

#ifndef NDEBUG
	for (size_t s = 0; s < n; ++s)
		for (size_t c = 0; c < k; ++c)
			assert(dfa_packed_next(&packed, s, c) == d->transitions[s * k + c]);
#endif
	return packed;
}

void
dfa_packed_destroy(dfa_packed* packed)
{
	free(packed->base);
	free(packed->parent);
	free(packed->next);
	free(packed->check);
	*packed = (dfa_packed){};
}
