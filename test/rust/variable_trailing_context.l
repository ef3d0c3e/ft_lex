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
(a|ab)/(b|bc)     token("SPLIT", self.text());
x+/x*y            token("X_BEFORE_Y", self.text());
[a-z]             token("OTHER", self.text());
\n                ;
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
