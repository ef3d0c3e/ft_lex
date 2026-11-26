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
%x COMMENT
D   [0-9]
L   [a-zA-Z_]
H   [a-fA-F0-9]
E   ([Ee][+-]?{D}+)
IS  ((u|U)|(u|U)?(l|L|ll|LL)|(l|L|ll|LL)(u|U))
%%
"/*"                         { BEGIN COMMENT; }
<COMMENT>"*/"                { BEGIN INITIAL; token("COMMENT"); }
<COMMENT>.|\n                ;
"//".*                       token("LINE_COMMENT");
^[ \t]*#.*                   token("PREPROCESSOR");
"auto"|"break"|"case"|"char"|"const"|"continue"|"default"|"do"|"double"|"else"|"enum"|"extern"|"float"|"for"|"goto"|"if"|"inline"|"int"|"long"|"register"|"restrict"|"return"|"short"|"signed"|"sizeof"|"static"|"struct"|"switch"|"typedef"|"union"|"unsigned"|"void"|"volatile"|"while"  token("KEYWORD");
{L}({L}|{D})*                token("IDENTIFIER");
0[xX]{H}+{IS}?               token("HEX");
0[0-7]*{IS}?                 token("OCTAL");
[1-9]{D}*{IS}?               token("DECIMAL");
{D}+{E}                      token("FLOAT");
{D}*"."{D}+{E}?              token("FLOAT");
{D}+"."{D}*{E}?              token("FLOAT");
L?'(\\.|[^\\'\n])+'      token("CHAR");
L?\"(\\.|[^\\"\n])*\"    token("STRING");
"..."|">>="|"<<="|"+="|"-="|"*="|"/="|"%="|"&="|"^="|"|="|">>"|"<<"|"++"|"--"|"->"|"&&"|"||"|"<="|">="|"=="|"!="  token("OPERATOR");
[;{},:=()\[\].&!~+*/%<>^|?-] token("PUNCTUATOR");
[ \t\v\n\f]+                 ;
.                            token("UNKNOWN");
%%
int yywrap(void) { return 1; }
int main(void) { while (yylex() != 0) ; return 0; }
