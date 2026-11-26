%{
/// Tokens returned by `lex`, only printed
#[derive(Debug)]
#[allow(dead_code)]
enum Token {
    Number(i64),
    Word(String),
    Operator(char),
}
%}
%%
	let mut calls = 0;
	calls += 1;
[0-9]+          { return Some(Token::Number(yytext.parse().unwrap())); }
"+" |
"-"             { return Some(Token::Operator(yytext.chars().next().unwrap())); }
[a-z]+          {
    // Lifetimes, labels, nested comments, raw strings, multi-line strings and character
    // literals with braces must not end the action block
    fn first<'a>(s: &'a str) -> &'a str { &s[..1] }
    /* nested /* comment } */ still a comment { */
    let raw = r#"raw "string" with } and {"#;
    let multi = "multi-line
string }";
    let braces = ['{', '}'];
    let mut length = 0;
    'outer: for _ in 0..1 {
        length = yyleng;
        break 'outer;
    }
    assert_eq!((raw.len(), multi.len(), braces.len()), (25, 19, 2));
    assert_eq!(calls, 1);
    return Some(Token::Word(format!("{}{}:{}", first(&yytext), &yytext[1..], length)));
}
[ \t\n]+        ;
.               { self.echo(); }
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while let Some(token) = lexer.lex() {
        println!("{:?}", token);
    }
}
