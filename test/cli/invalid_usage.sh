# Usage errors exit with status 2, other errors with status 1, and no lex.yy.c is left behind
fail() { echo "$@"; exit 1; }
status_of() { if "$@" > stdout 2> stderr; then status=0; else status=$?; fi; }

status_of "$FT_LEX" -x "$TESTS/corpus/words.l"
test "$status" -eq 2 || fail "invalid option: status $status"
grep -q 'usage' stderr || fail "invalid option: no usage message"

status_of "$FT_LEX" missing.l
test "$status" -eq 1 || fail "missing file: status $status"
grep -q 'missing.l' stderr || fail "missing file: not named in the error"

printf '%%%%\na) ;\n' > invalid.l
status_of "$FT_LEX" invalid.l
test "$status" -eq 1 || fail "invalid input: status $status"
grep -q 'invalid.l:2:2' stderr || fail "invalid input: error not located"
test ! -e lex.yy.c || fail "lex.yy.c left behind"
