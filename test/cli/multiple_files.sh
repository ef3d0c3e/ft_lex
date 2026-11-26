# Input files are concatenated in order, and errors name the file and its line
fail() { echo "$@"; exit 1; }

printf 'DIGIT [0-9]\n' > definitions.l
printf '%%%%\n{DIGIT}+ printf("NUMBER(%%s)\\n", yytext);\n.|\\n ;\n%%%%\nint yywrap(void) { return 1; }\nint main(void) { yylex(); return 0; }\n' > rules.l
"$FT_LEX" -t definitions.l rules.l > scanner.c
"$CC" -o scanner scanner.c
test "$(echo 'a 12 b 345' | ./scanner)" = "$(printf 'NUMBER(12)\nNUMBER(345)')" || fail "wrong scanner output"

printf '%%%%\n{UNDEFINED} ;\n' > bad_rules.l
if "$FT_LEX" -t definitions.l bad_rules.l > /dev/null 2> stderr; then fail "undefined name accepted"; fi
grep -q 'bad_rules.l:2:2' stderr || fail "error not located in bad_rules.l, line 2"
