#include "automaton.h"

/**
 * @file
 * @brief DFA minimization, with Hopcroft's partition refinement
 *
 * States are equivalent when they accept the same rules (in the same order, for `REJECT`), end
 * the same patterns (for variable trailing context), and their transitions lead to equivalent
 * states. The minimal DFA therefore scans exactly like the original one.
 *
 * The dead state is added as an explicit state (index `state_count`), so missing transitions
 * become transitions to it.
 */

/** @brief Compare two lists of rules */
[[nodiscard]] static int
compare_lists(const size_t* a, size_t a_len, const size_t* b, size_t b_len)
{
	if (a_len != b_len)
		return a_len < b_len ? -1 : 1;
	for (size_t i = 0; i < a_len; ++i)
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return 0;
}

/** @brief Compare the rules accepted, then the patterns ended, by states @p x and @p y */
[[nodiscard]] static int
compare_rules(const dfa* d, size_t x, size_t y)
{
	static const dfa_state dead = {};
	const dfa_state* sx = x < d->state_count ? &d->states[x] : &dead;
	const dfa_state* sy = y < d->state_count ? &d->states[y] : &dead;
	const int order = compare_lists(sx->accepts, sx->accept_count, sy->accepts, sy->accept_count);
	if (order != 0)
		return order;
	return compare_lists(sx->head_ends, sx->head_end_count, sy->head_ends, sy->head_end_count);
}

/** @brief DFA being minimized, for @ref compare_states (qsort has no context argument) */
static const dfa* sorted_dfa;

/** @brief Order states by @ref compare_rules, then by index */
[[nodiscard]] static int
compare_states(const void* a, const void* b)
{
	const size_t x = *(const size_t*)a;
	const size_t y = *(const size_t*)b;
	const int order = compare_rules(sorted_dfa, x, y);
	return order != 0 ? order : (x > y) - (x < y);
}

/**
 * @brief Partition of the states into blocks
 *
 * The states of block `b` are `elements[first[b]..end[b])`. Marked states of a block are moved to
 * the start of its range, `marked[b]` counts them.
 */
typedef struct
{
	size_t* elements;
	/** @brief Position of each state in @ref elements */
	size_t* location;
	/** @brief Block of each state */
	size_t* block;
	size_t* first;
	size_t* end;
	size_t* marked;
	size_t count;

	/** @brief Blocks with marked states */
	size_t* touched;
	size_t touched_count;

	/** @brief Blocks waiting to be used as splitters */
	size_t* worklist;
	size_t worklist_count;
	bool* in_worklist;
} partition;

static void
push_worklist(partition* p, size_t block)
{
	if (p->in_worklist[block])
		return;
	p->in_worklist[block] = true;
	p->worklist[p->worklist_count++] = block;
}

static void
mark(partition* p, size_t state)
{
	const size_t b = p->block[state];
	const size_t i = p->location[state];
	const size_t j = p->first[b] + p->marked[b];
	/* Already in the marked prefix */
	if (i < j)
		return;
	const size_t other = p->elements[j];
	p->elements[j] = state;
	p->elements[i] = other;
	p->location[state] = j;
	p->location[other] = i;
	if (p->marked[b]++ == 0)
		p->touched[p->touched_count++] = b;
}

/** @brief Split every touched block into its marked and unmarked states */
static void
split_touched(partition* p)
{
	for (size_t t = 0; t < p->touched_count; ++t) {
		const size_t b = p->touched[t];
		const size_t marked = p->marked[b];
		const size_t size = p->end[b] - p->first[b];
		p->marked[b] = 0;
		if (marked == size)
			continue;

		/* The marked states form a new block */
		const size_t nb = p->count++;
		p->first[nb] = p->first[b];
		p->end[nb] = p->first[b] + marked;
		p->marked[nb] = 0;
		p->in_worklist[nb] = false;
		p->first[b] += marked;
		for (size_t i = p->first[nb]; i < p->end[nb]; ++i)
			p->block[p->elements[i]] = nb;

		/* Hopcroft: both halves if the block was waiting, otherwise the smaller one suffices */
		if (p->in_worklist[b] || marked < size - marked)
			push_worklist(p, nb);
		else
			push_worklist(p, b);
	}
	p->touched_count = 0;
}

