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
^#[a-z]+               token("DIRECTIVE", self.text());
^[ \t]*\n              token("BLANK_LINE", self.text());
end$                   token("END_OF_LINE", self.text());
[0-9]+/px              token("PIXELS", self.text());
[0-9]+/"%"             token("PERCENT", self.text());
[a-z_]+/"("            token("CALL", self.text());
ab/c+                  token("AB_BEFORE_C", self.text());
[a-z_]+                token("IDENT", self.text());
[0-9]+                 token("NUMBER", self.text());
[ \t\n]                ;
.                      token("OTHER", self.text());
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
