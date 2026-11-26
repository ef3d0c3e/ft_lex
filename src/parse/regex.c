#include "internal.h"

/**
 * @file
 * @brief Lex extended regular expression parser
 *
 * Grammar, from lowest to highest precedence (POSIX lex):
 * @code
 * alternation := concat ('|' concat)*
 * concat      := repeat+
 * repeat      := atom ('*' | '+' | '?' | '{' m '}' | '{' m ',' '}' | '{' m ',' n '}')*
 * atom        := '(' alternation ')' | '[' bracket ']' | '"' quoted '"' | '{' name '}'
 *              | '.' | escape | byte
 * @endcode
 */

void
regex_free(regex_node* regex)
{
	if (!regex)
		return;
	switch (regex->type) {
		case kRegexLiteral:
			free(regex->data.literal.str);
			break;
		case kRegexSet:
			break;
		case kRegexConcat:
		case kRegexAlternation:
			for (size_t i = 0; i < regex->data.list.len; ++i)
				regex_free(regex->data.list.exprs[i]);
			free(regex->data.list.exprs);
			break;
		case kRegexRepeat:
			regex_free(regex->data.repeat.expr);
			break;
	}
	free(regex);
}

regex_node*
regex_clone(const regex_node* regex)
{
	assert(regex != NULL);
	regex_node* result = xmalloc(sizeof(*result));

	*result = *regex;
	switch (regex->type) {
		case kRegexLiteral: {
			const size_t len = regex->data.literal.len;
			result->data.literal.str = xmalloc(len);
			if (len != 0)
				memcpy(result->data.literal.str, regex->data.literal.str, len);
			break;
		}
		case kRegexSet:
			break;
		case kRegexConcat:
		case kRegexAlternation: {
			const size_t len = regex->data.list.len;
			result->data.list.exprs = xmalloc(sizeof(regex_node*) * len);
			for (size_t i = 0; i < len; ++i)
				result->data.list.exprs[i] = regex_clone(regex->data.list.exprs[i]);
			break;
		}
		case kRegexRepeat:
			result->data.repeat.expr = regex_clone(regex->data.repeat.expr);
			break;
	}
	return result;
}

/** @brief Build an expected token error spanning from @p start to the cursor */
[[nodiscard]]
static parse_error
expected(const parse_state* s, size_t start, const char* token, const char* reason)
{
	return (parse_error){
		kParseExpectedToken, start, s->cursor.pos, reason, { .token = { token, strlen(token) } }
	};
}

/** @brief Build an unexpected token error for the text between @p start and the cursor */
[[nodiscard]]
static parse_error
unexpected_span(const parse_state* s, size_t start, const char* reason)
{
	return (parse_error){ kParseUnexpectedToken,
	                      start,
	                      s->cursor.pos,
	                      reason,
	                      { .token = { s->doc.content + start, s->cursor.pos - start } } };
}

/* {{{ Nodes */

[[nodiscard]]
static regex_node*
new_node(regex_node node)
{
	regex_node* ptr = xmalloc(sizeof(*ptr));
	*ptr = node;
	ptr->depth = regex_node_depth(ptr);
	return ptr;
}

/** @brief Error for a regex deeper than @ref REGEX_DEPTH_LIMIT, spanning from @p start */
[[nodiscard]]
static parse_error
too_deep(const parse_state* s, size_t start)
{
	return (parse_error){ kParseGeneric,
	                      start,
	                      s->cursor.pos,
	                      "regular expression is nested too deeply (limit is 1000 levels)",
	                      {} };
}

[[nodiscard]]
static regex_node*
new_byte(unsigned char c)
{
	char* str = xmalloc(1);
	str[0] = (char)c;
	return new_node((regex_node){ .type = kRegexLiteral, .data.literal = { str, 1 } });
}

typedef struct
{
	regex_node** data;
	size_t len;
	size_t capacity;
} node_list;

static void
list_push(node_list* list, regex_node* node)
{
	if (list->len == list->capacity) {
		list->capacity = list->capacity ? list->capacity * 2 : 4;
		list->data = xrealloc(list->data, list->capacity * sizeof(*list->data));
	}
	list->data[list->len++] = node;
}

static void
list_free(node_list* list)
{
	for (size_t i = 0; i < list->len; ++i)
		regex_free(list->data[i]);
	free(list->data);
}

