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
^#[a-z]+               token("DIRECTIVE");
^[ \t]*\n              token("BLANK_LINE");
end$                   token("END_OF_LINE");
[0-9]+/px              token("PIXELS");
[0-9]+/"%"             token("PERCENT");
[a-z_]+/"("            token("CALL");
ab/c+                  token("AB_BEFORE_C");
[a-z_]+                token("IDENT");
[0-9]+                 token("NUMBER");
[ \t\n]                ;
.                      token("OTHER");
%%
int yywrap(void) { return 1; }
int main(void) { while (yylex() != 0) ; return 0; }
