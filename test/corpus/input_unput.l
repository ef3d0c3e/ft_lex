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
"/*"           {
	int c, previous = 0;
	token("COMMENT_START");
	while ((c = input()) != 0) {
		if (previous == '*' && c == '/')
			break;
		previous = c;
	}
	printf("COMMENT_END(%s)\n", c ? "closed" : "end of file");
}
@[a-z]+         {
	int i;
	token("MENTION");
	for (i = yyleng - 1; i > 0; --i)
		unput(yytext[i]);
}
[a-z]+          token("WORD");
[ \t\n]+        ;
.               token("OTHER");
%%
int yywrap(void) { return 1; }
int main(void) { yylex(); return 0; }
