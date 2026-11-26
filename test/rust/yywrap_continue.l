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
^[a-z]+         token("LINE_START", self.text());
[a-z]+          token("WORD", self.text());
[0-9]+          token("NUMBER", self.text());
[ \t\n]+        ;
%%
fn main() {
    let mut data = Vec::new();
    std::io::Read::read_to_end(&mut std::io::stdin(), &mut data).unwrap();
    let mut lexer = Lexer::new(std::io::Cursor::new(data.clone()));
    let mut passes = 0;
    lexer.wrap = Some(Box::new(move || {
        passes += 1;
        if passes == 1 {
            println!("REWIND");
            return Some(Box::new(std::io::Cursor::new(data.clone())) as Box<dyn std::io::Read>);
        }
        println!("END");
        None
    }));
    while lexer.lex().is_some() {}
}
