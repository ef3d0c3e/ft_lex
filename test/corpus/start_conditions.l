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
%x STRING COMMENT
%s DECL
%%
"/*"                   { BEGIN COMMENT; token("COMMENT_START"); }
<COMMENT>"*/"          { BEGIN INITIAL; token("COMMENT_END"); }
<COMMENT>[^*\n]+       token("COMMENT_TEXT");
<COMMENT>\*            token("COMMENT_STAR");
<COMMENT>\n            token("COMMENT_NEWLINE");
\"                     { BEGIN STRING; token("STRING_START"); }
<STRING>\\.          token("STRING_ESCAPE");
<STRING>[^"\\\n]+    token("STRING_TEXT");
<STRING>\"             { BEGIN INITIAL; token("STRING_END"); }
<STRING>\n             { BEGIN INITIAL; token("UNTERMINATED"); }
"let"                  { BEGIN DECL; token("LET"); }
<DECL>[a-z]+           token("DECL_NAME");
<DECL>";"              { BEGIN INITIAL; token("END_DECL"); }
[a-z]+                 token("IDENT");
[ \t\n]+               ;
.                      token("OTHER");
%%
int yywrap(void) { return 1; }
int main(void) { while (yylex() != 0) ; return 0; }
