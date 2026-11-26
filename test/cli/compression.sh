# -C (bonus) writes a scanner at least 2x smaller, with packed tables, that scans like the default
# one, and -v reports the compression
fail() { echo "$@"; exit 1; }
lexer="$TESTS/corpus/c_tokens.l"
input="$TESTS/corpus/c_tokens/hello.c"

"$FT_LEX" -t "$lexer" > full.c
"$FT_LEX" -t -C "$lexer" > compressed.c
full=$(wc -c < full.c)
compressed=$(wc -c < compressed.c)
test $((compressed * 2)) -le "$full" || fail "-C: $compressed bytes, not 2x smaller than $full"
grep -q 'define YY_COMPRESSED 1' compressed.c || fail "-C: tables are not packed"

"$CC" -o full full.c
"$CC" -o compressed compressed.c
./full < "$input" > full.out
./compressed < "$input" > compressed.out
test -s full.out || fail "the scanner produced no output"
cmp -s full.out compressed.out || fail "-C: the scanner does not scan like the default one"

"$FT_LEX" -t -C -v "$lexer" 2>&1 > /dev/null | grep -q 'compressed (' || fail "-v: no compression statistics"
