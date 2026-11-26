#include "automaton/automaton.h"
#include "codegen/codegen.h"
#include "parse/parse.h"

#include <format.h>
#include <stdio.h>
#include <unistd.h>

/**
 * @file
 * @brief Command line interface
 *
 * Usage: `ft_lex [-t] [-n|-v] [file...]`
 *  - `-t`: write the scanner to the standard output instead of `lex.yy.c`
 *  - `-n`: suppress the statistics summary
 *  - `-v`: write a statistics summary to the standard output, or to the standard error with `-t`
 *    (as specified by POSIX, the standard output then holds the scanner)
 *
 * Input files are concatenated. The standard input is read when no file is given, or for `-`.
 */

static void
usage(const char* program)
{
	fprintf(stderr, "usage: %s [-t] [-n|-v] [file...]\n", program);
}

static void
print_statistics(FILE* out, const document* doc, const nfa* n, const dfa* d)
{
	size_t transitions = 0;
	for (size_t i = 0; i < d->state_count * d->class_count; ++i)
		transitions += d->transitions[i] != DFA_DEAD;

	fprintf(out,
	        "%zu rules, %zu start conditions\n"
	        "%zu NFA states\n"
	        "%zu DFA states, %zu transitions, %zu byte classes\n",
	        doc->rules.size,
	        doc->start_conditions.size,
	        n->state_count,
	        d->state_count,
	        transitions,
	        d->class_count);
}

int
main(int argc, char** argv)
{
	bool to_stdout = false;
	bool verbose = false;
	bool quiet = false;

	int opt;
	while ((opt = getopt(argc, argv, "tnv")) != -1) {
		switch (opt) {
			case 't':
				to_stdout = true;
				break;
			case 'n':
				quiet = true;
				break;
			case 'v':
				verbose = true;
				break;
			default:
				usage(argv[0]);
				return 2;
		}
	}

	struct format_output errors = format_output_fd(STDERR_FILENO);
	auto result =
	  parse(&errors, (const char* const*)argv + optind, (size_t)(argc - optind));
	if (result.flag) {
		display_error(&errors, &result.error);
		format_output_destroy(&errors);
		return 1;
	}
	document* doc = &result.value;

	nfa n = nfa_build(doc);
	dfa d = dfa_build(&n);
	if (verbose && !quiet)
		print_statistics(to_stdout ? stderr : stdout, doc, &n, &d);
	nfa_destroy(&n);

	const char* out_name = to_stdout ? "<stdout>" : "lex.yy.c";
	FILE* out = to_stdout ? stdout : fopen(out_name, "w");
	int status = 0;
	if (!out) {
		perror(out_name);
		status = 1;
	} else {
		const auto generated = codegen(doc, &d, out, out_name);
		if (generated.flag) {
			display_error(&errors, &generated.error);
			status = 1;
		}
		if (!to_stdout && fclose(out) != 0) {
			perror(out_name);
			status = 1;
		}
		if (status != 0 && !to_stdout)
			remove(out_name);
	}

	dfa_destroy(&d);
	document_destroy(doc);
	format_output_destroy(&errors);
	return status;
}
