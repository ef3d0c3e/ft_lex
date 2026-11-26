# The standard input is read when there are no file operands, and for the `-` operand
fail() { echo "$@"; exit 1; }
words="$TESTS/corpus/words.l"

"$FT_LEX" -t "$words" | grep -v '^#line' > from_file.c
"$FT_LEX" -t < "$words" | grep -v '^#line' > from_stdin.c
"$FT_LEX" -t - < "$words" | grep -v '^#line' > from_dash.c
cmp -s from_file.c from_stdin.c || fail "no operand: different scanner"
cmp -s from_file.c from_dash.c || fail "'-': different scanner"
"$FT_LEX" -t < "$words" | grep -q '"<stdin>"' || fail "#line directives do not name the standard input"

# `-` mixed with a file operand
printf '%%%%\n{DIGIT}+ ;\n' > rules.l
printf 'DIGIT [0-9]\n' | "$FT_LEX" -t - rules.l > scanner.c || fail "'-' followed by a file failed"
