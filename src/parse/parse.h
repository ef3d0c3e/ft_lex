#ifndef PARSE_H
#define PARSE_H

#include "cash.h"
#include "../vector.h"
#undef DATASTORE_VEC_SETTINGS_DEFAULT
#define DATASTORE_VEC_SETTINGS_DEFAULT(X)                                                        \
	X(NEW, {                                                                                     \
		ptr = malloc(size);                                                                      \
		if (!ptr) {                                                                              \
			perror("malloc");                                                                    \
			exit(1);                                                                             \
		};                                                                                       \
	})                                                                                           \
	X(REALLOC, {                                                                                 \
		ptr = realloc(ptr, size);                                                                \
		if (!ptr) {                                                                              \
			perror("realloc");                                                                   \
			exit(1);                                                                             \
		};                                                                                       \
	})                                                                                           \
	X(FREE, { free(ptr); })                                                                      \
	X(GROW, { new_capacity = capacity != 0 ? (capacity * 2) : 1; })

#include "../error.h"

/**
 * @defgroup Parser Parser
 * @{
 */

typedef struct
{
	/** @brief Absolute byte position */
	size_t pos;
	/** @brief Line number */
	size_t line;
	/** @brief Column byte position */
	size_t column;
} cursor;

typedef struct
{
	cursor start;
	cursor end;
} token;

typedef enum
{
	/** @brief A fragment delimited by `%{` and `%}` */
	kFragmentBlock,
	/** @brief An inline fragmet started by a <blank> */
	kFragmentInline,
} fragment_type;

/**
 * @brief Represent C program fragment
 *
 * Any lines in **Definitions** that begins with a `<blank>` or within `%{` and `}%` delimits a
 * fragment
 */
typedef struct
{
	token location;
	fragment_type type;
} fragment;

#define FRAGMENT_TRAIT(X)                                                                        \
	X(TYPE, fragment)                                                                            \
	X(FREE, {})                                                                                  \
	X(CLONE, { *new = *val; })
DATASTORE_VEC(fragment, fragment_vec)

/**
 * @defgroup ParseRegex Regex Parser
 * @ingroup Parser
 * @{
 */

typedef enum
{
	/** @brief Match a literal byte string */
	kRegexLiteral,
	/** @brief Match a single byte from a set */
	kRegexSet,
	/** @brief Match a sequence of expressions */
	kRegexConcat,
	/** @brief Match one of several expressions */
	kRegexAlternation,
	/** @brief Match an expression repeated */
	kRegexRepeat,
} regex_node_type;

/** @brief Value of @ref regex_node::repeat::max for unbounded repetitions (`*`, `+`, `{n,}`) */
#define REGEX_REPEAT_UNBOUNDED SIZE_MAX

/**
 * @brief Maximum finite repetition count in an interval (`{m,n}`)
 *
 * Intervals are expanded into copies of the repeated expression when building automata
 */
#define REGEX_REPEAT_LIMIT 1000

/**
 * @brief Set of bytes, one bit per byte value
 *
 * Bracket expressions, character classes (`[:alpha:]`, ...) and `.` are all expanded into a set
 * at parse time. Negation (`[^...]`) is applied at parse time too.
 */
typedef struct
{
	uint64_t bits[4];
} regex_set;

static inline void
regex_set_add(regex_set* set, unsigned char c)
{
	set->bits[c / 64] |= (uint64_t)1 << (c % 64);
}

/** @brief Add the inclusive range `[first, last]` to @p set */
static inline void
regex_set_add_range(regex_set* set, unsigned char first, unsigned char last)
{
	for (unsigned c = first; c <= last; ++c)
		regex_set_add(set, (unsigned char)c);
}

static inline void
regex_set_negate(regex_set* set)
{
	for (size_t i = 0; i < 4; ++i)
		set->bits[i] = ~set->bits[i];
}

[[nodiscard]] static inline bool
regex_set_contains(const regex_set* set, unsigned char c)
{
	return (set->bits[c / 64] >> (c % 64)) & 1;
}