/** @brief Turn a non-empty list into a node of type @p type, or its only element */
[[nodiscard]]
static regex_node*
list_into(node_list* list, regex_node_type type)
{
	assert(list->len != 0);
	if (list->len == 1) {
		regex_node* node = list->data[0];
		free(list->data);
		return node;
	}
	return new_node((regex_node){ .type = type, .data.list = { list->data, list->len } });
}

/** @brief Push @p node to a concatenation, merging adjacent literals */
static void
concat_push(node_list* list, regex_node* node)
{
	regex_node* last = list->len ? list->data[list->len - 1] : NULL;
	if (!last || last->type != kRegexLiteral || node->type != kRegexLiteral) {
		list_push(list, node);
		return;
	}

	const size_t len = last->data.literal.len + node->data.literal.len;
	last->data.literal.str = xrealloc(last->data.literal.str, len ? len : 1);
	if (node->data.literal.len)
		memcpy(last->data.literal.str + last->data.literal.len,
		       node->data.literal.str,
		       node->data.literal.len);
	last->data.literal.len = len;
	regex_free(node);
}

/* }}} */

/* {{{ Terminals */

/** @brief Whether the cursor is on an unquoted byte that ends the expression */
[[nodiscard]]
static bool
at_end(const parse_state* s)
{
	const int c = current(s);
	return c == -1 || c == '\n' || is_blank(c);
}

/** @brief Whether the cursor is on a `$` that ends the expression */
[[nodiscard]]
static bool
at_dollar_end(const parse_state* s)
{
	const int next = lookahead(s, 1);
	return current(s) == '$' && (next == -1 || next == '\n' || is_blank(next));
}

[[nodiscard]]
static unsigned
digit_value(int c)
{
	return isdigit(c) ? (unsigned)(c - '0') : (unsigned)(tolower(c) - 'a' + 10);
}

/**
 * @brief Parse an escape sequence, the cursor must be on the `\`
 *
 * Supports `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v`, octal (`\1` to `\377`), hexadecimal
 * (`\x1` to `\xff`), and `\c` for any other `c`.
 */
[[nodiscard]]
static parse_error
parse_escape(parse_state* s, unsigned char* out)
{
	const size_t start = s->cursor.pos;
	skip(s, 1);

	const int c = current(s);
	if (c == -1 || c == '\n')
		return unexpected_current(s, "expected a character after `\\'");
	skip(s, 1);

	static const char simple[] = "a\ab\bf\fn\nr\rt\tv\v";
	const char* found = c != 0 ? strchr(simple, c) : NULL;
	if (found && (found - simple) % 2 == 0) {
		*out = (unsigned char)found[1];
		return parse_ok();
	}

	unsigned base = 0;
	size_t max_digits = 0;
	if (c >= '0' && c <= '7') {
		base = 8;
		max_digits = 2; /* the first digit is already consumed */
	} else if (c == 'x') {
		base = 16;
		max_digits = SIZE_MAX;
		if (!isxdigit(current(s)))
			return expected(s, start, "<hexadecimal digit>", "in escape sequence");
	} else {
		*out = (unsigned char)c;
		return parse_ok();
	}

	unsigned value = base == 8 ? digit_value(c) : 0;
	for (size_t n = 0; n < max_digits; ++n) {
		const int d = current(s);
		if (base == 8 ? !(d >= '0' && d <= '7') : !isxdigit(d))
			break;
		if (value <= 255)
			value = value * base + digit_value(d);
		skip(s, 1);
	}
	if (value > 255)
		return unexpected_span(s, start, "escape sequence is out of range");
	*out = (unsigned char)value;
	return parse_ok();
}

/** @brief Parse a quoted string `"..."`, escape sequences are recognized */
[[nodiscard]]
static parse_error
parse_quoted(parse_state* s, regex_node** out)
{
	const size_t start = s->cursor.pos;
	skip(s, 1);

	char* buf = NULL;
	size_t len = 0;
	size_t capacity = 0;
	while (current(s) != '"') {
		const int c = current(s);
		if (c == -1 || c == '\n') {
			free(buf);
			return expected(s, start, "\"", "to terminate quoted string");
		}

		unsigned char byte = (unsigned char)c;
		if (c == '\\') {
			const auto err = parse_escape(s, &byte);
			if (err.status != kParseOk) {
				free(buf);
				return err;
			}
		} else
			skip(s, 1);

		if (len == capacity) {
			capacity = capacity ? capacity * 2 : 16;
			buf = xrealloc(buf, capacity);
		}
		buf[len++] = (char)byte;
	}
	skip(s, 1);

	*out = new_node((regex_node){ .type = kRegexLiteral, .data.literal = { buf, len } });
	return parse_ok();
}

