#include "internal.h"

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

/**
 * @brief Validate the code delimited by @p token
 *
 * In particular, C language code may not contain trigraphs, or have lines that consist only of
 * `%}` or `%%`
 */
[[nodiscard]]
static parse_error
validate_code_fragment(parse_state* s, const fragment* fragment)
{
	const auto loc = &fragment->location;
	assert(loc->start.pos <= loc->end.pos);

	const size_t code_len = loc->end.pos - loc->start.pos;

	/* > The C-language code within "%{" and "%}" delimiter lines shall not contain any lines
	 * consisting only of "%}", or only of "%%". */
	if (fragment->type == kFragmentBlock) {
		/* Check individual lines are not made solely of `%}` or `%%` */
		for (size_t i = 0; i < code_len;) {
			const char* at = memchr(s->doc.content + i + loc->start.pos, '\n', code_len - i);
			assert(at != NULL);

			const auto line_len = (size_t)(at - (s->doc.content + i + loc->start.pos));
			if (line_len != 2) {
				i += line_len + 1;
				continue;
			}
			if (!memcmp(at - 2, "%%", 2))
				return (parse_error){ kParseUnexpectedToken,
				                      loc->start.pos + i,
				                      loc->start.pos + i + 2,
				                      "line cannot consist of only `%%'",
				                      {
					                    .token = PARSE_TOKEN_LIT("%%"),
					                  } };
			if (!memcmp(at - 2, "%}", 2))
				return (parse_error){ kParseUnexpectedToken,
				                      loc->start.pos + i,
				                      loc->start.pos + i + 2,
				                      "line cannot consist of only `%}'",
				                      {
					                    .token = PARSE_TOKEN_LIT("%}"),
					                  } };
			i += line_len + 1;
		}
	}

	if (strcmp(s->language, "c"))
		return parse_ok();

	/* Check that the code contains no trigraphs */
	static const char* trigraphs[] = {
		"??"
		"=",
		"??"
		"/",
		"??"
		"'",
		"??"
		"(",
		"??"
		")",
		"??"
		"!",
		"??"
		"<",
		"??"
		">",
		"??"
		"-",
	};
	for (size_t i = 0; i < sizeof(trigraphs) / sizeof(trigraphs[0]); ++i) {
		const char* at =
		  ft_memmem(s->doc.content + loc->start.pos, code_len, trigraphs[i], strlen(trigraphs[i]));
		if (!at)
			continue;
		const size_t start = (size_t)(at - s->doc.content);
		const size_t end = start + strlen(trigraphs[i]);

		return (parse_error){ kParseUnexpectedToken,
		                      start,
		                      end,
		                      "language code in the input shall not contain C-language trigraphs",
		                      {
			                    .token = { trigraphs[i], strlen(trigraphs[i]) },
			                  } };
	}

	return parse_ok();
}

/**
 * @brief Parse a single code fragment at the cursor
 *
 * The cursor must be at the start of a line beginning with a <blank> (inline fragment) or with
 * `%{` (block fragment). The fragment is pushed to @p out.
 */
[[nodiscard]] static parse_error
parse_fragment(parse_state* s, struct fragment_vec* out)
{
	assert(s->cursor.pos == 0 || s->doc.content[s->cursor.pos - 1] == '\n');

	const auto start = s->cursor;
	/* <blank>: the whole line is code */
	if (is_blank(current(s))) {
		const char* eol = memchr(s->doc.content + s->cursor.pos, '\n', s->doc.size - s->cursor.pos);
		const size_t end = eol ? (size_t)(eol - s->doc.content) : s->doc.size;
		[[maybe_unused]] const auto err = advance(s, end - s->cursor.pos);
		assert(!err.flag);

		const fragment frag = { .location = { start, s->cursor }, .type = kFragmentInline };
		/* Consume the newline */
		(void)accept(s, "\n");
		{
			const auto err = validate_code_fragment(s, &frag);
			if (err.status != kParseOk)
				return err;
		}
		fragment_vec_push(out, frag);
		return parse_ok();
	}

	/* block: %{ ... %} */
	[[maybe_unused]] const int opened = accept(s, "%{");
	assert(opened);
	if (!accept_line_end(s))
		return unexpected_until_eol(s, "only <blank>s may follow `%{'");

	/* Code spans from the line after `%{` up to (excluding) the `%}` line */
	const auto code_start = s->cursor;
	while (s->cursor.column != 0 || !peek(s, "%}")) {
		const auto err = advance(s, 1);
		if (err.flag)
			return (parse_error){ kParseExpectedToken,
			                      start.pos,
			                      s->doc.size,
			                      "to terminate fragment block",
			                      { .token = PARSE_TOKEN_LIT("%}") } };
	}
	const auto code_end = s->cursor;

	[[maybe_unused]] const int closed = accept(s, "%}");
	assert(closed);
	if (!accept_line_end(s))
		return unexpected_until_eol(s, "only <blank>s may follow `%}'");

	const fragment frag = { .location = { code_start, code_end }, .type = kFragmentBlock };
	{
		const auto err = validate_code_fragment(s, &frag);
		if (err.status != kParseOk)
			return err;
	}
	fragment_vec_push(out, frag);
	return parse_ok();
}

