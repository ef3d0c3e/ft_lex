#include "automaton.h"

/* {{{ Sets of NFA states */

/** @brief Sorted set of NFA states, identifying a DFA state */
typedef struct
{
	size_t* ids;
	size_t len;
} state_set;

[[nodiscard]] static uint64_t
state_set_hash(state_set set)
{
	return cash_hash_bytes(set.ids, set.len * sizeof(*set.ids));
}

[[nodiscard]] static bool
state_set_eq(state_set a, state_set b)
{
	return a.len == b.len && (a.len == 0 || !memcmp(a.ids, b.ids, a.len * sizeof(*a.ids)));
}

[[nodiscard]] static state_set
state_set_copy(state_set set)
{
	state_set copy = { xmalloc(set.len * sizeof(*set.ids)), set.len };
	if (set.len)
		memcpy(copy.ids, set.ids, set.len * sizeof(*set.ids));
	return copy;
}

static void
state_set_free(state_set set)
{
	free(set.ids);
}

/* Maps a set of NFA states to its DFA state */
#define state_map_key_type state_set
#define state_map_value_type size_t
#define state_map_hash state_set_hash
#define state_map_eq state_set_eq
#define state_map_key_copy state_set_copy
#define state_map_key_free state_set_free
#define state_map_value_copy CASH_COPY_TRIVIAL
#define state_map_value_free CASH_FREE_NOOP
CASH_HM(state_map, state_map);

[[nodiscard]] static int
compare_ids(const void* a, const void* b)
{
	const size_t x = *(const size_t*)a;
	const size_t y = *(const size_t*)b;
	return (x > y) - (x < y);
}

/** @brief Sort @p ids and remove duplicates, return the new length */
[[nodiscard]] static size_t
sort_unique(size_t* ids, size_t len)
{
	if (len == 0)
		return 0;
	qsort(ids, len, sizeof(*ids), compare_ids);
	size_t out = 1;
	for (size_t i = 1; i < len; ++i)
		if (ids[i] != ids[out - 1])
			ids[out++] = ids[i];
	return out;
}

/* }}} */

typedef struct
{
	const nfa* n;
	dfa* d;

	/** @brief Owns the NFA state sets */
	struct state_map map;
	/** @brief NFA states of each DFA state, borrowed from @ref map */
	state_set* sets;
	size_t sets_capacity;

	/** @brief Epsilon closure work stack */
	size_t* stack;
	/** @brief `visited[id] == generation` when NFA state `id` is in the current closure */
	size_t* visited;
	size_t generation;
} builder;

/**
 * @brief Compute the epsilon closure of @p seeds
 *
 * @return The closure, sorted, as a newly allocated set
 */
[[nodiscard]] static state_set
closure(builder* b, const size_t* seeds, size_t seed_count)
{
	const nfa* n = b->n;
	++b->generation;

	size_t* result = xmalloc(n->state_count * sizeof(*result));
	size_t len = 0;
	size_t top = 0;
	for (size_t i = 0; i < seed_count; ++i) {
		if (b->visited[seeds[i]] == b->generation)
			continue;
		b->visited[seeds[i]] = b->generation;
		b->stack[top++] = seeds[i];
	}

	while (top != 0) {
		const size_t id = b->stack[--top];
		result[len++] = id;
		const nfa_state* state = &n->states[id];
		for (size_t i = 0; i < state->edge_count; ++i) {
			const nfa_edge* edge = &state->edges[i];
			if (!edge->epsilon || b->visited[edge->target] == b->generation)
				continue;
			b->visited[edge->target] = b->generation;
			b->stack[top++] = edge->target;
		}
	}

	len = sort_unique(result, len);
	return (state_set){ result, len };
}

/**
 * @brief Collect the rules of @p set's states, sorted and deduplicated
 *
 * @param accepts Collect accepted rules if true, rules whose pattern ends otherwise
 */
static void
collect_rules(const nfa* n,
              state_set set,
              bool accepts,
              size_t** out,
              size_t* out_count)
{
	size_t* rules = xmalloc(set.len * sizeof(*rules));
	size_t len = 0;
	for (size_t i = 0; i < set.len; ++i) {
		const nfa_state* state = &n->states[set.ids[i]];
		const size_t rule = accepts ? state->accept : state->head_end;
		if (rule != NFA_NONE)
			rules[len++] = rule;
	}
	*out_count = sort_unique(rules, len);
	if (*out_count == 0) {
		free(rules);
		rules = NULL;
	}
	*out = rules;
}

/**
 * @brief Get the DFA state for the NFA states in @p set, creating it if needed
 *
 * Takes ownership of @p set
 */
