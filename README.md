![Test badge](https://github.com/ef3d0c3e/ft_lex/actions/workflows/tests.yml/badge.svg)

# Usage

`ft_lex` supports the arguments from <https://pubs.opengroup.org/onlinepubs/9699919799/utilities/lex.html>.
Additionally, it supports the following arguments:
 - `-L` (default 'c') to select the language. `c` and `rust` are supported
 - `-C` enable compression

# Running Tests

To run tests, make sure `flex` and `diff` are installed.
Then run `make test` to run all tests.

# References
 * <https://en.wikipedia.org/wiki/Thompson%27s_construction>
 * <https://pubs.opengroup.org/onlinepubs/9699919799/utilities/lex.html>
 * <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04>
 * <https://ftp.gnu.org/old-gnu/Manuals/flex-2.5.4/html_mono/flex.html>

# License

This project is licensed under the GNU GPL Version 3. See [LICENSE](./LICENSE) for more information.
