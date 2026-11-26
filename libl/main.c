/**
 * @file
 * @brief Default `main` of the lex library (`-l l`)
 *
 * As specified by POSIX.1-2024: "Calls yylex() to perform lexical analysis, then exits". `yylex()`
 * is called once, so an action returning a token ends the program.
 *
 * Kept in its own object file, so programs defining their own `yywrap` can still use it.
 */

int
yylex(void);

int
main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	yylex();
	return 0;
}
