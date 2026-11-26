/**
 * @file
 * @brief Default `yywrap` of the lex library (`-l l`)
 *
 * Returns `1`: there is no more input after the end of `yyin`.
 *
 * Kept in its own object file, so programs defining their own `main` can still use it.
 */

int
yywrap(void);

int
yywrap(void)
{
	return 1;
}
