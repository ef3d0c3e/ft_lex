#include "codegen.h"
#include "skeleton.h"

#include <limits.h>
#include <stdarg.h>

/**
 * @file
 * @brief Scanner generation
 *
 * Layout of the generated scanner:
 *  1. @ref skeleton_prologue, `YY_ARRAY` and the `yytext` declaration
 *  2. @ref skeleton_declarations, start condition macros
 *  3. Definitions section code fragments
 *  4. Automaton tables
 *  5. @ref skeleton_runtime, ending with the declarations of `yylex`'s variables
 *  6. Rules section code fragments
 *  7. @ref skeleton_scan, then a `case` for each rule's action, then @ref skeleton_epilogue
 *  8. User subroutines
 *
 * User code is surrounded by `#line` directives, so compiler diagnostics refer to the lex input.
 */

typedef struct
{
	FILE* out;
	/** @brief Name of the output, for `#line` directives */
	const char* out_name;
	const document* doc;
	/** @brief Current output line (1-based) */
	size_t line;
} emitter;

static void
emit_bytes(emitter* e, const char* bytes, size_t len)
{
	fwrite(bytes, 1, len, e->out);
	for (size_t i = 0; i < len; ++i)
		e->line += bytes[i] == '\n';
}

static void
emit(emitter* e, const char* str)
{
	emit_bytes(e, str, strlen(str));
}

static void __attribute__((__format__(__printf__, 2, 3)))
emitf(emitter* e, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char small[256];
	const int len = vsnprintf(small, sizeof(small), fmt, args);
	va_end(args);
	assert(len >= 0);

	if ((size_t)len < sizeof(small)) {
		emit_bytes(e, small, (size_t)len);
		return;
	}
	char* buf = xmalloc((size_t)len + 1);
	va_start(args, fmt);
	vsnprintf(buf, (size_t)len + 1, fmt, args);
	va_end(args);
	emit_bytes(e, buf, (size_t)len);
	free(buf);
}

static void
emit_lines(emitter* e, const char* const* lines)
{
	for (size_t i = 0; lines[i]; ++i)
		emit(e, lines[i]);
}

/** @brief Emit `#line line "name"` */
static void
emit_line_directive(emitter* e, size_t line, const char* name)
{
	emitf(e, "#line %zu \"", line);
	for (const char* c = name; *c; ++c) {
		if (*c == '"' || *c == '\\')
			emit(e, "\\");
		if (*c == '\n')
			emit(e, "\\n");
		else
			emit_bytes(e, c, 1);
	}
	emit(e, "\"\n");
}

/** @brief Emit user code from the input, with `#line` directives around it */
static void
emit_code(emitter* e, token location)
{
	const size_t len = location.end.pos - location.start.pos;
	if (len == 0)
		return;

	size_t line;
	const source* src =
	  document_locate(e->doc, location.start.pos, location.start.line, &line);
	emit_line_directive(e, line, strcmp(src->name, "-") ? src->name : "<stdin>");

	const char* code = e->doc->content + location.start.pos;
	emit_bytes(e, code, len);
	if (code[len - 1] != '\n')
		emit(e, "\n");

	/* The directive refers to the line after it */
	emit_line_directive(e, e->line + 1, e->out_name);
}

/** @brief Emit `decl = { values... };`. Empty arrays get a single `0` element */
static void
emit_array(emitter* e, const char* decl, const long* values, size_t count)
{
	emitf(e, "%s = {", decl);
	if (count == 0)
		emit(e, "0");
	for (size_t i = 0; i < count; ++i) {
		if (i % 16 == 0)
			emit(e, "\n\t");
		emitf(e, "%ld,", values[i]);
		if (i % 16 != 15 && i + 1 != count)
			emit(e, " ");
	}
	emit(e, "\n};\n");
}

[[nodiscard]] static long
state_value(size_t state)
{
	if (state == DFA_DEAD)
		return -1;
	assert(state <= INT_MAX);
	return (long)state;
}

