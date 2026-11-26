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

/** @brief Language of the generated scanner */
typedef enum
{
	/** @brief C scanner, as specified by POSIX */
	kLanguageC,
	/** @brief Rust lexer (bonus), see @ref codegen_rust */
	kLanguageRust,
} target_language;

typedef struct
{
	target_language language;
	/** @brief Pack the transition table (see @ref dfa_packed) instead of writing it in full */
	bool compress;
} codegen_options;

/**
 * @brief Write the scanner for @p doc to @p out
 *
 * @param doc Parsed document
 * @param d Automaton built from @p doc
 * @param out Output stream
 * @param out_name Name of the output, used in `#line` directives
 * @param options Target language and compression
 */
[[nodiscard]] EXPECTED()
codegen(const document* doc, const dfa* d, FILE* out, const char* out_name, codegen_options options);

/**
 * @brief Write a Rust lexer for @p doc to @p out
 *
 * Same specification format, with Rust code fragments and actions. The output defines:
 *  - a constant for each start condition (`INITIAL`, ...)
 *  - `pub struct Lexer`, created with `Lexer::new(input)` (any `std::io::Read`), with:
 *    - `lex(&mut self) -> Option<Token>`: the next token, `None` at the end of the input. `Token`
 *      must be defined by the definitions section (e.g. an `enum`, or `type Token = ();`)
 *    - `wrap`: called at the end of the input (like `yywrap`), returns the next input, or `None`
 *      to stop
 *    - `set_output`: where `echo` and the default rule write, the standard output by default
 *    - for actions: `begin`, `condition`, `echo`, `yymore`, `yyless`, `input`, `unput`, `text`
 *
 * In actions, `yytext` (a `String`, lossily decoded as UTF-8) and `yyleng` (its length in bytes)
 * hold a copy of the matched text, `return Some(token)` returns a token, and `reject!()` selects
 * the next best match. `%array` and `%pointer` have no effect.
 */
[[nodiscard]] EXPECTED()
codegen_rust(const document* doc, const dfa* d, FILE* out, const char* out_name, bool compress);

/** @} */

#endif // CODEGEN_H
