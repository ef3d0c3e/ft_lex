#include "automaton.h"

/** @brief Part of an NFA with a single entry and a single exit state */
typedef struct
{
	size_t start;
	/** @brief Exit state, it has no outgoing transitions */
	size_t end;
} nfa_fragment;

[[nodiscard]] static size_t
new_state(nfa* n)
{
	if (n->state_count == n->state_capacity) {
		n->state_capacity = n->state_capacity ? n->state_capacity * 2 : 64;
		n->states = xrealloc(n->states, n->state_capacity * sizeof(*n->states));
	}
	n->states[n->state_count] = (nfa_state){ .accept = NFA_NONE, .head_end = NFA_NONE };
	return n->state_count++;
}

static void
add_edge(nfa* n, size_t from, nfa_edge edge)
{
	nfa_state* state = &n->states[from];
	if (state->edge_count == state->edge_capacity) {
		state->edge_capacity = state->edge_capacity ? state->edge_capacity * 2 : 2;
		state->edges = xrealloc(state->edges, state->edge_capacity * sizeof(*state->edges));
	}
	state->edges[state->edge_count++] = edge;
}

static void
add_epsilon(nfa* n, size_t from, size_t to)
{
	add_edge(n, from, (nfa_edge){ .epsilon = true, .target = to });
}

static void
add_set(nfa* n, size_t from, const regex_set* set, size_t to)
{
	add_edge(n, from, (nfa_edge){ .epsilon = false, .set = *set, .target = to });
}

[[nodiscard]] static nfa_fragment
build(nfa* n, const regex_node* node, bool reverse);

/** @brief Build `expr{min,max}` by copying @p expr's fragment */
[[nodiscard]] static nfa_fragment
build_repeat(nfa* n, const regex_node* expr, size_t min, size_t max, bool reverse)
{
	const size_t start = new_state(n);
	size_t end = start;

	/* Mandatory copies */
	for (size_t i = 0; i < min; ++i) {
		const auto f = build(n, expr, reverse);
		add_epsilon(n, end, f.start);
		end = f.end;
	}

	if (max == REGEX_REPEAT_UNBOUNDED) {
		/* Kleene star */
		const auto f = build(n, expr, reverse);
		const size_t exit = new_state(n);
		add_epsilon(n, end, f.start);
		add_epsilon(n, end, exit);
		add_epsilon(n, f.end, f.start);
		add_epsilon(n, f.end, exit);
		return (nfa_fragment){ start, exit };
	}

	/* Optional copies */
	for (size_t i = min; i < max; ++i) {
		const auto f = build(n, expr, reverse);
		const size_t exit = new_state(n);
		add_epsilon(n, end, f.start);
		add_epsilon(n, end, exit);
		add_epsilon(n, f.end, exit);
		end = exit;
	}
	return (nfa_fragment){ start, end };
}

/**
 * @brief Build the fragment for @p node
 *
 * @param reverse Build a fragment matching the reverse of the strings matched by @p node
 */
[[nodiscard]] static nfa_fragment
build(nfa* n, const regex_node* node, bool reverse)
{
	switch (node->type) {
		case kRegexLiteral: {
			const size_t start = new_state(n);
			size_t end = start;
			const size_t len = node->data.literal.len;
			for (size_t i = 0; i < len; ++i) {
				regex_set set = {};
				regex_set_add(&set, (unsigned char)node->data.literal.str[reverse ? len - 1 - i : i]);
				const size_t next = new_state(n);
				add_set(n, end, &set, next);
				end = next;
			}
			return (nfa_fragment){ start, end };
		}
		case kRegexSet: {
			const size_t start = new_state(n);
			const size_t end = new_state(n);
			add_set(n, start, &node->data.set, end);
			return (nfa_fragment){ start, end };
		}
		case kRegexConcat: {
			const size_t len = node->data.list.len;
			auto result = build(n, node->data.list.exprs[reverse ? len - 1 : 0], reverse);
			for (size_t i = 1; i < len; ++i) {
				const auto f = build(n, node->data.list.exprs[reverse ? len - 1 - i : i], reverse);
				add_epsilon(n, result.end, f.start);
				result.end = f.end;
			}
			return result;
		}
		case kRegexAlternation: {
			const size_t start = new_state(n);
			const size_t end = new_state(n);
			for (size_t i = 0; i < node->data.list.len; ++i) {
				const auto f = build(n, node->data.list.exprs[i], reverse);
				add_epsilon(n, start, f.start);
				add_epsilon(n, f.end, end);
			}
			return (nfa_fragment){ start, end };
		}
		case kRegexRepeat:
			return build_repeat(
			  n, node->data.repeat.expr, node->data.repeat.min, node->data.repeat.max, reverse);
	}
	assert(0);
	return (nfa_fragment){};
}