/** @brief Emit a byte class table and a transition table */
static void
emit_transitions(emitter* e, const dfa* d, const char* class_decl, const char* next_decl)
{
	long classes[256];
	for (size_t c = 0; c < 256; ++c)
		classes[c] = (long)d->byte_class[c];
	emit_array(e, class_decl, classes, 256);

	const size_t count = d->state_count * d->class_count;
	long* next = xmalloc(count * sizeof(*next));
	for (size_t i = 0; i < count; ++i)
		next[i] = state_value(d->transitions[i]);
	emit_array(e, next_decl, next, count);
	free(next);
}

/**
 * @brief Emit the rule lists of each state as `<name>_base` (offsets, one per state plus one)
 * and `<name>_list`
 */
static void
emit_rule_lists(emitter* e, const dfa* d, bool accepts, const char* name)
{
	long* base = xmalloc((d->state_count + 1) * sizeof(*base));
	size_t total = 0;
	for (size_t i = 0; i < d->state_count; ++i) {
		base[i] = (long)total;
		total += accepts ? d->states[i].accept_count : d->states[i].head_end_count;
	}
	base[d->state_count] = (long)total;

	long* list = xmalloc(total * sizeof(*list));
	size_t n = 0;
	for (size_t i = 0; i < d->state_count; ++i) {
		const dfa_state* state = &d->states[i];
		const size_t count = accepts ? state->accept_count : state->head_end_count;
		for (size_t j = 0; j < count; ++j)
			list[n++] = (long)(accepts ? state->accepts[j] : state->head_ends[j]);
	}

	char decl[64];
	snprintf(decl, sizeof(decl), "static const int %s_base[]", name);
	emit_array(e, decl, base, d->state_count + 1);
	snprintf(decl, sizeof(decl), "static const int %s_list[]", name);
	emit_array(e, decl, list, total);
	free(base);
	free(list);
}

