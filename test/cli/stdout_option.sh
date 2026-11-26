# -t writes the scanner to the standard output, and no lex.yy.c
fail() { echo "$@"; exit 1; }

"$FT_LEX" -t "$TESTS/corpus/words.l" > scanner.c
test ! -e lex.yy.c || fail "lex.yy.c was written"
grep -q 'int yylex(void)' scanner.c || fail "no scanner on the standard output"
"$CC" -o scanner scanner.c
