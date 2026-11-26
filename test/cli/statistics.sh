# -v writes statistics to the standard output, or to the standard error with -t (as the standard
# output then holds the scanner). -n, the default, suppresses them
fail() { echo "$@"; exit 1; }
words="$TESTS/corpus/words.l"

"$FT_LEX" -v "$words" > stdout 2> stderr
grep -q 'DFA states' stdout || fail "-v: no statistics on the standard output"
test ! -s stderr || fail "-v: unexpected standard error"
test -s lex.yy.c || fail "-v: lex.yy.c was not written"

"$FT_LEX" -t -v "$words" > stdout 2> stderr
grep -q 'DFA states' stderr || fail "-t -v: no statistics on the standard error"
if grep -q 'DFA states' stdout; then fail "-t -v: statistics mixed with the scanner"; fi
grep -q 'int yylex(void)' stdout || fail "-t -v: no scanner on the standard output"

for options in "" "-n"; do
	"$FT_LEX" $options "$words" > stdout 2> stderr
	test ! -s stdout || fail "'$options': unexpected standard output"
	test ! -s stderr || fail "'$options': unexpected standard error"
done