/** @brief Advance the cursor while @p predicate holds, return the number of skipped bytes */
static size_t
skip_while(parse_state* s, bool (*predicate)(int))
{
	size_t n = 0;
	while (predicate(current(s))) {
		[[maybe_unused]] const auto err = advance(s, 1);
		assert(!err.flag);
		++n;
	}
	return n;
}

[[nodiscard]] static bool
is_alpha(int c)
{
	return c != -1 && isalpha(c);
}

[[nodiscard]] static bool
is_digit(int c)
{
	return c != -1 && isdigit(c);
}

/** @brief Index of the start condition named `content[start, start + len)`, or `SIZE_MAX` */
[[nodiscard]] static size_t
find_start_condition(const parse_state* s, size_t start, size_t len)
{
	const auto conditions = &s->doc.start_conditions;
	for (size_t i = 0; i < conditions->size; ++i) {
		const char* name = conditions->data[i].name;
		if (strlen(name) == len && !memcmp(name, s->doc.content + start, len))
			return i;
	}
	return SIZE_MAX;
}

/**
 * @brief Parse the names of a start condition declaration (`%s` or `%x`)
 *
 * The cursor must be right after the directive word
 */
[[nodiscard]] static parse_error
parse_start_conditions(parse_state* s, size_t directive_start, bool exclusive)
{
	if (skip_blanks(s) == 0 || !is_name_start(current(s)))
		return (parse_error){ kParseExpectedToken,
		                      directive_start,
		                      s->cursor.pos,
		                      "start condition declarations require at least one name",
		                      { .token = PARSE_TOKEN_LIT("<name>") } };

	do {
		const auto start = s->cursor;
		if (!is_name_start(current(s)))
			return unexpected_current(s, "expected a start condition name");
		skip_while(s, is_identifier_char);
		const size_t len = s->cursor.pos - start.pos;
		if (current(s) != -1 && current(s) != '\n' && !is_blank(current(s)))
			return unexpected_current(s, "invalid character in start condition name");

		if (find_start_condition(s, start.pos, len) != SIZE_MAX)
			return (parse_error){ kParseRedefinition,
			                      start.pos,
			                      s->cursor.pos,
			                      NULL,
			                      { .token = { s->doc.content + start.pos, len } } };

		char* name = xmalloc(len + 1);
		memcpy(name, s->doc.content + start.pos, len);
		name[len] = 0;
		start_condition_vec_push(
		  &s->doc.start_conditions,
		  (start_condition){ .name = name, .exclusive = exclusive, .location = { start, s->cursor } });
		skip_blanks(s);
	} while (current(s) != -1 && current(s) != '\n');

	(void)accept(s, "\n");
	return parse_ok();
}

/**
 * @brief Parse a `%` directive line in the Definitions section (other than `%{` and `%%`)
 *
 * Supported directives:
 *  - `%s name...`, `%S name...`: inclusive start conditions
 *  - `%x name...`, `%X name...`: exclusive start conditions
 *  - `%array`, `%pointer`: type of `yytext`
 *  - Table size declarations (`%p n`, `%n n`, `%a n`, `%e n`, `%k n`, `%o n`), which are accepted
 *    and ignored, as allowed by POSIX.
 */
