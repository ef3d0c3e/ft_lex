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
^[a-z]+         token("LINE_START");
[a-z]+          token("WORD");
[0-9]+          token("NUMBER");
[ \t\n]+        ;
%%
static int passes = 0;
int yywrap(void)
{
	if (++passes == 1) {
		printf("REWIND\n");
		rewind(yyin);
		return 0;
	}
	printf("END\n");
	return 1;
}
int main(void) { yylex(); return 0; }
