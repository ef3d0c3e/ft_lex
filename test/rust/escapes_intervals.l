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
\101\x42          token("OCTAL_HEX", self.text());
"\t"+             token("TABS", self.text());
\\[a-z]           token("BACKSLASH_LETTER", self.text());
\"[^"\n]*\"       token("QUOTED", self.text());
"\x2a"{2}         token("TWO_STARS", self.text());
x{3}              token("EXACTLY_3", self.text());
y{2,}             token("AT_LEAST_2", self.text());
z{1,3}            token("ONE_TO_THREE", self.text());
w{0,2}v           token("V_AFTER_0_TO_2_W", self.text());
[ \n]+            ;
.                 token("OTHER", self.text());
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
