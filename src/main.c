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
 * Usage: `ft_lex [-t] [-C] [-L language] [-n|-v] [file...]`
 *  - `-t`: write the scanner to the standard output instead of `lex.yy.c` (`lex.yy.rs` for Rust)
 *  - `-C`: compress the automaton: minimize the DFA and pack its transition table (bonus, not
 *    part of POSIX)
 *  - `-L language`: language of the code in the input and of the scanner, `c` (default) or `rust`
 *    (bonus, not part of POSIX)
 *  - `-n`: suppress the statistics summary
 *  - `-v`: write a statistics summary to the standard output, or to the standard error with `-t`
 *    (as specified by POSIX, the standard output then holds the scanner)
 *
 * Input files are concatenated. The standard input is read when no file is given, or for `-`.
 */

static void
usage(const char* program)
{
	fprintf(stderr, "usage: %s [-t] [-C] [-L c|rust] [-n|-v] [file...]\n", program);
}

/**
 * @brief Write the statistics summary
 *
 * @param d Automaton as built
 * @param minimized Automaton written to the scanner when compressing, NULL otherwise
 */
static void
print_statistics(FILE* out, const document* doc, const nfa* n, const dfa* d, const dfa* minimized)
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
	if (!minimized) {
		fprintf(out, "transition table: %zu entries\n", d->state_count * d->class_count);
		return;
	}

	dfa_packed packed = dfa_pack(minimized);
	const size_t full = d->state_count * d->class_count;
	const size_t minimized_full = minimized->state_count * minimized->class_count;
	size_t compressed = dfa_packed_entries(&packed, minimized->state_count);
	/* The scanner keeps the minimized full table when packing does not make it smaller */
	if (compressed >= minimized_full)
		compressed = minimized_full;
	fprintf(out,
	        "%zu DFA states after minimization\n"
	        "transition table: %zu entries, %zu compressed (%.1fx smaller)\n",
	        minimized->state_count,
	        full,
	        compressed,
	        compressed ? (double)full / (double)compressed : 0.0);
	dfa_packed_destroy(&packed);
}

int
main(int argc, char** argv)
{
	bool to_stdout = false;
	bool verbose = false;
	bool quiet = false;
	bool compress = false;
	target_language language = kLanguageC;

	int opt;
	while ((opt = getopt(argc, argv, "tCL:nv")) != -1) {
		switch (opt) {
			case 'L':
				if (!strcmp(optarg, "c"))
					language = kLanguageC;
				else if (!strcmp(optarg, "rust"))
					language = kLanguageRust;
				else {
					fprintf(stderr, "%s: unknown language `%s', expected `c' or `rust'\n", argv[0], optarg);
					usage(argv[0]);
					return 2;
				}
				break;
			case 't':
				to_stdout = true;
				break;
			case 'C':
				compress = true;
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
	  parse(&errors,
	        (const char* const*)argv + optind,
	        (size_t)(argc - optind),
	        language == kLanguageRust ? "rust" : "c");
	if (result.flag) {
		display_error(&errors, &result.error);
		format_output_destroy(&errors);
		return 1;
	}
	document* doc = &result.value;

	nfa n = nfa_build(doc);
	dfa d = dfa_build(&n);
	if (compress) {
		dfa minimized = dfa_minimize(&d);
		if (verbose && !quiet)
			print_statistics(to_stdout ? stderr : stdout, doc, &n, &d, &minimized);
		dfa_destroy(&d);
		d = minimized;
	} else if (verbose && !quiet)
		print_statistics(to_stdout ? stderr : stdout, doc, &n, &d, NULL);
	nfa_destroy(&n);

	const char* out_name =
	  to_stdout ? "<stdout>" : (language == kLanguageRust ? "lex.yy.rs" : "lex.yy.c");
	FILE* out = to_stdout ? stdout : fopen(out_name, "w");
	int status = 0;
	if (!out) {
		perror(out_name);
		status = 1;
	} else {
		const auto generated =
		  codegen(doc, &d, out, out_name, (codegen_options){ .language = language, .compress = compress });
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
