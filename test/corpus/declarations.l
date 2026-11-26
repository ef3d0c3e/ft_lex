%p 3000
%n 600
%a 2500
%e 1200
%k 1100
%o 3500
%pointer
%S INCLUSIVE
%X EXCLUSIVE
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
"<"             { BEGIN EXCLUSIVE; token("OPEN"); }
<EXCLUSIVE>">"  { BEGIN INITIAL; token("CLOSE"); }
<EXCLUSIVE>[^>] token("INSIDE");
"["             { BEGIN INCLUSIVE; token("OPEN_INCLUSIVE"); }
<INCLUSIVE>"]"  { BEGIN INITIAL; token("CLOSE_INCLUSIVE"); }
[a-z]+          token("WORD");
.|\n            ;
%%
int yywrap(void) { return 1; }
int main(void) { yylex(); return 0; }
