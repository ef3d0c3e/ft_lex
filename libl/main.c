/**
 * @file
 * @brief Default `main` of the lex library (`-l l`)
 *
 * Calls `yylex()` until it returns `0` (end of input), so actions that return tokens do not stop
 * the scanner. This matches flex's `libfl`.
 *
 * Kept in its own object file, so programs defining their own `yywrap` can still use it.
 */

int
yylex(void);

int
main(void)
{
	while (yylex() != 0)
		;
	return 0;
}
