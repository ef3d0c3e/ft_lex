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
\101\x42          token("OCTAL_HEX");
"\t"+             token("TABS");
\\[a-z]           token("BACKSLASH_LETTER");
\"[^"\n]*\"       token("QUOTED");
"\x2a"{2}         token("TWO_STARS");
x{3}              token("EXACTLY_3");
y{2,}             token("AT_LEAST_2");
z{1,3}            token("ONE_TO_THREE");
w{0,2}v           token("V_AFTER_0_TO_2_W");
[ \n]+            ;
.                 token("OTHER");
%%
int yywrap(void) { return 1; }
int main(void) { yylex(); return 0; }