/* }}} */

/* {{{ Bracket expressions */

static const struct
{
	const char* name;
	int (*predicate)(int);
} char_classes[] = {
	{ "alnum", isalnum }, { "alpha", isalpha }, { "blank", isblank }, { "cntrl", iscntrl },
	{ "digit", isdigit }, { "graph", isgraph }, { "lower", islower }, { "print", isprint },
	{ "punct", ispunct }, { "space", isspace }, { "upper", isupper }, { "xdigit", isxdigit },
};

/** @brief Parse a character class `[:name:]` into @p set, the cursor must be on `[:` */
[[nodiscard]]
static parse_error
parse_char_class(parse_state* s, regex_set* set)
{
	const size_t start = s->cursor.pos;
	skip(s, 2);

	const size_t name_start = s->cursor.pos;
	while (islower(current(s)))
		skip(s, 1);
	const char* name = s->doc.content + name_start;
	const size_t name_len = s->cursor.pos - name_start;

	if (!accept(s, ":]"))
		return expected(s, start, ":]", "to terminate character class");

	for (size_t i = 0; i < sizeof(char_classes) / sizeof(char_classes[0]); ++i) {
		if (strlen(char_classes[i].name) != name_len ||
		    memcmp(char_classes[i].name, name, name_len))
			continue;
		for (int c = 0; c <= 255; ++c)
			if (char_classes[i].predicate(c))
				regex_set_add(set, (unsigned char)c);
		return parse_ok();
	}
	return unexpected_span(s, start, "unknown character class");
}

/**
 * @brief Parse a single character of a bracket expression
 *
 * This is either a collating symbol `[.c.]`, an equivalence class `[=c=]` (both limited to a
 * single character), an escape sequence, or a literal byte.
 */
[[nodiscard]]
static parse_error
parse_bracket_char(parse_state* s, unsigned char* out)
{
	const size_t start = s->cursor.pos;
	const bool collating = peek(s, "[.");
	if (collating || peek(s, "[=")) {
		skip(s, 2);
		const int c = current(s);
		if (c == -1 || c == '\n')
			return unexpected_current(s, "expected a character");
		skip(s, 1);
		if (!accept(s, collating ? ".]" : "=]"))
			return expected(s,
			                start,
			                collating ? ".]" : "=]",
			                "only single-character collating elements are supported");
		*out = (unsigned char)c;
		return parse_ok();
	}
	if (current(s) == '\\')
		return parse_escape(s, out);

	*out = (unsigned char)current(s);
	skip(s, 1);
	return parse_ok();
}

/** @brief Parse a bracket expression `[...]`, negation is applied to the resulting set */
[[nodiscard]]
static parse_error
parse_bracket(parse_state* s, regex_node** out)
{
	const size_t start = s->cursor.pos;
	skip(s, 1);

	regex_set set = {};
	const bool negate = accept(s, "^");
	/* `]` is literal when it comes first */
	for (bool first = true;; first = false) {
		const int c = current(s);
		if (c == -1 || c == '\n')
			return expected(s, start, "]", "to terminate bracket expression");
		if (c == ']' && !first) {
			skip(s, 1);
			break;
		}

		if (peek(s, "[:")) {
			const auto err = parse_char_class(s, &set);
			if (err.status != kParseOk)
				return err;
			continue;
		}

		const size_t element_start = s->cursor.pos;
		unsigned char low;
		{
			const auto err = parse_bracket_char(s, &low);
			if (err.status != kParseOk)
				return err;
		}

		/* `-` is literal when last */
		const int next = lookahead(s, 1);
		if (current(s) != '-' || next == ']' || next == -1 || next == '\n') {
			regex_set_add(&set, low);
			continue;
		}
		skip(s, 1);
		if (peek(s, "[:"))
			return unexpected_current(s, "a character class cannot end a range");

		unsigned char high;
		{
			const auto err = parse_bracket_char(s, &high);
			if (err.status != kParseOk)
				return err;
		}
		if (high < low)
			return unexpected_span(s, element_start, "range end is lower than its start");
		regex_set_add_range(&set, low, high);
	}

	if (negate)
		regex_set_negate(&set);
	*out = new_node((regex_node){ .type = kRegexSet, .data.set = set });
	return parse_ok();
}

