#ifndef CODEGEN_H
#define CODEGEN_H

#include "../automaton/automaton.h"

#include <stdio.h>

/**
 * @defgroup Codegen Code generation
 * @brief Generation of the C scanner (`lex.yy.c`)
 *
 * The generated scanner provides, as required by POSIX:
 *  - `int yylex(void)`, `int yymore(void)`, `int yyless(int)`, `int input(void)`,
 *    `int unput(int)`
 *  - `yytext` (`char[YYLMAX]` with `%array`, `char *` otherwise), `int yyleng`,
 *    `FILE *yyin`, `FILE *yyout`
 *  - the `ECHO`, `REJECT` and `BEGIN` macros, and a macro for each start condition
 *
 * `int yywrap(void)` (and `main`) must be provided by the user, or by linking with `libl`.
 *
 * Runtime behaviour:
 *  - Matches are the longest, then the first rule. Empty matches, and matches whose `yytext`
 *    would be empty because of trailing context, are ignored.
 *  - Input that no rule matches is copied to `yyout`, one byte at a time.
 *  - `input()` returns `0` at end of file.
 *  - Fatal errors (out of memory, token longer than `YYLMAX` with `%array`) are reported once on
 *    the standard error, then `yylex()` returns `0` as if the input had ended. `exit()` is never
 *    called: the generated code only uses `<stdio.h>`, `<string.h>` and `realloc`.
 *  - `REJECT` selects the next best match: the next rule matching the same text, then rules
 *    matching shorter prefixes. It must not be used after `input()`, `unput()` or `yyless()` in
 *    the same action.
 * @{
 */

/**
 * @brief Write the scanner for @p doc to @p out
 *
 * @param doc Parsed document
 * @param d Automaton built from @p doc
 * @param out Output stream
 * @param out_name Name of the output, used in `#line` directives
 */
[[nodiscard]] EXPECTED()
codegen(const document* doc, const dfa* d, FILE* out, const char* out_name);

/** @} */

#endif // CODEGEN_H
