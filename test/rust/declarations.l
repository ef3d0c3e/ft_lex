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
%p 3000
%n 600
%a 2500
%e 1200
%k 1100
%o 3500
%pointer
%S INCLUSIVE
%X EXCLUSIVE
%%
"<"             { self.begin(EXCLUSIVE); token("OPEN", self.text()); }
<EXCLUSIVE>">"  { self.begin(INITIAL); token("CLOSE", self.text()); }
<EXCLUSIVE>[^>] token("INSIDE", self.text());
"["             { self.begin(INCLUSIVE); token("OPEN_INCLUSIVE", self.text()); }
<INCLUSIVE>"]"  { self.begin(INITIAL); token("CLOSE_INCLUSIVE", self.text()); }
[a-z]+          token("WORD", self.text());
.|\n            ;
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
