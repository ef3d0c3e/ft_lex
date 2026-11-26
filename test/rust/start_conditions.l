%{
type Token = ();

/// Print a token as `KIND(text)`, escaping control and non-ASCII bytes
fn token(kind: &str, text: &[u8]) {
    let mut out = String::new();
    for &c in text {
        match c {
            b'\n' => out.push_str("\\n"),
            b'\t' => out.push_str("\\t"),
            c if c < 32 || c >= 127 => out.push_str(&format!("\\x{:02x}", c)),
            c => out.push(c as char),
        }
    }
    println!("{}({})", kind, out);
}
%}
%x STRING COMMENT
%s DECL
%%
"/*"                   { self.begin(COMMENT); token("COMMENT_START", self.text()); }
<COMMENT>"*/"          { self.begin(INITIAL); token("COMMENT_END", self.text()); }
<COMMENT>[^*\n]+       token("COMMENT_TEXT", self.text());
<COMMENT>\*            token("COMMENT_STAR", self.text());
<COMMENT>\n            token("COMMENT_NEWLINE", self.text());
\"                     { self.begin(STRING); token("STRING_START", self.text()); }
<STRING>\\.          token("STRING_ESCAPE", self.text());
<STRING>[^"\\\n]+    token("STRING_TEXT", self.text());
<STRING>\"             { self.begin(INITIAL); token("STRING_END", self.text()); }
<STRING>\n             { self.begin(INITIAL); token("UNTERMINATED", self.text()); }
"let"                  { self.begin(DECL); token("LET", self.text()); }
<DECL>[a-z]+           token("DECL_NAME", self.text());
<DECL>";"              { self.begin(INITIAL); token("END_DECL", self.text()); }
[a-z]+                 token("IDENT", self.text());
[ \t\n]+               ;
.                      token("OTHER", self.text());
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