[[nodiscard]] static parse_error
parse_directive(parse_state* s)
{
	const size_t start = s->cursor.pos;
	[[maybe_unused]] const int percent = accept(s, "%");
	assert(percent);

	/* Directive word */
	skip_while(s, is_alpha);
	const size_t word_len = s->cursor.pos - start;
	const char* word = s->doc.content + start;

	if (word_len == 2 && strchr("sSxX", word[1]))
		return parse_start_conditions(s, start, word[1] == 'x' || word[1] == 'X');

	if ((word_len == 6 && !memcmp(word, "%array", 6)) ||
	    (word_len == 8 && !memcmp(word, "%pointer", 8))) {
		s->doc.yytext = word_len == 6 ? kYytextArray : kYytextPointer;
		if (!accept_line_end(s))
			return unexpected_until_eol(s, "only <blank>s may follow `%array' and `%pointer'");
		return parse_ok();
	}

	if (word_len == 2 && strchr("pnaeko", word[1])) {
		if (skip_blanks(s) == 0 || !is_digit(current(s)))
			return (parse_error){ kParseExpectedToken,
			                      start,
			                      s->cursor.pos,
			                      "table size declaration requires a size",
			                      { .token = PARSE_TOKEN_LIT("<number>") } };
		skip_while(s, is_digit);
		if (!accept_line_end(s))
			return unexpected_until_eol(s, "only <blank>s may follow a table size declaration");
		return parse_ok();
	}

	return (parse_error){ kParseUnexpectedToken,
	                      start,
	                      s->cursor.pos > start + 1 ? s->cursor.pos : start + 1,
	                      "unknown directive",
	                      { .token = { word, word_len > 1 ? word_len : 1 } } };
}

/** @brief Parse a `name regex` definition line */
[[nodiscard]] static parse_error
parse_definition(parse_state* s)
{
	const auto start = s->cursor;
	if (!is_name_start(current(s)))
		return unexpected_current(s,
		                          "expected a definition name, a code fragment, a directive or `%%'");
	while (is_name_char(current(s))) {
		[[maybe_unused]] const auto err = advance(s, 1);
		assert(!err.flag);
	}
	const auto name_end = s->cursor;
	const size_t name_len = name_end.pos - start.pos;

	if (skip_blanks(s) == 0 && current(s) != '\n' && current(s) != -1)
		return unexpected_current(s, "invalid character in definition name");
	if (current(s) == '\n' || current(s) == -1)
		return (parse_error){ kParseExpectedToken,
		                      start.pos,
		                      s->cursor.pos,
		                      "definitions require a regular expression after their name",
		                      { .token = PARSE_TOKEN_LIT("<regex>") } };

	char* name = xmalloc(name_len + 1);
	memcpy(name, s->doc.content + start.pos, name_len);
	name[name_len] = 0;

	if (definition_map_contains(&s->doc.definitions, name)) {
		free(name);
		return (parse_error){ kParseRedefinition,
		                      start.pos,
		                      name_end.pos,
		                      NULL,
		                      { .token = { s->doc.content + start.pos, name_len } } };
	}

	regex_node* regex;
	{
		const auto err = parse_regex(s, &regex);
		if (err.status != kParseOk) {
			free(name);
			return err;
		}
	}

	/* `parse_regex` stopped at a <blank>, newline, end of document, `/` or `$` */
	parse_error err = parse_ok();
	if (current(s) == '/')
		err = unexpected_current(s, "trailing context is only allowed in rules");
	else if (current(s) == '$')
		err = unexpected_current(s, "`$' is only allowed at the end of rules");
	else if (!accept_line_end(s))
		err = unexpected_until_eol(s,
		                           "only <blank>s may follow a definition's regular expression");
	if (err.status != kParseOk) {
		free(name);
		regex_free(regex);
		return err;
	}

	const definition def = { .location = { start, name_end }, .regex = regex };
	/* The map takes ownership of `name` and `def` */
	if (!definition_map_insert(&s->doc.definitions, name, def)) {
		perror("malloc");
		exit(1);
	}
	return parse_ok();
}