/* }}} */

/* {{{ Expressions */

[[nodiscard]]
static parse_error
parse_alternation(parse_state* s, size_t depth, regex_node** out);

/** @brief Parse a definition reference `{name}` into a copy of the definition's expression */
[[nodiscard]]
static parse_error
parse_reference(parse_state* s, regex_node** out)
{
	skip(s, 1);

	const size_t name_start = s->cursor.pos;
	if (!is_name_start(current(s)))
		return unexpected_current(s, "expected a definition name or a repetition count after `{'");
	while (is_name_char(current(s)))
		skip(s, 1);
	const size_t name_end = s->cursor.pos;
	const size_t name_len = name_end - name_start;

	if (current(s) != '}')
		return unexpected_current(s, "expected `}' to terminate definition reference");
	skip(s, 1);

	char* name = xmalloc(name_len + 1);
	memcpy(name, s->doc.content + name_start, name_len);
	name[name_len] = 0;
	const definition* def = definition_map_get(&s->doc.definitions, name);
	free(name);

	if (!def)
		return (parse_error){ kParseUndefined,
		                      name_start,
		                      name_end,
		                      NULL,
		                      { .token = { s->doc.content + name_start, name_len } } };
	*out = regex_clone(def->regex);
	return parse_ok();
}

/** @brief Parse a decimal repetition count */
[[nodiscard]]
static parse_error
parse_count(parse_state* s, size_t* out)
{
	const size_t start = s->cursor.pos;
	size_t value = 0;
	bool overflow = false;
	while (isdigit(current(s))) {
		const size_t d = digit_value(current(s));
		/* Keep SIZE_MAX free for @ref REGEX_REPEAT_UNBOUNDED */
		if (value > (SIZE_MAX - 1 - d) / 10)
			overflow = true;
		else
			value = value * 10 + d;
		skip(s, 1);
	}
	if (overflow)
		return unexpected_span(s, start, "repetition count is too large");
	*out = value;
	return parse_ok();
}

/** @brief Parse a repetition interval `{m}`, `{m,}` or `{m,n}`, the cursor must be on `{` */
[[nodiscard]]
static parse_error
parse_interval(parse_state* s, size_t* min, size_t* max)
{
	const size_t start = s->cursor.pos;
	skip(s, 1);

	{
		const auto err = parse_count(s, min);
		if (err.status != kParseOk)
			return err;
	}
	if (!accept(s, ","))
		*max = *min;
	else if (!isdigit(current(s)))
		*max = REGEX_REPEAT_UNBOUNDED;
	else {
		const auto err = parse_count(s, max);
		if (err.status != kParseOk)
			return err;
	}

	if (!accept(s, "}"))
		return expected(s, start, "}", "to terminate repetition interval");
	if (*max < *min)
		return unexpected_span(s, start, "repetition maximum is lower than its minimum");
	if (*min > REGEX_REPEAT_LIMIT || (*max != REGEX_REPEAT_UNBOUNDED && *max > REGEX_REPEAT_LIMIT))
		return unexpected_span(s, start, "repetition count is too large (limit is 1000)");
	return parse_ok();
}

[[nodiscard]]
static parse_error
parse_atom(parse_state* s, size_t depth, regex_node** out)
{
	const int c = current(s);
	switch (c) {
		case '(': {
			const size_t start = s->cursor.pos;
			/* Each group is parsed recursively */
			if (depth + 1 > REGEX_DEPTH_LIMIT)
				return unexpected_current(s, "groups are nested too deeply (limit is 1000 levels)");
			skip(s, 1);
			regex_node* inner;
			const auto err = parse_alternation(s, depth + 1, &inner);
			if (err.status != kParseOk)
				return err;
			if (!accept(s, ")")) {
				regex_free(inner);
				return expected(s, start, ")", "to close group");
			}
			*out = inner;
			return parse_ok();
		}
		case '[':
			return parse_bracket(s, out);
		case '"':
			return parse_quoted(s, out);
		case '.': {
			regex_set set = {};
			regex_set_add(&set, '\n');
			regex_set_negate(&set);
			skip(s, 1);
			*out = new_node((regex_node){ .type = kRegexSet, .data.set = set });
			return parse_ok();
		}
		case '\\': {
			unsigned char byte;
			const auto err = parse_escape(s, &byte);
			if (err.status != kParseOk)
				return err;
			*out = new_byte(byte);
			return parse_ok();
		}
		case '{':
			if (isdigit(lookahead(s, 1)))
				return unexpected_current(s, "nothing to repeat");
			return parse_reference(s, out);
		case '*':
		case '+':
		case '?':
			return unexpected_current(s, "nothing to repeat");
		case '^':
			return unexpected_current(
			  s, "`^' is only special at the start of a rule, use `\\^' to match it literally");
		case '$':
			return unexpected_current(
			  s, "`$' is only special at the end of a rule, use `\\$' to match it literally");
		case '/':
			assert(depth != 0);
			return unexpected_current(s, "trailing context cannot appear inside a group");
		default:
			assert(!at_end(s) && c != '|' && c != ')');
			skip(s, 1);
			*out = new_byte((unsigned char)c);
			return parse_ok();
	}
}

