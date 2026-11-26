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
she                    { token("SHE", self.text()); reject!(); }
he                     { token("HE", self.text()); reject!(); }
hers                   { token("HERS", self.text()); reject!(); }
his                    { token("HIS", self.text()); reject!(); }
[a-z]                  ;
.|\n                   ;
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