/**
 * @brief Compute the length of the strings matched by @p node, if they all have the same length
 *
 * @return Whether the length is fixed
 */
[[nodiscard]] static bool
fixed_length(const regex_node* node, size_t* out)
{
	switch (node->type) {
		case kRegexLiteral:
			*out = node->data.literal.len;
			return true;
		case kRegexSet:
			*out = 1;
			return true;
		case kRegexConcat: {
			size_t total = 0;
			for (size_t i = 0; i < node->data.list.len; ++i) {
				size_t len;
				if (!fixed_length(node->data.list.exprs[i], &len) || len > SIZE_MAX - total)
					return false;
				total += len;
			}
			*out = total;
			return true;
		}
		case kRegexAlternation: {
			size_t first;
			if (!fixed_length(node->data.list.exprs[0], &first))
				return false;
			for (size_t i = 1; i < node->data.list.len; ++i) {
				size_t len;
				if (!fixed_length(node->data.list.exprs[i], &len) || len != first)
					return false;
			}
			*out = first;
			return true;
		}
		case kRegexRepeat: {
			size_t len;
			const size_t count = node->data.repeat.min;
			if (count != node->data.repeat.max || !fixed_length(node->data.repeat.expr, &len))
				return false;
			if (len != 0 && count > SIZE_MAX / len)
				return false;
			*out = count * len;
			return true;
		}
	}
	assert(0);
	return false;
}

/** @brief Whether rule @p r is active in start condition @p condition */
[[nodiscard]] static bool
rule_active(const document* doc, const rule* r, size_t condition)
{
	if (r->start_condition_count == 0)
		return !doc->start_conditions.data[condition].exclusive;
	for (size_t i = 0; i < r->start_condition_count; ++i)
		if (r->start_conditions[i] == condition)
			return true;
	return false;
}

/**
 * @brief Build an NFA with a single start state for @p node, accepting with rule `0`
 *
 * @param reverse Match the reverse of the strings matched by @p node
 */
[[nodiscard]] static nfa
build_single(const regex_node* node, bool reverse)
{
	nfa n = {};
	n.start_count = 1;
	n.starts = xmalloc(sizeof(*n.starts));
	n.starts[0] = new_state(&n);
	const auto f = build(&n, node, reverse);
	add_epsilon(&n, n.starts[0], f.start);
	n.states[f.end].accept = 0;
	return n;
}

nfa
nfa_build(const document* doc)
{
	nfa n = {};

	const size_t conditions = doc->start_conditions.size;
	n.start_count = conditions * 2;
	n.starts = xmalloc(n.start_count * sizeof(*n.starts));
	for (size_t i = 0; i < n.start_count; ++i)
		n.starts[i] = new_state(&n);

	n.rule_count = doc->rules.size;
	n.rules = xmalloc(n.rule_count * sizeof(*n.rules));
	n.reverse_trailing = xmalloc(n.rule_count * sizeof(*n.reverse_trailing));
	for (size_t i = 0; i < n.rule_count; ++i)
		n.reverse_trailing[i] = (nfa){};
	for (size_t i = 0; i < doc->rules.size; ++i) {
		const rule* r = &doc->rules.data[i];

		const auto head = build(&n, r->pattern, false);
		size_t end = head.end;

		n.rules[i] = (rule_trailing){ .kind = kTrailingNone };
		if (r->trailing) {
			size_t len;
			if (fixed_length(r->trailing, &len))
				n.rules[i] = (rule_trailing){ .kind = kTrailingFixedTail, .length = len };
			else if (fixed_length(r->pattern, &len))
				n.rules[i] = (rule_trailing){ .kind = kTrailingFixedHead, .length = len };
			else {
				n.rules[i] = (rule_trailing){ .kind = kTrailingVariable };
				n.states[head.end].head_end = i;
				n.reverse_trailing[i] = build_single(r->trailing, true);
			}

			const auto tail = build(&n, r->trailing, false);
			add_epsilon(&n, head.end, tail.start);
			end = tail.end;
		}
		n.states[end].accept = i;

		for (size_t c = 0; c < conditions; ++c) {
			if (!rule_active(doc, r, c))
				continue;
			add_epsilon(&n, n.starts[automaton_start_index(c, true)], head.start);
			if (!r->bol)
				add_epsilon(&n, n.starts[automaton_start_index(c, false)], head.start);
		}
	}
	return n;
}

void
nfa_destroy(nfa* n)
{
	for (size_t i = 0; i < n->state_count; ++i)
		free(n->states[i].edges);
	free(n->states);
	free(n->starts);
	for (size_t i = 0; i < n->rule_count; ++i)
		nfa_destroy(&n->reverse_trailing[i]);
	free(n->reverse_trailing);
	free(n->rules);
	*n = (nfa){};
}