typedef struct regex_node
{
	regex_node_type type;

	union
	{
		/**
		 * @brief Data for @ref kRegexLiteral
		 *
		 * May contain NUL bytes (e.g. from `\0`), hence the explicit length
		 */
		struct
		{
			char* str;
			size_t len;
		} literal;

		/** @brief Data for @ref kRegexSet */
		regex_set set;

		/** @brief Data for @ref kRegexConcat and @ref kRegexAlternation */
		struct
		{
			struct regex_node** exprs;
			size_t len;
		} list;

		/** @brief Data for @ref kRegexRepeat */
		struct
		{
			struct regex_node* expr;
			size_t min;
			/** @brief Maximum repetitions, @ref REGEX_REPEAT_UNBOUNDED for no limit */
			size_t max;
		} repeat;
	} data;
} regex_node;

void
regex_free(regex_node* regex);
[[nodiscard]] regex_node*
regex_clone(const regex_node* regex);

/** @} */

typedef struct
{
	token location;
	regex_node* regex;
} definition;

static inline void
definition_free(definition def)
{
	regex_free(def.regex);
}

[[nodiscard]] static inline definition
definition_clone(const definition def)
{
	return (definition){ def.location, regex_clone(def.regex) };
}

#define definition_map_key_type char*
#define definition_map_value_type definition
#define definition_map_hash cash_hash_str
#define definition_map_eq(a, b) (strcmp((a), (b)) == 0)
#define definition_map_key_copy strdup
#define definition_map_key_free free
#define definition_map_value_copy definition_clone
#define definition_map_value_free definition_free
CASH_HM(definition_map, definition_map);

/**
 * @brief Start condition declared with `%s` (inclusive) or `%x` (exclusive)
 *
 * The start condition at index @ref START_CONDITION_INITIAL is the implicit `INITIAL` condition
 */
typedef struct
{
	/** @brief Name, a C identifier */
	char* name;
	/** @brief Exclusive (`%x`) conditions only activate rules that explicitly list them */
	bool exclusive;
	/** @brief Location of the name in its declaration, zeroed for `INITIAL` */
	token location;
} start_condition;

/** @brief Index of the implicit `INITIAL` start condition */
#define START_CONDITION_INITIAL 0

#define START_CONDITION_TRAIT(X)                                                                 \
	X(TYPE, start_condition)                                                                     \
	X(FREE, { free(val->name); })                                                                \
	X(CLONE, {                                                                                   \
		*new = *val;                                                                             \
		new->name = strdup(val->name);                                                           \
	})
DATASTORE_VEC(start_condition, start_condition_vec)

typedef enum
{
	/** @brief A C code action, possibly empty */
	kActionCode,
	/** @brief The `|` action: use the action of the next rule */
	kActionFallthrough,
} action_type;

/**
 * @brief A lex rule: `[<conditions>][^]pattern[/trailing | $] action`
 *
 * `^`, `$` and trailing context only have meaning at the top level of a rule, so they are stored
 * here instead of in the regex tree.
 */
typedef struct
{
	/** @brief Location of the rule's pattern, including start conditions and anchors */
	token location;

	/**
	 * @brief Indices into @ref document::start_conditions
	 *
	 * When empty, the rule is active in all inclusive start conditions (including `INITIAL`)
	 */
	size_t* start_conditions;
	size_t start_condition_count;

	/** @brief Rule only matches at the beginning of a line (leading `^`) */
	bool bol;
	/** @brief Matched pattern */
	regex_node* pattern;
	/** @brief Trailing context (`r/s` or `r$`, where `$` is `/\n`), NULL if none */
	regex_node* trailing;

	action_type action_type;
	/**
	 * @brief Action code, including the braces for `{ ... }` blocks
	 *
	 * Empty when the rule has no action. For @ref kActionFallthrough, location of the `|`
	 */
	token action;
} rule;

void
rule_free(rule* r);
[[nodiscard]] rule
rule_clone(const rule* r);

#define RULE_TRAIT(X)                                                                            \
	X(TYPE, rule)                                                                                \
	X(FREE, { rule_free(val); })                                                                 \
	X(CLONE, { *new = rule_clone(val); })
DATASTORE_VEC(rule, rule_vec)

/** @brief Type of `yytext`, selected with `%array` or `%pointer` */
typedef enum
{
	kYytextPointer,
	kYytextArray,
} yytext_type;