[[nodiscard]] static size_t*
copy_list(const size_t* list, size_t len)
{
	if (len == 0)
		return NULL;
	size_t* copy = xmalloc(len * sizeof(*copy));
	memcpy(copy, list, len * sizeof(*copy));
	return copy;
}

dfa
dfa_minimize(const dfa* d)
{
	const size_t n = d->state_count;
	const size_t total = n + 1; /* With the dead state */
	const size_t k = d->class_count;
#define TARGET(s, c)                                                                             \
	((s) == n || d->transitions[(s) * k + (c)] == DFA_DEAD ? n : d->transitions[(s) * k + (c)])

	/* Initial partition: states accepting the same rules */
	partition p = {
		.elements = xmalloc(total * sizeof(size_t)),
		.location = xmalloc(total * sizeof(size_t)),
		.block = xmalloc(total * sizeof(size_t)),
		.first = xmalloc(total * sizeof(size_t)),
		.end = xmalloc(total * sizeof(size_t)),
		.marked = xmalloc(total * sizeof(size_t)),
		.touched = xmalloc(total * sizeof(size_t)),
		.worklist = xmalloc(total * sizeof(size_t)),
		.in_worklist = xmalloc(total * sizeof(bool)),
	};
	for (size_t s = 0; s < total; ++s)
		p.elements[s] = s;
	sorted_dfa = d;
	qsort(p.elements, total, sizeof(size_t), compare_states);
	for (size_t i = 0; i < total; ++i) {
		const size_t s = p.elements[i];
		if (i == 0 || compare_rules(d, p.elements[i - 1], s) != 0) {
			p.first[p.count] = i;
			p.marked[p.count] = 0;
			p.in_worklist[p.count] = false;
			++p.count;
		}
		p.end[p.count - 1] = i + 1;
		p.block[s] = p.count - 1;
		p.location[s] = i;
	}
	for (size_t b = 0; b < p.count; ++b)
		push_worklist(&p, b);

	/* Predecessors of each state, as (source, class) pairs */
	size_t* pred_start = xmalloc((total + 1) * sizeof(size_t));
	for (size_t t = 0; t <= total; ++t)
		pred_start[t] = 0;
	for (size_t s = 0; s < total; ++s)
		for (size_t c = 0; c < k; ++c)
			++pred_start[TARGET(s, c) + 1];
	for (size_t t = 0; t < total; ++t)
		pred_start[t + 1] += pred_start[t];
	size_t* pred_source = xmalloc(total * k * sizeof(size_t));
	size_t* pred_class = xmalloc(total * k * sizeof(size_t));
	{
		size_t* fill = xmalloc(total * sizeof(size_t));
		memcpy(fill, pred_start, total * sizeof(size_t));
		for (size_t s = 0; s < total; ++s)
			for (size_t c = 0; c < k; ++c) {
				const size_t i = fill[TARGET(s, c)]++;
				pred_source[i] = s;
				pred_class[i] = c;
			}
		free(fill);
	}

	/* Refinement */
	size_t* splitter = xmalloc(total * sizeof(size_t));
	size_t* bucket_start = xmalloc((k + 1) * sizeof(size_t));
	size_t* bucket = NULL;
	size_t bucket_capacity = 0;
	while (p.worklist_count != 0) {
		const size_t b = p.worklist[--p.worklist_count];
		p.in_worklist[b] = false;

		/* The splitter's states, as they are now: splits below may change the block */
		const size_t size = p.end[b] - p.first[b];
		memcpy(splitter, p.elements + p.first[b], size * sizeof(size_t));

		/* Predecessors of the splitter, grouped by class (counting sort) */
		for (size_t c = 0; c <= k; ++c)
			bucket_start[c] = 0;
		size_t pred_count = 0;
		for (size_t i = 0; i < size; ++i)
			for (size_t j = pred_start[splitter[i]]; j < pred_start[splitter[i] + 1]; ++j) {
				++bucket_start[pred_class[j] + 1];
				++pred_count;
			}
		if (pred_count > bucket_capacity) {
			bucket_capacity = pred_count;
			bucket = xrealloc(bucket, bucket_capacity * sizeof(size_t));
		}
		for (size_t c = 0; c < k; ++c)
			bucket_start[c + 1] += bucket_start[c];
		for (size_t i = 0; i < size; ++i)
			for (size_t j = pred_start[splitter[i]]; j < pred_start[splitter[i] + 1]; ++j)
				bucket[bucket_start[pred_class[j]]++] = pred_source[j];
		/* `bucket_start[c]` is now the end of class `c`'s bucket */

		for (size_t c = 0; c < k; ++c) {
			const size_t start = c == 0 ? 0 : bucket_start[c - 1];
			for (size_t i = start; i < bucket_start[c]; ++i)
				mark(&p, bucket[i]);
			split_touched(&p);
		}
	}
	free(bucket);
	free(bucket_start);
	free(splitter);
	free(pred_start);
	free(pred_source);
	free(pred_class);

	/* Build the minimal DFA, numbering blocks by order of first appearance (start states first) */
	const size_t dead_block = p.block[n];
	size_t* index = xmalloc(p.count * sizeof(size_t));
	size_t* representative = xmalloc(p.count * sizeof(size_t));
	for (size_t b = 0; b < p.count; ++b)
		index[b] = SIZE_MAX;
	size_t count = 0;
	for (size_t pass = 0; pass < 2; ++pass) {
		const size_t limit = pass == 0 ? d->start_count : n;
		for (size_t i = 0; i < limit; ++i) {
			const size_t s = pass == 0 ? d->starts[i] : i;
			const size_t b = p.block[s];
			/* Equivalent to the dead state: transitions there become DFA_DEAD. Start states are
			 * kept as states, since scans start from them */
			if ((pass == 1 && b == dead_block) || index[b] != SIZE_MAX)
				continue;
			index[b] = count;
			representative[count++] = s;
		}
	}

	dfa m = { .class_count = k, .state_count = count, .start_count = d->start_count };
	memcpy(m.byte_class, d->byte_class, sizeof(m.byte_class));
	m.states = xmalloc(count * sizeof(*m.states));
	m.transitions = xmalloc(count * k * sizeof(*m.transitions));
	for (size_t i = 0; i < count; ++i) {
		const size_t r = representative[i];
		for (size_t c = 0; c < k; ++c) {
			const size_t target = p.block[TARGET(r, c)];
			m.transitions[i * k + c] = target == dead_block ? DFA_DEAD : index[target];
		}
		if (r == n) {
			m.states[i] = (dfa_state){};
			continue;
		}
		const dfa_state* state = &d->states[r];
		m.states[i] = (dfa_state){
			.accepts = copy_list(state->accepts, state->accept_count),
			.accept_count = state->accept_count,
			.head_ends = copy_list(state->head_ends, state->head_end_count),
			.head_end_count = state->head_end_count,
		};
	}
	m.starts = xmalloc(d->start_count * sizeof(*m.starts));
	for (size_t i = 0; i < d->start_count; ++i)
		m.starts[i] = index[p.block[d->starts[i]]];
#undef TARGET

	m.rule_count = d->rule_count;
	m.rules = xmalloc(d->rule_count * sizeof(*m.rules));
	m.reverse_trailing = xmalloc(d->rule_count * sizeof(*m.reverse_trailing));
	for (size_t i = 0; i < d->rule_count; ++i) {
		m.rules[i] = d->rules[i];
		m.reverse_trailing[i] = d->rules[i].kind == kTrailingVariable
		                          ? dfa_minimize(&d->reverse_trailing[i])
		                          : (dfa){};
	}

	free(index);
	free(representative);
	free(p.elements);
	free(p.location);
	free(p.block);
	free(p.first);
	free(p.end);
	free(p.marked);
	free(p.touched);
	free(p.worklist);
	free(p.in_worklist);
	return m;
}
