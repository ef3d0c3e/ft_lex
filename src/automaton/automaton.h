#ifndef AUTOMATON_H
#define AUTOMATON_H

#include "../parse/parse.h"

/**
 * @defgroup Automaton Automaton
 * @brief Rules compiled into finite automata
 *
 * All rules of a @ref document are compiled into a single automaton. Matching needs the
 * following external context, which selects the start state (see @ref automaton_start_index):
 *  - the active start condition
 *  - whether the input is at the beginning of a line, which enables `^` rules
 *
 * Automata work on bytes, independently of the locale.
 * @{
 */

/** @brief Index of the start state for start condition @p condition */
[[nodiscard]] static inline size_t
automaton_start_index(size_t condition, bool bol)
{
	return condition * 2 + (bol ? 1 : 0);
}

typedef enum
{
	/** @brief The rule has no trailing context */
	kTrailingNone,
	/** @brief The trailing context has a fixed length: `yyleng = match length - length` */
	kTrailingFixedTail,
	/** @brief The pattern has a fixed length: `yyleng = length` */
	kTrailingFixedHead,
	/**
	 * @brief Both pattern and trailing context have a variable length
	 *
	 * For a match of length `L`, `yyleng` is the largest `p <= L` such that:
	 *  - the forward automaton's state at position `p` ends the rule's pattern
	 *    (@ref dfa_state::head_ends contains the rule)
	 *  - the trailing context matches `input[p, L)`: running the rule's reverse trailing context
	 *    automaton (@ref dfa::reverse_trailing) backwards from `input[L - 1]` down to
	 *    `input[p]` ends in an accepting state.
	 *
	 * Such a `p` always exists for a match of the rule.
	 */
	kTrailingVariable,
} trailing_kind;

/** @brief How to compute `yyleng` for a rule's match */
typedef struct
{
	trailing_kind kind;
	/** @brief Length for @ref kTrailingFixedTail and @ref kTrailingFixedHead */
	size_t length;
} rule_trailing;

/**
 * @defgroup NFA NFA
 * @ingroup Automaton
 * @{
 */

/** @brief Value for "no rule" in @ref nfa_state */
#define NFA_NONE SIZE_MAX

typedef struct
{
	/** @brief Epsilon transition, @ref set is unused */
	bool epsilon;
	/** @brief Bytes accepted by this transition */
	regex_set set;
	/** @brief Target state */
	size_t target;
} nfa_edge;

typedef struct
{
	nfa_edge* edges;
	size_t edge_count;
	size_t edge_capacity;
	/** @brief Rule accepted in this state, @ref NFA_NONE if none */
	size_t accept;
	/**
	 * @brief Rule whose pattern ends in this state, @ref NFA_NONE if none
	 *
	 * Only set for rules with @ref kTrailingVariable trailing context
	 */
	size_t head_end;
} nfa_state;

/** @brief Thompson NFA */
typedef struct nfa
{
	nfa_state* states;
	size_t state_count;
	size_t state_capacity;

	/** @brief Start states, indexed with @ref automaton_start_index */
	size_t* starts;
	size_t start_count;

	/** @brief Trailing context handling, for each rule */
	rule_trailing* rules;
	size_t rule_count;

	/**
	 * @brief For each rule with @ref kTrailingVariable trailing context, an NFA matching its
	 * trailing context reversed. Other entries are empty.
	 *
	 * These have a single start state and accept with rule `0`
	 */
	struct nfa* reverse_trailing;
} nfa;

/** @brief Build the NFA for all rules of @p doc */
[[nodiscard]] nfa
nfa_build(const document* doc);

void
nfa_destroy(nfa* n);

/** @} */

/**
 * @defgroup DFA DFA
 * @ingroup Automaton
 * @{
 */

/** @brief Transition to the dead state: no rule can match anymore */
#define DFA_DEAD SIZE_MAX

typedef struct
{
	/**
	 * @brief Rules accepted in this state, in ascending order
	 *
	 * The first rule has priority; the others are needed to implement `REJECT`
	 */
	size_t* accepts;
	size_t accept_count;
	/** @brief Rules with @ref kTrailingVariable trailing context whose pattern ends here */
	size_t* head_ends;
	size_t head_end_count;
} dfa_state;

/**
 * @brief Deterministic automaton
 *
 * Bytes are grouped into equivalence classes (bytes that have the same transitions everywhere),
 * to keep the transition table small.
 */
typedef struct dfa
{
	/** @brief Equivalence class of each byte */
	size_t byte_class[256];
	size_t class_count;

	dfa_state* states;
	size_t state_count;
	/**
	 * @brief Transition table, `transitions[state * class_count + class]`
	 *
	 * @ref DFA_DEAD when there is no transition
	 */
	size_t* transitions;

	/** @brief Start states, indexed with @ref automaton_start_index */
	size_t* starts;
	size_t start_count;

	/** @brief Trailing context handling, for each rule */
	rule_trailing* rules;
	size_t rule_count;

	/**
	 * @brief For each rule with @ref kTrailingVariable trailing context, a DFA matching its
	 * trailing context reversed. Other entries are empty.
	 *
	 * These have a single start state, and accept (with rule `0`) when the bytes read so far,
	 * reversed, match the trailing context.
	 */
	struct dfa* reverse_trailing;
} dfa;

/** @brief Build a DFA from @p n using the subset construction */
[[nodiscard]] dfa
dfa_build(const nfa* n);

void
dfa_destroy(dfa* d);

/** @brief Transition from @p state on byte @p c */
[[nodiscard]] static inline size_t
dfa_next(const dfa* d, size_t state, unsigned char c)
{
	return d->transitions[state * d->class_count + d->byte_class[c]];
}

/** @} */

/** @} */

#endif // AUTOMATON_H
