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
"<"                    { yymore(); }
"<"[a-z]+">"           token("TAG");
"%"[a-z]+              { yyless(2); token("PERCENT_LETTER"); }
[a-z]+                 token("WORD");
[ \t\n]+               ;
.                      token("OTHER");
%%
int yywrap(void) { return 1; }
int main(void) { while (yylex() != 0) ; return 0; }