/** @brief An input file, part of a document's content */
typedef struct
{
	/** @brief File name, `-` for the standard input */
	char* name;
	/** @brief Position of the file in the document content */
	size_t offset;
	/** @brief Line of the document content (1-based) where the file starts */
	size_t line;
} source;

/**
 * @brief A parsed lex document
 *
 * The document is the concatenation of its input files. Tokens refer to positions in
 * @ref content, which the document owns. Release it with @ref document_destroy.
 */
typedef struct
{
	/** @brief Document content, owned by the document */
	char* content;
	/** @brief Content size */
	size_t size;
	/** @brief Input files, in order */
	source* sources;
	size_t source_count;

	/* Definitions section */

	/** @brief Code fragments of the Definitions section, copied outside of `yylex` */
	struct fragment_vec fragments;
	/** @brief ERE Definitions */
	struct definition_map definitions;
	/** @brief Start conditions, @ref START_CONDITION_INITIAL first */
	struct start_condition_vec start_conditions;
	/** @brief Type of `yytext` */
	yytext_type yytext;

	/* Rules section */

	/** @brief Code fragments before the first rule, copied at the start of `yylex` */
	struct fragment_vec rule_fragments;
	/** @brief Rules, in order of appearance */
	struct rule_vec rules;

	/* User subroutines section */

	/** @brief Code after the second `%%`, empty when absent */
	token user_code;
} document;

/** @brief Release all resources held by @p doc, including its content */
void
document_destroy(document* doc);

/**
 * @brief Find the input file containing @p pos
 *
 * @param doc Document
 * @param pos Position in the document content
 * @param global_line Line of @p pos in the document content (1-based)
 * @param line Set to the line of @p pos in its file (1-based)
 * @return The input file
 */
[[nodiscard]] const source*
document_locate(const document* doc, size_t pos, size_t global_line, size_t* line);

/**
 * @brief Parse a lex document
 *
 * Parse errors are displayed to @p output
 *
 * @param output Output to display parse errors to
 * @param files Input files, concatenated in order. `-` is the standard input
 * @param file_count Number of input files, the standard input is used when `0`
 * @return The parsed document, to release with @ref document_destroy
 */
EXPECTED(document)
parse(struct format_output* output, const char* const* files, size_t file_count);

typedef struct
{
	/** @brief Parsed language */
	const char* language;

	/** @brief Current cursor */
	cursor cursor;

	/** @brief Parsed document */
	document doc;
} parse_state;

/**
 * @defgroup ParserError Parser error
 * @ingroup Parser
 * @{
 */
typedef enum
{
	kParseOk = 0,
	kParseGeneric,
	kParseExpectedToken,
	kParseUnexpectedToken,
	/** @brief A definition or start condition is defined twice. The token is the name */
	kParseRedefinition,
	/** @brief Reference to an undefined definition or start condition. The token is the name */
	kParseUndefined,
} parse_status;

typedef struct
{
	parse_status status;

	/**
	 * @brief Error start position in bytes
	 *
	 * The @ref token will be reconstructed when printing the error
	 */
	size_t start;
	/** @brief Error end position in bytes (exclusive, may equal the document size) */
	size_t end;
	/** @brief Additional reason, can be NULL for none */
	const char* reason;

	/** @brief Additional data for error display */
	union
	{
		/**
		 * @brief Token to display
		 *
		 * Valid for:
		 * @ref kParseExpectedToken
		 * @ref kParseUnexpectedToken
		 * @ref kParseRedefinition
		 * @ref kParseUndefined
		 */
		struct
		{
			/** @brief Token text, not necessarily NUL-terminated */
			const char* str;
			/** @brief Token length in bytes */
			size_t len;
		} token;
	} args;
} parse_error;

/** @brief Build a @ref parse_error token from a string literal */
#define PARSE_TOKEN_LIT(lit) { .str = (lit), .len = sizeof(lit) - 1 }

/**
 * @brief Display a parser error to the specified output
 *
 * @param out Output to write to
 * @param err Error to display
 * @param doc Document the error refers to
 */
void
parser_display_error(struct format_output* out, const parse_error* err, const document* doc);

/** @} */

/** @} */

[[nodiscard]]
void*
xmalloc(size_t size);
[[nodiscard]]
void*
xrealloc(void* ptr, size_t size);
[[nodiscard]]
const void*
ft_memmem(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen);

#endif // PARSE_H