[[nodiscard]] static size_t
intern(builder* b, state_set set)
{
	const size_t* found = state_map_get(&b->map, set);
	if (found) {
		state_set_free(set);
		return *found;
	}

	dfa* d = b->d;
	const size_t index = d->state_count++;
	if (index == b->sets_capacity) {
		b->sets_capacity = b->sets_capacity ? b->sets_capacity * 2 : 64;
		b->sets = xrealloc(b->sets, b->sets_capacity * sizeof(*b->sets));
		d->states = xrealloc(d->states, b->sets_capacity * sizeof(*d->states));
		d->transitions = xrealloc(
		  d->transitions, b->sets_capacity * d->class_count * sizeof(*d->transitions));
	}
	for (size_t c = 0; c < d->class_count; ++c)
		d->transitions[index * d->class_count + c] = DFA_DEAD;

	dfa_state* state = &d->states[index];
	collect_rules(b->n, set, true, &state->accepts, &state->accept_count);
	collect_rules(b->n, set, false, &state->head_ends, &state->head_end_count);

	b->sets[index] = set;
	if (!state_map_insert(&b->map, set, index)) {
		perror("malloc");
		exit(1);
	}
	return index;
}

/**
 * @brief Compute byte equivalence classes
 *
 * Two bytes are in the same class when every transition of the NFA either accepts both or
 * neither of them. Classes are refined by each transition's set.
 */
static void
compute_byte_classes(const nfa* n, dfa* d)
{
	for (size_t c = 0; c < 256; ++c)
		d->byte_class[c] = 0;
	d->class_count = 1;

	/* `remap[class * 2 + in_set]` is the refined class */
	size_t remap[512];
	for (size_t i = 0; i < n->state_count; ++i) {
		const nfa_state* state = &n->states[i];
		for (size_t e = 0; e < state->edge_count; ++e) {
			const nfa_edge* edge = &state->edges[e];
			if (edge->epsilon)
				continue;

			for (size_t k = 0; k < d->class_count * 2; ++k)
				remap[k] = SIZE_MAX;
			size_t count = 0;
			for (size_t c = 0; c < 256; ++c) {
				const size_t key =
				  d->byte_class[c] * 2 + regex_set_contains(&edge->set, (unsigned char)c);
				if (remap[key] == SIZE_MAX)
					remap[key] = count++;
				d->byte_class[c] = remap[key];
			}
			d->class_count = count;
		}
	}
}

dfa
dfa_build(const nfa* n)
{
	dfa d = {};
	compute_byte_classes(n, &d);

	/* A byte representative of each class */
	unsigned char representative[256];
	for (size_t c = 256; c-- > 0;)
		representative[d.byte_class[c]] = (unsigned char)c;

	builder b = {
		.n = n,
		.d = &d,
		.stack = xmalloc(n->state_count * sizeof(size_t)),
		.visited = xmalloc(n->state_count * sizeof(size_t)),
	};
	for (size_t i = 0; i < n->state_count; ++i)
		b.visited[i] = 0;

	d.start_count = n->start_count;
	d.starts = xmalloc(d.start_count * sizeof(*d.starts));
	for (size_t i = 0; i < n->start_count; ++i)
		d.starts[i] = intern(&b, closure(&b, &n->starts[i], 1));

	/* Subset construction, states are processed in creation order */
	/* Each byte transition contributes at most one target */
	size_t edge_count = 0;
	for (size_t i = 0; i < n->state_count; ++i)
		edge_count += n->states[i].edge_count;
	size_t* targets = xmalloc(edge_count * sizeof(*targets));
	for (size_t index = 0; index < d.state_count; ++index) {
		for (size_t c = 0; c < d.class_count; ++c) {
			/* `b.sets` may be reallocated by `intern` */
			const state_set set = b.sets[index];
			size_t target_count = 0;
			for (size_t i = 0; i < set.len; ++i) {
				const nfa_state* state = &n->states[set.ids[i]];
				for (size_t e = 0; e < state->edge_count; ++e) {
					const nfa_edge* edge = &state->edges[e];
					if (!edge->epsilon && regex_set_contains(&edge->set, representative[c]))
						targets[target_count++] = edge->target;
				}
			}
			if (target_count == 0)
				continue;

			const size_t next = intern(&b, closure(&b, targets, target_count));
			d.transitions[index * d.class_count + c] = next;
		}
	}
	free(targets);

	d.rule_count = n->rule_count;
	d.rules = xmalloc(d.rule_count * sizeof(*d.rules));
	d.reverse_trailing = xmalloc(d.rule_count * sizeof(*d.reverse_trailing));
	for (size_t i = 0; i < d.rule_count; ++i) {
		d.rules[i] = n->rules[i];
		d.reverse_trailing[i] = n->rules[i].kind == kTrailingVariable
		                          ? dfa_build(&n->reverse_trailing[i])
		                          : (dfa){};
	}

	state_map_destroy(&b.map);
	free(b.sets);
	free(b.stack);
	free(b.visited);
	return d;
}

void
dfa_destroy(dfa* d)
{
	for (size_t i = 0; i < d->state_count; ++i) {
		free(d->states[i].accepts);
		free(d->states[i].head_ends);
	}
	free(d->states);
	free(d->transitions);
	free(d->starts);
	for (size_t i = 0; i < d->rule_count; ++i)
		dfa_destroy(&d->reverse_trailing[i]);
	free(d->reverse_trailing);
	free(d->rules);
	*d = (dfa){};
}
