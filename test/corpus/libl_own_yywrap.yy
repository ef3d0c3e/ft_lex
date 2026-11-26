%{
#include <stdio.h>
static void token(const char *kind)
{
	int i;
	printf("%s(", kind);
	for (i = 0; i < yyleng; ++i) {
		unsigned char c = (unsigned char)yytext[i];
		if (c == '\n')
			printf("\\n");
		else if (c == '\t')
			printf("\\t");
		else if (c < 32 || c >= 127)
			printf("\\x%02x", c);
		else
			putchar(c);
	}
	printf(")\n");
}
%}
%%
[a-z]+    { token("WORD"); return 1; }
[0-9]+    token("NUMBER");
[ \t\n]+  ;
%%
int yywrap(void)
{
	printf("own yywrap\n");
	return 1;
}