/**
 * @brief Parse the Definitions section, up to and including the `%%` line that ends it
 *
 * Each line is either empty, a code fragment, a directive, or a definition.
 */
[[nodiscard]] static parse_error
parse_definitions(parse_state* s)
{
	while (s->cursor.pos != s->doc.size) {
		assert(s->cursor.pos == 0 || s->doc.content[s->cursor.pos - 1] == '\n');

		parse_error err;
		if (accept(s, "\n"))
			continue;
		else if (is_blank(current(s)) || peek(s, "%{"))
			err = parse_fragment(s, &s->doc.fragments);
		else if (accept(s, "%%")) {
			if (!accept_line_end(s))
				return unexpected_until_eol(s, "only <blank>s may follow `%%'");
			return parse_ok();
		} else if (current(s) == '%')
			err = parse_directive(s);
		else
			err = parse_definition(s);

		if (err.status != kParseOk)
			return err;
	}
	return (parse_error){ kParseExpectedToken,
	                      s->doc.size,
	                      s->doc.size,
	                      "to end the definitions section",
	                      { .token = PARSE_TOKEN_LIT("%%") } };
}

/* {{{ Rules */

/** @brief Length of the Rust character literal starting at the cursor's `'`, 0 if it is not one */
[[nodiscard]] static size_t
rust_char_literal_length(const parse_state* s)
{
	/* Escape sequence: up to the closing quote, on the same line */
	if (lookahead(s, 1) == '\\') {
		for (size_t i = 2; i < 12; ++i) {
			const int c = lookahead(s, i);
			if (c == -1 || c == '\n')
				return 0;
			if (c == '\'' && i > 2)
				return i + 1;
		}
		return 0;
	}
	/* A single character, of up to 4 bytes in UTF-8. Otherwise, a lifetime or a label */
	for (size_t i = 2; i <= 5; ++i) {
		const int c = lookahead(s, i);
		if (c == '\'')
			return i + 1;
		if (c == -1 || c < 0x80)
			return 0;
	}
	return 0;
}

/**
 * @brief Length of the Rust raw string literal (`r"..."`, `r#"..."#`, ...) starting at the cursor's
 * `r`, 0 if it is not one or it is not terminated
 */
[[nodiscard]] static size_t
rust_raw_string_length(const parse_state* s)
{
	/* Not the end of an identifier */
	if (s->cursor.pos > 0) {
		const int previous = (unsigned char)s->doc.content[s->cursor.pos - 1];
		if (previous == '_' || isalnum(previous))
			return 0;
	}
	size_t hashes = 0;
	while (lookahead(s, 1 + hashes) == '#')
		++hashes;
	if (lookahead(s, 1 + hashes) != '"')
		return 0;
	for (size_t i = 2 + hashes; s->cursor.pos + i < s->doc.size; ++i) {
		if (lookahead(s, i) != '"')
			continue;
		size_t closing = 0;
		while (closing < hashes && lookahead(s, i + 1 + closing) == '#')
			++closing;
		if (closing == hashes)
			return i + 1 + hashes;
	}
	return 0;
}

/**
 * @brief Skip a code block `{ ... }`, the cursor must be on the opening `{`
 *
 * Braces inside comments, string literals and character literals are ignored. For Rust, block
 * comments nest, strings may span lines, raw strings are recognized, and `'` may start a lifetime
 * or a label instead of a character literal.
 */
