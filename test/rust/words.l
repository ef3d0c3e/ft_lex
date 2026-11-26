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
%array
LETTER [A-Za-z]
DIGIT  [0-9]
%%
{LETTER}+('{LETTER}+)?   token("WORD", self.text());
{DIGIT}+(\.{DIGIT}+)?    token("NUMBER", self.text());
[.,;:!?]                 token("PUNCT", self.text());
[ \t]+                   ;
\n                       token("NEWLINE", self.text());
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
