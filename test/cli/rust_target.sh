# -L rust (bonus) writes the lexer to lex.yy.rs, and unknown languages are usage errors
fail() { echo "$@"; exit 1; }
lexer="$TESTS/rust/words.l"

"$FT_LEX" -L rust "$lexer" > stdout
test -s lex.yy.rs || fail "lex.yy.rs was not written"
test ! -e lex.yy.c || fail "lex.yy.c was written"
test ! -s stdout || fail "unexpected standard output"
grep -q 'pub fn lex(&mut self) -> Option<Token>' lex.yy.rs || fail "no Rust lexer in lex.yy.rs"

"$FT_LEX" -t -L rust "$lexer" | grep -q 'pub struct Lexer' || fail "-t: no Rust lexer on the standard output"
"$FT_LEX" -t -L c "$TESTS/corpus/words.l" | grep -q 'int yylex(void)' || fail "-L c: no C scanner"

if "$FT_LEX" -t -L cobol "$lexer" > /dev/null 2> stderr; then status=0; else status=$?; fi
test "$status" -eq 2 || fail "unknown language: status $status"
grep -q 'cobol' stderr || fail "unknown language: not named in the error"