[[nodiscard]] static parse_error
skip_code_block(parse_state* s)
{
	const bool rust = !strcmp(s->language, "rust");
	const size_t start = s->cursor.pos;
	size_t depth = 0;
	while (1) {
		const int c = current(s);
		if (c == -1)
			return (parse_error){ kParseExpectedToken,
			                      start,
			                      s->doc.size,
			                      "to terminate action block",
			                      { .token = PARSE_TOKEN_LIT("}") } };

		/* Comments */
		if (peek(s, "/*")) {
			const size_t comment = s->cursor.pos;
			size_t nesting = 1;
			skip(s, 2);
			while (nesting != 0) {
				if (current(s) == -1)
					return (parse_error){ kParseExpectedToken,
					                      comment,
					                      s->doc.size,
					                      "to terminate comment",
					                      { .token = PARSE_TOKEN_LIT("*/") } };
				if (accept(s, "*/"))
					--nesting;
				else if (rust && accept(s, "/*"))
					++nesting;
				else
					skip(s, 1);
			}
			continue;
		}
		if (peek(s, "//")) {
			while (current(s) != -1 && current(s) != '\n')
				skip(s, 1);
			continue;
		}

		if (rust && c == 'r') {
			const size_t raw = rust_raw_string_length(s);
			if (raw != 0) {
				skip(s, raw);
				continue;
			}
		}
		if (rust && c == '\'') {
			const size_t literal = rust_char_literal_length(s);
			skip(s, literal ? literal : 1);
			continue;
		}

		/* String and character literals */
		if (c == '"' || c == '\'') {
			const size_t literal = s->cursor.pos;
			skip(s, 1);
			while (current(s) != c) {
				const int d = current(s);
				if (d == -1 || (d == '\n' && !rust))
					return (parse_error){ kParseExpectedToken,
					                      literal,
					                      s->cursor.pos,
					                      "to terminate literal",
					                      { .token = { c == '"' ? "\"" : "'", 1 } } };
				if (d == '\\' && lookahead(s, 1) != -1)
					skip(s, 1);
				skip(s, 1);
			}
			skip(s, 1);
			continue;
		}

		skip(s, 1);
		if (c == '{')
			++depth;
		else if (c == '}' && --depth == 0)
			return parse_ok();
	}
}

/**
 * @brief Parse a rule's action, the cursor must be on the first non-blank after the pattern
 *
 * The action is either:
 *  - empty (nothing until the end of the line)
 *  - `|`, to use the action of the next rule
 *  - a `{ ... }` block, which may span multiple lines
 *  - a single C statement, up to the end of the line
 */
[[nodiscard]] static parse_error
parse_action(parse_state* s, rule* r)
{
	const auto start = s->cursor;
	const int c = current(s);
	r->action_type = kActionCode;

	if (c == -1 || c == '\n') {
		r->action = (token){ start, start };
		(void)accept(s, "\n");
		return parse_ok();
	}

	if (c == '|') {
		skip(s, 1);
		const auto end = s->cursor;
		if (accept_line_end(s)) {
			r->action_type = kActionFallthrough;
			r->action = (token){ start, end };
			return parse_ok();
		}
		/* Not a lone `|`, handle as code */
		s->cursor = start;
	}

	if (c == '{') {
		const auto err = skip_code_block(s);
		if (err.status != kParseOk)
			return err;
		r->action = (token){ start, s->cursor };
		if (!accept_line_end(s))
			return unexpected_until_eol(s, "only <blank>s may follow an action block");
	} else {
		const char* eol =
		  memchr(s->doc.content + s->cursor.pos, '\n', s->doc.size - s->cursor.pos);
		skip(s, (eol ? (size_t)(eol - s->doc.content) : s->doc.size) - s->cursor.pos);
		r->action = (token){ start, s->cursor };
		(void)accept(s, "\n");
	}

	const fragment code = { .location = r->action, .type = kFragmentInline };
	return validate_code_fragment(s, &code);
}

/** @brief Parse a rule's start condition list `<A,B,...>`, the cursor must be on `<` */
[[nodiscard]] static parse_error
parse_rule_start_conditions(parse_state* s, rule* r)
{
	skip(s, 1);
	do {
		const size_t start = s->cursor.pos;
		if (!is_name_start(current(s)))
			return unexpected_current(s, "expected a start condition name");
		skip_while(s, is_identifier_char);
		const size_t len = s->cursor.pos - start;

		const size_t index = find_start_condition(s, start, len);
		if (index == SIZE_MAX)
			return (parse_error){ kParseUndefined,
			                      start,
			                      s->cursor.pos,
			                      NULL,
			                      { .token = { s->doc.content + start, len } } };

		r->start_conditions = xrealloc(
		  r->start_conditions, (r->start_condition_count + 1) * sizeof(*r->start_conditions));
		r->start_conditions[r->start_condition_count++] = index;
	} while (accept(s, ","));

	if (!accept(s, ">"))
		return unexpected_current(s, "expected `,' or `>' in start condition list");
	return parse_ok();
}

