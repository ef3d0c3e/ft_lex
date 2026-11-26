#include "internal.h"

DATASTORE_VEC_IMPL(FRAGMENT_TRAIT, fragment_vec)
DATASTORE_VEC_IMPL(START_CONDITION_TRAIT, start_condition_vec)
DATASTORE_VEC_IMPL(RULE_TRAIT, rule_vec)

void
rule_free(rule* r)
{
	free(r->start_conditions);
	regex_free(r->pattern);
	regex_free(r->trailing);
}

rule
rule_clone(const rule* r)
{
	rule clone = *r;
	clone.start_conditions = xmalloc(r->start_condition_count * sizeof(*r->start_conditions));
	if (r->start_condition_count)
		memcpy(clone.start_conditions,
		       r->start_conditions,
		       r->start_condition_count * sizeof(*r->start_conditions));
	clone.pattern = r->pattern ? regex_clone(r->pattern) : NULL;
	clone.trailing = r->trailing ? regex_clone(r->trailing) : NULL;
	return clone;
}

[[nodiscard]]
void*
xmalloc(size_t size)
{
	void* ptr = malloc(size);
	if (!ptr && size != 0) {
		perror("malloc");
		exit(1);
	}
	return ptr;
}

[[nodiscard]]
void*
xrealloc(void* ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr && size != 0) {
		perror("realloc");
		exit(1);
	}
	return ptr;
}

[[nodiscard]]
const void*
ft_memmem(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen)
{
	if (needlelen > haystacklen)
		return NULL;

	const unsigned char* h = haystack;
	for (size_t i = 0; i <= haystacklen - needlelen; ++i) {
		if (!memcmp(h + i, needle, needlelen))
			return h + i;
	}
	return NULL;
}

parse_error
parse_ok(void)
{
	return (parse_error){ kParseOk, 0, 0, NULL, { { NULL, 0 } } };
}

EXPECTED()
advance(parse_state* s, size_t n)
{
	if (s->cursor.pos + n > s->doc.size)
		return EXPECTED_ERR(error_generic("cursor outside of document"));
	for (size_t i = 0; i < n; ++i) {
		s->cursor.column += 1;
		if (s->doc.content[s->cursor.pos] == '\n') {
			s->cursor.column = 0;
			s->cursor.line += 1;
		}
		s->cursor.pos += 1;
	}
	return EXPECTED_OK();
}

int
peek(const parse_state* s, const char* tok)
{
	const size_t len = strlen(tok);
	if (s->cursor.pos + len > s->doc.size)
		return 0;
	if (memcmp(s->doc.content + s->cursor.pos, tok, len))
		return 0;

	return 1;
}

int
accept(parse_state* s, const char* tok)
{
	if (!peek(s, tok))
		return 0;

	[[maybe_unused]] const auto result = advance(s, strlen(tok));
	assert(result.flag == 0);

	return 1;
}

size_t
skip_blanks(parse_state* s)
{
	size_t n = 0;
	while (accept(s, " ") || accept(s, "\t"))
		++n;
	return n;
}

int
accept_line_end(parse_state* s)
{
	skip_blanks(s);
	return s->cursor.pos == s->doc.size || accept(s, "\n");
}

parse_error
unexpected_until_eol(const parse_state* s, const char* reason)
{
	const size_t start = s->cursor.pos;
	const char* eol = memchr(s->doc.content + start, '\n', s->doc.size - start);
	const size_t end = eol ? (size_t)(eol - s->doc.content) : s->doc.size;

	return (parse_error){ kParseUnexpectedToken,
	                      start,
	                      end,
	                      reason,
	                      { .token = { s->doc.content + start, end - start } } };
}

parse_error
unexpected_current(const parse_state* s, const char* reason)
{
	const size_t pos = s->cursor.pos;
	if (pos == s->doc.size)
		return (parse_error){
			kParseUnexpectedToken, pos, pos, reason, { .token = PARSE_TOKEN_LIT("end of file") }
		};
	if (s->doc.content[pos] == '\n')
		return (parse_error){
			kParseUnexpectedToken, pos, pos + 1, reason, { .token = PARSE_TOKEN_LIT("\\n") }
		};
	return (parse_error){
		kParseUnexpectedToken, pos, pos + 1, reason, { .token = { s->doc.content + pos, 1 } }
	};
}
