#include <stdio.h>
/* A comment
 * over several lines */
int main(int argc, char **argv)
{
	// line comment
	unsigned long n = 0x1Fu + 017 + 42UL;
	double d = 1.5e-3 + .25 + 3.;
	char c = '\n';
	const char *s = "say \"hi\"\t";
	for (int i = 0; i < argc; ++i)
		n += (n >> 2) <<= 1, s++;
	return n != 0 && argv[0][0] == 'x' ? 1 : 0;
}