/** @brief Parse a rule: `[<conditions>][^]pattern[/trailing | $] action` */
[[nodiscard]] static parse_error
parse_rule(parse_state* s)
{
	rule r = {};
	const auto start = s->cursor;
	parse_error err = parse_ok();

	if (current(s) == '<') {
		err = parse_rule_start_conditions(s, &r);
		if (err.status != kParseOk)
			goto fail;
	}

	r.bol = accept(s, "^");
	err = parse_regex(s, &r.pattern);
	if (err.status != kParseOk)
		goto fail;

	/* `parse_regex` stopped at a <blank>, newline, end of document, `/` or `$` */
	if (accept(s, "/")) {
		err = parse_regex(s, &r.trailing);
		if (err.status != kParseOk)
			goto fail;
		if (current(s) == '/' || current(s) == '$') {
			err = unexpected_current(s, "a rule can only have one trailing context");
			goto fail;
		}
	} else if (accept(s, "$")) {
		char* newline = xmalloc(1);
		*newline = '\n';
		r.trailing = xmalloc(sizeof(*r.trailing));
		*r.trailing =
		  (regex_node){ .type = kRegexLiteral, .depth = 1, .data.literal = { .str = newline, .len = 1 } };
	}
	r.location = (token){ start, s->cursor };

	assert(current(s) == -1 || current(s) == '\n' || is_blank(current(s)));
	skip_blanks(s);
	err = parse_action(s, &r);
	if (err.status != kParseOk)
		goto fail;

	rule_vec_push(&s->doc.rules, r);
	return parse_ok();

fail:
	rule_free(&r);
	return err;
}

/** @brief Fail if the last rule uses the `|` action, as there is no next rule */
[[nodiscard]] static parse_error
check_last_action(const parse_state* s)
{
	const auto rules = &s->doc.rules;
	if (rules->size == 0 || rules->data[rules->size - 1].action_type != kActionFallthrough)
		return parse_ok();

	const auto action = &rules->data[rules->size - 1].action;
	return (parse_error){ kParseUnexpectedToken,
	                      action->start.pos,
	                      action->end.pos,
	                      "the last rule cannot use the `|' action",
	                      { .token = PARSE_TOKEN_LIT("|") } };
}

/** @brief Parse the User Subroutines section: everything up to the end of document */
[[nodiscard]] static parse_error
parse_user_code(parse_state* s)
{
	const auto start = s->cursor;
	skip(s, s->doc.size - s->cursor.pos);
	s->doc.user_code = (token){ start, s->cursor };

	const fragment code = { .location = s->doc.user_code, .type = kFragmentInline };
	return validate_code_fragment(s, &code);
}

/**
 * @brief Parse the Rules section and the User Subroutines section, if any
 *
 * Code fragments (lines beginning with a <blank>, or `%{ ... %}` blocks) are only accepted before
 * the first rule.
 */
[[nodiscard]] static parse_error
parse_rules(parse_state* s)
{
	while (s->cursor.pos != s->doc.size) {
		assert(s->cursor.pos == 0 || s->doc.content[s->cursor.pos - 1] == '\n');

		/* Empty lines, or lines made of <blank>s */
		const auto line_start = s->cursor;
		if (accept_line_end(s))
			continue;
		s->cursor = line_start;

		parse_error err;
		if (accept(s, "%%")) {
			if (!accept_line_end(s))
				return unexpected_until_eol(s, "only <blank>s may follow `%%'");
			err = check_last_action(s);
			if (err.status != kParseOk)
				return err;
			return parse_user_code(s);
		} else if (is_blank(current(s)) || peek(s, "%{")) {
			if (s->doc.rules.size != 0)
				return unexpected_until_eol(
				  s, "code fragments are only allowed before the first rule");
			err = parse_fragment(s, &s->doc.rule_fragments);
		} else
			err = parse_rule(s);

		if (err.status != kParseOk)
			return err;
	}
	return check_last_action(s);
}

/* }}} */

