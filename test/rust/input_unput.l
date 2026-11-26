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
"/*"           {
    token("COMMENT_START", self.text());
    let mut previous = 0u8;
    let mut closed = false;
    while let Some(c) = self.input() {
        if previous == b'*' && c == b'/' {
            closed = true;
            break;
        }
        previous = c;
    }
    println!("COMMENT_END({})", if closed { "closed" } else { "end of file" });
}
@[a-z]+         {
    token("MENTION", self.text());
    let text = self.text().to_vec();
    for &c in text[1..].iter().rev() {
        self.unput(c);
    }
}
[a-z]+          token("WORD", self.text());
[ \t\n]+        ;
.               token("OTHER", self.text());
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