[[nodiscard]]
static parse_error
parse_repeat(parse_state* s, size_t depth, regex_node** out)
{
	regex_node* node;
	{
		const auto err = parse_atom(s, depth, &node);
		if (err.status != kParseOk)
			return err;
	}

	while (1) {
		const size_t operator_start = s->cursor.pos;
		size_t min, max;
		if (accept(s, "*")) {
			min = 0;
			max = REGEX_REPEAT_UNBOUNDED;
		} else if (accept(s, "+")) {
			min = 1;
			max = REGEX_REPEAT_UNBOUNDED;
		} else if (accept(s, "?")) {
			min = 0;
			max = 1;
		} else if (current(s) == '{' && isdigit(lookahead(s, 1))) {
			const auto err = parse_interval(s, &min, &max);
			if (err.status != kParseOk) {
				regex_free(node);
				return err;
			}
		} else
			break;

		node = new_node(
		  (regex_node){ .type = kRegexRepeat, .data.repeat = { node, min, max } });
		/* e.g. `a*****...` */
		if (node->depth > REGEX_DEPTH_LIMIT) {
			regex_free(node);
			return too_deep(s, operator_start);
		}
	}
	*out = node;
	return parse_ok();
}

/** @brief Whether the cursor is on something that ends a concatenation */
[[nodiscard]]
static bool
at_concat_end(const parse_state* s, size_t depth)
{
	const int c = current(s);
	if (at_end(s) || c == '|' || c == ')')
		return true;
	return depth == 0 && (c == '/' || at_dollar_end(s));
}

[[nodiscard]]
static parse_error
parse_concat(parse_state* s, size_t depth, regex_node** out)
{
	const size_t start = s->cursor.pos;
	node_list items = {};
	while (!at_concat_end(s, depth)) {
		regex_node* node;
		const auto err = parse_repeat(s, depth, &node);
		if (err.status != kParseOk) {
			list_free(&items);
			return err;
		}
		concat_push(&items, node);
	}

	if (items.len == 0) {
		if (current(s) == '|' || current(s) == ')')
			return unexpected_current(s, "expected a regular expression before it");
		return (parse_error){ kParseExpectedToken,
		                      s->cursor.pos,
		                      s->cursor.pos,
		                      NULL,
		                      { .token = PARSE_TOKEN_LIT("<regex>") } };
	}
	*out = list_into(&items, kRegexConcat);
	if ((*out)->depth > REGEX_DEPTH_LIMIT) {
		regex_free(*out);
		return too_deep(s, start);
	}
	return parse_ok();
}

[[nodiscard]]
static parse_error
parse_alternation(parse_state* s, size_t depth, regex_node** out)
{
	const size_t start = s->cursor.pos;
	node_list alternatives = {};
	do {
		regex_node* node;
		const auto err = parse_concat(s, depth, &node);
		if (err.status != kParseOk) {
			list_free(&alternatives);
			return err;
		}
		list_push(&alternatives, node);
	} while (accept(s, "|"));

	*out = list_into(&alternatives, kRegexAlternation);
	if ((*out)->depth > REGEX_DEPTH_LIMIT) {
		regex_free(*out);
		return too_deep(s, start);
	}
	return parse_ok();
}

/* }}} */

parse_error
parse_regex(parse_state* s, regex_node** out)
{
	regex_node* regex;
	{
		const auto err = parse_alternation(s, 0, &regex);
		if (err.status != kParseOk)
			return err;
	}
	if (current(s) == ')') {
		regex_free(regex);
		return unexpected_current(s, "unmatched `)'");
	}
	*out = regex;
	return parse_ok();
}
