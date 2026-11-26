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
%x COMMENT
D   [0-9]
L   [a-zA-Z_]
H   [a-fA-F0-9]
E   ([Ee][+-]?{D}+)
IS  ((u|U)|(u|U)?(l|L|ll|LL)|(l|L|ll|LL)(u|U))
%%
"/*"                         { self.begin(COMMENT); }
<COMMENT>"*/"                { self.begin(INITIAL); token("COMMENT", self.text()); }
<COMMENT>.|\n                ;
"//".*                       token("LINE_COMMENT", self.text());
^[ \t]*#.*                   token("PREPROCESSOR", self.text());
"auto"|"break"|"case"|"char"|"const"|"continue"|"default"|"do"|"double"|"else"|"enum"|"extern"|"float"|"for"|"goto"|"if"|"inline"|"int"|"long"|"register"|"restrict"|"return"|"short"|"signed"|"sizeof"|"static"|"struct"|"switch"|"typedef"|"union"|"unsigned"|"void"|"volatile"|"while"  token("KEYWORD", self.text());
{L}({L}|{D})*                token("IDENTIFIER", self.text());
0[xX]{H}+{IS}?               token("HEX", self.text());
0[0-7]*{IS}?                 token("OCTAL", self.text());
[1-9]{D}*{IS}?               token("DECIMAL", self.text());
{D}+{E}                      token("FLOAT", self.text());
{D}*"."{D}+{E}?              token("FLOAT", self.text());
{D}+"."{D}*{E}?              token("FLOAT", self.text());
L?'(\\.|[^\\'\n])+'      token("CHAR", self.text());
L?\"(\\.|[^\\"\n])*\"    token("STRING", self.text());
"..."|">>="|"<<="|"+="|"-="|"*="|"/="|"%="|"&="|"^="|"|="|">>"|"<<"|"++"|"--"|"->"|"&&"|"||"|"<="|">="|"=="|"!="  token("OPERATOR", self.text());
[;{},:=()\[\].&!~+*/%<>^|?-] token("PUNCTUATOR", self.text());
[ \t\v\n\f]+                 ;
.                            token("UNKNOWN", self.text());
%%
fn main() {
    let mut lexer = Lexer::new(std::io::stdin());
    while lexer.lex().is_some() {}
}
