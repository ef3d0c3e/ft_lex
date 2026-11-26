#include "codegen.h"
#include "emit.h"
#include "skeleton.h"

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
	emit_line_directive(e, line, source_display_name(src));

	const char* code = e->doc->content + location.start.pos;
	emit_bytes(e, code, len);
	if (code[len - 1] != '\n')
		emit(e, "\n");

	/* The directive refers to the line after it */
	emit_line_directive(e, e->line + 1, e->out_name);
}

/** @brief Write a table as a C array. Empty arrays get a single `0` element */
static void
write_c_table(emitter* e, const char* name, table_type type, const long* values, size_t count)
{
	const char* c_type = type == kTableByte ? "unsigned char" : type == kTableSize ? "size_t" : "int";
	emitf(e, "static const %s %s[] = {", c_type, name);
	if (count == 0)
		emit(e, "0\n");
	else
		emit_values(e, values, count, "\t");
	emit(e, "};\n");
}

static void
emit_tables(emitter* e, const dfa* d, bool compress)
{
	emit(e, "\n/* Automaton tables */\n");
	emitf(e, "#define YY_CLASSES %zu\n", d->class_count);

	/* Only pack when it is smaller, e.g. not for tiny tables with few byte classes */
	dfa_packed packed;
	const bool packing = pack_if_smaller(d, compress, &packed);
	emitf(e, "#define YY_COMPRESSED %d\n", packing ? 1 : 0);
	emit_automaton_tables(e, d, packing ? &packed : NULL, write_c_table);
	if (packing)
		dfa_packed_destroy(&packed);

	emit(e,
	     "struct yy_reverse\n{\n"
	     "\tconst unsigned char *byte_class;\n"
	     "\tconst int *next;\n"
	     "\tconst unsigned char *accept;\n"
	     "\tint classes;\n"
	     "\tint start;\n"
	     "};\n");
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

/** @brief Generate a C scanner */
[[nodiscard]] static EXPECTED()
codegen_c(const document* doc, const dfa* d, FILE* out, const char* out_name, bool compress)
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

	emit_tables(&e, d, compress);
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

EXPECTED()
codegen(const document* doc, const dfa* d, FILE* out, const char* out_name, codegen_options options)
{
	switch (options.language) {
		case kLanguageC:
			return codegen_c(doc, d, out, out_name, options.compress);
		case kLanguageRust:
			return codegen_rust(doc, d, out, out_name, options.compress);
	}
	assert(0);
	return EXPECTED_OK();
}
