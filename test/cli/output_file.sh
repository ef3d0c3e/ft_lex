# Without -t, the scanner is written to lex.yy.c, and nothing to the standard output
fail() { echo "$@"; exit 1; }

"$FT_LEX" "$TESTS/corpus/words.l" > stdout
test -s lex.yy.c || fail "lex.yy.c was not written"
test ! -s stdout || fail "unexpected standard output"
"$CC" -o scanner lex.yy.c
echo "hello world" | ./scanner | grep -q 'WORD(hello)' || fail "the scanner does not work"