void
document_destroy(document* doc)
{
	fragment_vec_free(&doc->fragments);
	definition_map_destroy(&doc->definitions);
	start_condition_vec_free(&doc->start_conditions);
	fragment_vec_free(&doc->rule_fragments);
	rule_vec_free(&doc->rules);
	for (size_t i = 0; i < doc->source_count; ++i)
		free(doc->sources[i].name);
	free(doc->sources);
	free(doc->content);
	*doc = (document){};
}

const source*
document_locate(const document* doc, size_t pos, size_t global_line, size_t* line)
{
	assert(doc->source_count != 0);
	/* Last file starting at or before `pos` */
	size_t low = 0;
	size_t high = doc->source_count;
	while (high - low > 1) {
		const size_t mid = low + (high - low) / 2;
		if (doc->sources[mid].offset <= pos)
			low = mid;
		else
			high = mid;
	}
	const source* src = &doc->sources[low];
	*line = global_line - src->line + 1;
	return src;
}

/** @brief Append the content of @p fd to @p doc's content */
[[nodiscard]] static EXPECTED()
read_fd(document* doc, int fd, size_t* capacity)
{
	while (1) {
		if (doc->size == *capacity) {
			*capacity = *capacity ? *capacity * 2 : 4096;
			doc->content = xrealloc(doc->content, *capacity);
		}
		const ssize_t n = read(fd, doc->content + doc->size, *capacity - doc->size);
		if (n == -1)
			return EXPECTED_ERR(error_syscall("failed to read input", "read"));
		if (n == 0)
			return EXPECTED_OK();
		doc->size += (size_t)n;
	}
}

/** @brief Read and concatenate the input files into @p doc */
[[nodiscard]] static EXPECTED()
read_sources(document* doc, const char* const* files, size_t file_count)
{
	static const char* const standard_input[] = { "-" };
	if (file_count == 0) {
		files = standard_input;
		file_count = 1;
	}

	doc->sources = xmalloc(file_count * sizeof(*doc->sources));
	size_t capacity = 0;
	size_t line = 1;
	for (size_t i = 0; i < file_count; ++i) {
		/* Count the lines of the previous file */
		const size_t previous = i ? doc->sources[i - 1].offset : 0;
		for (size_t p = previous; p < doc->size; ++p)
			line += doc->content[p] == '\n';

		const size_t len = strlen(files[i]);
		char* name = xmalloc(len + 1);
		memcpy(name, files[i], len + 1);
		doc->sources[i] = (source){ .name = name, .offset = doc->size, .line = line };
		doc->source_count = i + 1;

		const bool is_stdin = !strcmp(files[i], "-");
		const int fd = is_stdin ? STDIN_FILENO : open(files[i], O_RDONLY);
		if (fd == -1) {
			/* `error` only holds a static message */
			static char message[PATH_MAX + 64];
			snprintf(message, sizeof(message), "failed to open `%s'", files[i]);
			return EXPECTED_ERR(error_syscall(message, "open"));
		}
		const auto result = read_fd(doc, fd, &capacity);
		if (!is_stdin)
			close(fd);
		if (result.flag)
			return result;
	}

	/* Keep the content NUL-terminated, without counting it in the size */
	doc->content = xrealloc(doc->content, doc->size + 1);
	doc->content[doc->size] = 0;
	return EXPECTED_OK();
}

EXPECTED(document)
parse(struct format_output* output, const char* const* files, size_t file_count, const char* language)
{
	parse_state s = {};
	{
		const auto result = read_sources(&s.doc, files, file_count);
		if (result.flag) {
			document_destroy(&s.doc);
			return EXPECTED_ERR(document, result.error);
		}
	}
	s.language = language;
	s.cursor.line = 1;

	char* initial = xmalloc(sizeof("INITIAL"));
	memcpy(initial, "INITIAL", sizeof("INITIAL"));
	start_condition_vec_push(&s.doc.start_conditions,
	                         (start_condition){ .name = initial, .exclusive = false });

	auto err = parse_definitions(&s);
	if (err.status == kParseOk)
		err = parse_rules(&s);
	if (err.status != kParseOk) {
		parser_display_error(output, &err, &s.doc);
		document_destroy(&s.doc);
		return EXPECTED_ERR(document, error_generic("failed to parse document"));
	}
	return EXPECTED_OK(document, s.doc);
}
