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
%%
"<"                    { self.yymore(); }
"<"[a-z]+">"           token("TAG", self.text());
"%"[a-z]+              { self.yyless(2); token("PERCENT_LETTER", self.text()); }
[a-z]+                 token("WORD", self.text());
[ \t\n]+               ;
.                      token("OTHER", self.text());
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
