#ifndef PARSE_INTERNAL_H
#define PARSE_INTERNAL_H

/**
 * @file
 * @brief Cursor primitives and sub-parsers shared between the parser's translation units
 */

#include "parse.h"

#include <ctype.h>

/**
 * @defgroup ParserInternal Parser internals
 * @ingroup Parser
 * @{
 */

[[nodiscard]] parse_error
parse_ok(void);

/** @brief Advance the cursor by @p n bytes, fails if that would go past the end of document */
[[nodiscard]] EXPECTED()
advance(parse_state* s, size_t n);

/** @brief Whether the document continues with @p tok at the cursor */
[[nodiscard]] int
peek(const parse_state* s, const char* tok);

/** @brief Consume @p tok if the document continues with it */
[[nodiscard]] int
accept(parse_state* s, const char* tok);

/** @brief Advance the cursor by @p n bytes, which must be within the document */
static inline void
skip(parse_state* s, size_t n)
{
	[[maybe_unused]] const auto err = advance(s, n);
	assert(!err.flag);
}

/** @brief Byte at the cursor, or `-1` at end of document */
[[nodiscard]] static inline int
current(const parse_state* s)
{
	return s->cursor.pos < s->doc.size ? (unsigned char)s->doc.content[s->cursor.pos] : -1;
}

/** @brief Byte @p offset bytes after the cursor, or `-1` past the end of document */
[[nodiscard]] static inline int
lookahead(const parse_state* s, size_t offset)
{
	return s->cursor.pos + offset < s->doc.size ? (unsigned char)s->doc.content[s->cursor.pos + offset]
	                                        : -1;
}

[[nodiscard]] static inline bool
is_blank(int c)
{
	return c == ' ' || c == '\t';
}

/** @brief Whether @p c may start a definition name */
[[nodiscard]] static inline bool
is_name_start(int c)
{
	return c != -1 && (isalpha(c) || c == '_');
}

/** @brief Whether @p c may continue a definition name (`-` is accepted, as flex does) */
[[nodiscard]] static inline bool
is_name_char(int c)
{
	return c != -1 && (isalnum(c) || c == '_' || c == '-');
}

/** @brief Whether @p c may continue a C identifier (used for start condition names) */
[[nodiscard]] static inline bool
is_identifier_char(int c)
{
	return c != -1 && (isalnum(c) || c == '_');
}

/** @brief Skip <blank>s, return the number of skipped bytes */
size_t
skip_blanks(parse_state* s);

/**
 * @brief Accepts blanks, then the newline ending the line (or end of document)
 *
 * @return 1 on success, 0 if a non-blank was found. In that case the cursor is left on it.
 */
[[nodiscard]] int
accept_line_end(parse_state* s);

/** @brief Build an unexpected token error spanning from the cursor to the end of the line */
[[nodiscard]] parse_error
unexpected_until_eol(const parse_state* s, const char* reason);

/**
 * @brief Build an unexpected token error for the byte at the cursor
 *
 * Newlines and end of document are displayed as `\n` and `end of file`
 */
[[nodiscard]] parse_error
unexpected_current(const parse_state* s, const char* reason);

/**
 * @brief Parse a lex extended regular expression at the cursor
 *
 * References to definitions (`{name}`) are resolved using the definitions parsed so far.
 *
 * Parsing stops, without consuming it, at the first unquoted:
 *  - <blank>, newline, or end of document
 *  - `/` outside of a group (trailing context)
 *  - `$` ending the expression (i.e. followed by one of the above)
 *
 * The caller is responsible for handling what follows. A leading `^` is not accepted: rules
 * should consume it before calling this function.
 *
 * @param s Parser state
 * @param out Parsed expression on success, untouched on error
 */
[[nodiscard]] parse_error
parse_regex(parse_state* s, regex_node** out);

/** @} */

#endif // PARSE_INTERNAL_H
