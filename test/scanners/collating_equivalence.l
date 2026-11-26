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
[[=a=]b]+           token("EQUIVALENCE");
[[.-.]x]+           token("COLLATING");
[[.0.]-[.3.]]+      token("COLLATING_RANGE");
[ \n]               ;
.                   token("OTHER");
%%
int yywrap(void) { return 1; }
int main(void) { yylex(); return 0; }