static void
emit_tables(emitter* e, const dfa* d)
{
	emit(e, "\n/* Automaton tables */\n");
	emitf(e, "#define YY_CLASSES %zu\n", d->class_count);
	emit_transitions(
	  e, d, "static const unsigned char yy_byte_class[256]", "static const int yy_next[]");

	long* starts = xmalloc(d->start_count * sizeof(*starts));
	for (size_t i = 0; i < d->start_count; ++i)
		starts[i] = state_value(d->starts[i]);
	emit_array(e, "static const int yy_starts[]", starts, d->start_count);
	free(starts);

	emit_rule_lists(e, d, true, "yy_accept");
	emit_rule_lists(e, d, false, "yy_head");

	/* Trailing context */
	long* kinds = xmalloc(d->rule_count * sizeof(*kinds));
	long* lengths = xmalloc(d->rule_count * sizeof(*lengths));
	for (size_t i = 0; i < d->rule_count; ++i) {
		switch (d->rules[i].kind) {
			case kTrailingNone:
				kinds[i] = 0;
				break;
			case kTrailingFixedTail:
				kinds[i] = 1;
				break;
			case kTrailingFixedHead:
				kinds[i] = 2;
				break;
			case kTrailingVariable:
				kinds[i] = 3;
				break;
		}
		assert(d->rules[i].length <= LONG_MAX);
		lengths[i] = (long)d->rules[i].length;
	}
	emit(e, "/* 0: none, 1: fixed length trailing context, 2: fixed length pattern, 3: variable */\n");
	emit_array(e, "static const int yy_trailing_kind[]", kinds, d->rule_count);
	emit_array(e, "static const size_t yy_trailing_length[]", lengths, d->rule_count);
	free(kinds);
	free(lengths);

	emit(e,
	     "struct yy_reverse\n{\n"
	     "\tconst unsigned char *byte_class;\n"
	     "\tconst int *next;\n"
	     "\tconst unsigned char *accept;\n"
	     "\tint classes;\n"
	     "\tint start;\n"
	     "};\n");
	for (size_t i = 0; i < d->rule_count; ++i) {
		if (d->rules[i].kind != kTrailingVariable)
			continue;
		const dfa* reverse = &d->reverse_trailing[i];
		assert(reverse->start_count == 1);

		char class_decl[96];
		char next_decl[96];
		snprintf(class_decl, sizeof(class_decl), "static const unsigned char yy_reverse_class_%zu[256]", i);
		snprintf(next_decl, sizeof(next_decl), "static const int yy_reverse_next_%zu[]", i);
		emitf(e, "/* Reversed trailing context of rule %zu */\n", i);
		emit_transitions(e, reverse, class_decl, next_decl);

		long* accept = xmalloc(reverse->state_count * sizeof(*accept));
		for (size_t s = 0; s < reverse->state_count; ++s)
			accept[s] = reverse->states[s].accept_count != 0;
		char accept_decl[96];
		snprintf(accept_decl, sizeof(accept_decl), "static const unsigned char yy_reverse_accept_%zu[]", i);
		emit_array(e, accept_decl, accept, reverse->state_count);
		free(accept);
	}
	emit(e, "static const struct yy_reverse yy_reverse_dfa[] = {\n");
	if (d->rule_count == 0)
		emit(e, "\t{ 0, 0, 0, 0, 0 },\n");
	for (size_t i = 0; i < d->rule_count; ++i) {
		if (d->rules[i].kind != kTrailingVariable) {
			emit(e, "\t{ 0, 0, 0, 0, 0 },\n");
			continue;
		}
		const dfa* reverse = &d->reverse_trailing[i];
		emitf(e,
		      "\t{ yy_reverse_class_%zu, yy_reverse_next_%zu, yy_reverse_accept_%zu, %zu, %ld },\n",
		      i,
		      i,
		      i,
		      reverse->class_count,
		      state_value(reverse->starts[0]));
	}
	emit(e, "};\n");
}

EXPECTED()
codegen(const document* doc, const dfa* d, FILE* out, const char* out_name)
{
	emitter e = { .out = out, .out_name = out_name, .doc = doc, .line = 1 };
	const bool array = doc->yytext == kYytextArray;

	emit_lines(&e, skeleton_prologue);
	emitf(&e, "#define YY_ARRAY %d\n", array ? 1 : 0);
	emit(&e, array ? "char yytext[YYLMAX];\n" : "char *yytext;\n");
	emit_lines(&e, skeleton_declarations);

	emit(&e, "/* Start conditions */\n");
	for (size_t i = 0; i < doc->start_conditions.size; ++i)
		emitf(&e, "#define %s %zu\n", doc->start_conditions.data[i].name, i);
	emit(&e, "\n");

	for (size_t i = 0; i < doc->fragments.size; ++i)
		emit_code(&e, doc->fragments.data[i].location);

	emit_tables(&e, d);
	emit_lines(&e, skeleton_runtime);

	for (size_t i = 0; i < doc->rule_fragments.size; ++i)
		emit_code(&e, doc->rule_fragments.data[i].location);

	emit_lines(&e, skeleton_scan);
	for (size_t i = 0; i < doc->rules.size; ++i) {
		const rule* r = &doc->rules.data[i];
		emitf(&e, "\t\tcase %zu:\n", i);
		/* `|`: fall through to the next rule's action */
		if (r->action_type == kActionFallthrough)
			continue;
		emit_code(&e, r->action);
		emit(&e, "\t\t\tbreak;\n");
	}
	emit_lines(&e, skeleton_epilogue);

	emit_code(&e, doc->user_code);

	if (fflush(out) != 0 || ferror(out))
		return EXPECTED_ERR(error_syscall("failed to write the scanner", "fwrite"));
	return EXPECTED_OK();
}
