#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @file
 * @brief Test runner
 *
 * Usage: `tester <tests> <ft_lex> [filter]`
 *
 * Tests are grouped in suites, which are directories of `<tests>`. A test is named
 * `<suite>/<file>`, and only tests whose name contains `filter` are run (e.g. `errors/`).
 *
 * Suite `corpus`, comparing ft_lex with flex. For every `<name>.l`:
 *  1. A scanner is generated with both ft_lex and flex (`$FLEX`, `flex` by default), and
 *     compiled with `$CC` (`cc` by default) and the flags in `$SCANNER_CFLAGS`, separated by
 *     spaces. Both are linked with the lex library `$LIBL`, when set.
 *  2. Both scanners are run on every file of the `<name>/` directory, as their standard input.
 *  3. The test passes when, for every input, both scanners exit with the same status and write
 *     the same standard output and standard error, without any sanitizer report.
 *
 * `.l` files must define `main` and `yywrap`, or rely on `$LIBL` for them. Their actions should
 * print a description of each token, which is what gets compared.
 *
 * Suite `scanners`, for behaviour flex does not have or gets wrong. For every `<name>.l`, a
 * scanner is built with ft_lex only, as above, and run on every file of `<name>/` except
 * `*.expected` files. The test passes when, for every input, the scanner exits with status 0,
 * without sanitizer report, and its standard output is exactly `<name>/<input>.expected`.
 *
 * Suite `errors`, checking that invalid inputs are reported correctly. For every `<name>.l`,
 * `<name>.expected` describes the expected error, one `key: value` per line:
 *  - `type`: error type, as displayed (e.g. `Unexpected Token`), required
 *  - `at`: `line:column` of the error, required
 *  - `message`: diagnostic message, optional
 * Empty lines and lines starting with `#` are ignored. The test passes when ft_lex exits with
 * status 1, writes no scanner, and reports the expected error.
 *
 * Suite `cli`, for the command line interface. Every `<name>.sh` is run with `sh -e`, in an empty
 * working directory, with the environment variables `FT_LEX` (absolute path of ft_lex), `TESTS`
 * (absolute path of `<tests>`) and `CC`. The test passes when the script exits with status 0.
 *
 * flex is only needed when corpus tests are selected. Generated files and outputs are kept in
 * `build/test/<suite>/<name>/` for inspection. Exits with status 0 when all selected tests pass.
 */

#define WORK_DIR "build/test"
/** @brief Seconds before a command is killed */
#define TIMEOUT 10

/** @brief Name filter used by @ref select_test, NULL for none */
static const char* filter = NULL;
static const char* flex = "flex";
static const char* cc = "cc";
/** @brief Lex library linked with the scanners, NULL for none */
static const char* libl = NULL;
/** @brief Extra flags for compiling scanners, NULL-terminated */
static char** scanner_cflags = NULL;
static size_t scanner_cflag_count = 0;

/** @brief Split @p flags on whitespace into @ref scanner_cflags */
static void
parse_scanner_cflags(const char* flags)
{
	static char buf[4096];
	snprintf(buf, sizeof(buf), "%s", flags);
	scanner_cflags = malloc((strlen(buf) / 2 + 2) * sizeof(*scanner_cflags));
	if (!scanner_cflags) {
		perror("malloc");
		exit(2);
	}
	for (char* word = strtok(buf, " \t"); word; word = strtok(NULL, " \t"))
		scanner_cflags[scanner_cflag_count++] = word;
	scanner_cflags[scanner_cflag_count] = NULL;
}

/**
 * @brief Format a path into @p out, a `PATH_MAX` buffer
 *
 * @return Whether the path fits, an error is printed otherwise
 */
static bool __attribute__((__format__(__printf__, 2, 3)))
make_path(char* out, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const int len = vsnprintf(out, PATH_MAX, fmt, args);
	va_end(args);
	if (len < 0 || len >= PATH_MAX) {
		printf("FAILED\n  path too long: '%s...'\n", out);
		return false;
	}
	return true;
}

/** @brief Suite being listed by @ref select_test */
static const char* listed_suite = NULL;
/** @brief File suffix of the tests of @ref listed_suite */
static const char* listed_suffix = NULL;

/** @brief Whether @p name ends with @p suffix */
static bool
has_suffix(const char* name, const char* suffix)
{
	const size_t len = strlen(name);
	const size_t suffix_len = strlen(suffix);
	return len > suffix_len && !strcmp(name + len - suffix_len, suffix);
}

/** @brief Copy @p file without its @p suffix into @p name, a `NAME_MAX + 1` buffer */
static void
strip_suffix(char* name, const char* file, const char* suffix)
{
	snprintf(name, NAME_MAX + 1, "%.*s", (int)(strlen(file) - strlen(suffix)), file);
}

/** @brief Select test files whose test name, `<suite>/<file>`, matches @ref filter */
static int
select_test(const struct dirent* ent)
{
	if (!has_suffix(ent->d_name, listed_suffix))
		return 0;
	if (!filter)
		return 1;
	char name[PATH_MAX];
	snprintf(name, sizeof(name), "%s/%s", listed_suite, ent->d_name);
	return strstr(name, filter) != NULL;
}

/** @brief Select input files, ignoring hidden files */
static int
select_input(const struct dirent* ent)
{
	return ent->d_name[0] != '.';
}

/** @brief Select input files, ignoring hidden files and expected outputs */
static int
select_input_with_expectations(const struct dirent* ent)
{
	return ent->d_name[0] != '.' && !has_suffix(ent->d_name, ".expected");
}

/** @brief Create @p path and its parents */
static bool
make_dirs(const char* path)
{
	char buf[PATH_MAX];
	snprintf(buf, sizeof(buf), "%s", path);
	for (char* p = buf + 1;; ++p) {
		if (*p != '/' && *p != 0)
			continue;
		const char saved = *p;
		*p = 0;
		if (mkdir(buf, 0755) == -1 && errno != EEXIST) {
			perror(buf);
			return false;
		}
		if (saved == 0)
			return true;
		*p = saved;
	}
}

/** @brief Redirect @p fd to @p path in a child process, exit on failure */
static void
redirect(int fd, const char* path, int flags)
{
	const int file = open(path, flags, 0644);
	if (file == -1 || dup2(file, fd) == -1) {
		perror(path);
		_exit(127);
	}
	close(file);
}

/**
 * @brief Run a command and wait for it
 *
 * @param argv NULL-terminated arguments, `argv[0]` is looked up in `PATH`
 * @param in File for the standard input, NULL to inherit
 * @param out File for the standard output, NULL to inherit
 * @param err File for the standard error, NULL to inherit
 * @return The exit status, `128 + signal` if the command was killed (including by the timeout),
 * `127` if it could not be executed, or `-1` if it could not be started
 */
static int
run(const char* const* argv, const char* in, const char* out, const char* err)
{
	/* Do not duplicate buffered output in the child */
	fflush(stdout);
	fflush(stderr);

	const pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		if (in)
			redirect(STDIN_FILENO, in, O_RDONLY);
		if (out)
			redirect(STDOUT_FILENO, out, O_WRONLY | O_CREAT | O_TRUNC);
		if (err)
			redirect(STDERR_FILENO, err, O_WRONLY | O_CREAT | O_TRUNC);
		/* Kept across `execvp`, kills the command if it runs too long */
		alarm(TIMEOUT);
		execvp(argv[0], (char* const*)argv);
		_exit(127);
	}

	int status;
	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR) {
			perror("waitpid");
			return -1;
		}
	}
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return WEXITSTATUS(status);
}

/** @brief Describe an exit status returned by @ref run */
static void
print_status(int status)
{
	if (status == 128 + SIGALRM)
		printf("timed out after %d seconds", TIMEOUT);
	else if (status > 128)
		printf("killed by signal %d", status - 128);
	else if (status == 127)
		printf("could not be executed");
	else
		printf("exit status %d", status);
}

/** @brief Print the first lines of @p path, indented */
static void
print_file(const char* path)
{
	FILE* f = fopen(path, "r");
	if (!f)
		return;
	char line[512];
	for (int i = 0; i < 20 && fgets(line, sizeof(line), f); ++i)
		printf("    %s%s", line, strchr(line, '\n') ? "" : "\n");
	fclose(f);
}

/** @brief Whether two files have the same content */
static bool
same_content(const char* a, const char* b)
{
	FILE* fa = fopen(a, "rb");
	FILE* fb = fopen(b, "rb");
	bool same = fa && fb;
	while (same) {
		const int ca = getc(fa);
		const int cb = getc(fb);
		if (ca != cb)
			same = false;
		if (ca == EOF)
			break;
	}
	if (fa)
		fclose(fa);
	if (fb)
		fclose(fb);
	return same;
}

/** @brief Whether @p path contains an AddressSanitizer, LeakSanitizer or UBSan report */
static bool
has_sanitizer_report(const char* path)
{
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	char line[1024];
	bool found = false;
	while (!found && fgets(line, sizeof(line), f))
		found = strstr(line, "Sanitizer") || strstr(line, "runtime error:");
	fclose(f);
	return found;
}

/**
 * @brief Generate a scanner with @p generator and compile it to `<dir>/<name>`
 *
 * @return Whether the scanner was built, failures are reported
 */
static bool
build_scanner(const char* dir, const char* name, const char* generator, const char* source)
{
	char c_file[PATH_MAX];
	char binary[PATH_MAX];
	char log[PATH_MAX];
	if (!make_path(c_file, "%s/%s.c", dir, name) || !make_path(binary, "%s/%s", dir, name) ||
	    !make_path(log, "%s/%s.log", dir, name))
		return false;

	const char* const generate[] = { generator, "-t", source, NULL };
	int status = run(generate, NULL, c_file, log);
	if (status != 0) {
		printf("FAILED\n  %s: ", generator);
		print_status(status);
		printf("\n");
		print_file(log);
		return false;
	}

	/* cc [flags...] -o binary file.c [libl] */
	const char** compile = malloc((scanner_cflag_count + 6) * sizeof(*compile));
	if (!compile) {
		perror("malloc");
		exit(2);
	}
	size_t argc = 0;
	compile[argc++] = cc;
	for (size_t i = 0; i < scanner_cflag_count; ++i)
		compile[argc++] = scanner_cflags[i];
	compile[argc++] = "-o";
	compile[argc++] = binary;
	compile[argc++] = c_file;
	compile[argc++] = libl;
	compile[argc] = NULL;
	status = run(compile, NULL, NULL, log);
	free(compile);
	if (status != 0) {
		printf("FAILED\n  compiling the %s scanner: ", name);
		print_status(status);
		printf("\n");
		print_file(log);
		return false;
	}
	return true;
}

/** @brief Run test `corpus/<file>`, from the suite directory @p corpus */
static bool
run_corpus_test(const char* corpus, const char* ft_lex, const char* file)
{
	printf("Test 'corpus/%s': ", file);

	char name[NAME_MAX + 1];
	strip_suffix(name, file, ".l");

	char source[PATH_MAX];
	char inputs_dir[PATH_MAX];
	char dir[PATH_MAX];
	char binary_ft_lex[PATH_MAX];
	char binary_flex[PATH_MAX];
	if (!make_path(source, "%s/%s", corpus, file) ||
	    !make_path(inputs_dir, "%s/%s", corpus, name) ||
	    !make_path(dir, "%s/corpus/%s", WORK_DIR, name) ||
	    !make_path(binary_ft_lex, "%s/ft_lex", dir) || !make_path(binary_flex, "%s/flex", dir))
		return false;
	if (!make_dirs(dir)) {
		printf("FAILED\n");
		return false;
	}

	struct dirent** inputs;
	const int count = scandir(inputs_dir, &inputs, select_input, alphasort);
	if (count <= 0) {
		printf("FAILED\n  no inputs in '%s'\n", inputs_dir);
		if (count == 0)
			free(inputs);
		return false;
	}

	bool passed = build_scanner(dir, "ft_lex", ft_lex, source) &&
	              build_scanner(dir, "flex", flex, source);

	for (int i = 0; passed && i < count; ++i) {
		const char* input_name = inputs[i]->d_name;
		char input[PATH_MAX];
		char out_ft_lex[PATH_MAX];
		char out_flex[PATH_MAX];
		char err_ft_lex[PATH_MAX];
		char err_flex[PATH_MAX];
		if (!make_path(input, "%s/%s", inputs_dir, input_name) ||
		    !make_path(out_ft_lex, "%s/%s.ft_lex.out", dir, input_name) ||
		    !make_path(out_flex, "%s/%s.flex.out", dir, input_name) ||
		    !make_path(err_ft_lex, "%s/%s.ft_lex.err", dir, input_name) ||
		    !make_path(err_flex, "%s/%s.flex.err", dir, input_name)) {
			passed = false;
			break;
		}

		const char* const run_ft_lex[] = { binary_ft_lex, NULL };
		const int status_ft_lex = run(run_ft_lex, input, out_ft_lex, err_ft_lex);

		const char* const run_flex[] = { binary_flex, NULL };
		const int status_flex = run(run_flex, input, out_flex, err_flex);

		const bool report_ft_lex = has_sanitizer_report(err_ft_lex);
		const bool report_flex = has_sanitizer_report(err_flex);
		if (report_ft_lex || report_flex) {
			/* Even when identical in both scanners, e.g. caused by an action */
			printf("FAILED\n  input '%s': sanitizer error\n", input_name);
			if (report_ft_lex) {
				printf("  ft_lex scanner standard error:\n");
				print_file(err_ft_lex);
			}
			if (report_flex) {
				printf("  flex scanner standard error:\n");
				print_file(err_flex);
			}
			passed = false;
		} else if (status_ft_lex != status_flex) {
			/* Show the standard error, e.g. sanitizer reports */
			printf("FAILED\n  input '%s': ft_lex scanner ", input_name);
			print_status(status_ft_lex);
			printf(", flex scanner ");
			print_status(status_flex);
			printf("\n  ft_lex scanner standard error:\n");
			print_file(err_ft_lex);
			printf("  flex scanner standard error:\n");
			print_file(err_flex);
			passed = false;
		} else if (!same_content(out_flex, out_ft_lex)) {
			printf("FAILED\n  input '%s': outputs differ (- flex, + ft_lex)\n", input_name);
			const char* const diff[] = { "diff", "-u", out_flex, out_ft_lex, NULL };
			run(diff, NULL, NULL, NULL);
			passed = false;
		} else if (!same_content(err_flex, err_ft_lex)) {
			/* Also catches sanitizer reports, which differ between scanners */
			printf("FAILED\n  input '%s': standard errors differ (- flex, + ft_lex)\n", input_name);
			const char* const diff[] = { "diff", "-u", err_flex, err_ft_lex, NULL };
			run(diff, NULL, NULL, NULL);
			passed = false;
		}
	}

	if (passed)
		printf("ok (%d input%s)\n", count, count > 1 ? "s" : "");
	for (int i = 0; i < count; ++i)
		free(inputs[i]);
	free(inputs);
	return passed;
}

/* {{{ Error tests */

/** @brief An error, as expected by a test or as reported by ft_lex */
typedef struct
{
	char type[128];
	char at[64];
	char message[512];
	bool has_type;
	bool has_at;
	bool has_message;
} error_report;

/** @brief Remove the trailing newline and ANSI escape sequences (colors) from @p line */
static void
clean_line(char* line)
{
	char* out = line;
	for (const char* in = line; *in && *in != '\n';) {
		if (in[0] == '\033' && in[1] == '[') {
			/* Parameter and intermediate bytes, then a final byte */
			in += 2;
			while (*in >= 0x20 && *in <= 0x3f)
				++in;
			if (*in)
				++in;
			continue;
		}
		*out++ = *in++;
	}
	*out = 0;
}

/** @brief Copy @p src to the @p size bytes of @p dst, return false if it does not fit */
static bool
copy_field(char* dst, size_t size, const char* src)
{
	const int len = snprintf(dst, size, "%s", src);
	return len >= 0 && (size_t)len < size;
}

/**
 * @brief Read the expectations of an error test
 *
 * @return Whether the file is valid, problems are printed otherwise
 */
static bool
read_expected(const char* path, error_report* expected)
{
	*expected = (error_report){};
	FILE* f = fopen(path, "r");
	if (!f) {
		printf("FAILED\n  cannot read '%s'\n", path);
		return false;
	}

	char line[1024];
	bool valid = true;
	for (size_t number = 1; valid && fgets(line, sizeof(line), f); ++number) {
		clean_line(line);
		if (line[0] == 0 || line[0] == '#')
			continue;

		const char* value = strstr(line, ": ");
		if (value) {
			line[value - line] = 0;
			value += 2;
		}
		if (value && !strcmp(line, "type"))
			valid = expected->has_type = copy_field(expected->type, sizeof(expected->type), value);
		else if (value && !strcmp(line, "at"))
			valid = expected->has_at = copy_field(expected->at, sizeof(expected->at), value);
		else if (value && !strcmp(line, "message"))
			valid = expected->has_message =
			  copy_field(expected->message, sizeof(expected->message), value);
		else
			valid = false;
		if (!valid)
			printf("FAILED\n  %s:%zu: expected `type: ...', `at: ...' or `message: ...'\n", path, number);
	}
	fclose(f);

	if (valid && (!expected->has_type || !expected->has_at)) {
		printf("FAILED\n  %s: `type' and `at' are required\n", path);
		valid = false;
	}
	return valid;
}

/**
 * @brief Parse the error displayed by ft_lex in @p path:
 *
 *     ERR[<status>]: <type> at <file>:<line>:<column>
 *     <source excerpt>
 *
 *     <message>
 */
static void
read_report(const char* path, error_report* report)
{
	*report = (error_report){};
	FILE* f = fopen(path, "r");
	if (!f)
		return;

	enum { kHeader, kExcerpt, kMessage } state = kHeader;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		clean_line(line);
		if (state == kHeader) {
			const char* type = strncmp(line, "ERR[", 4) ? NULL : strstr(line, "]: ");
			if (!type)
				continue;
			type += 3;

			/* The type ends at the last " at " */
			char* at = NULL;
			for (char* p = strstr(line, " at "); p; p = strstr(p + 1, " at "))
				at = p;
			if (!at)
				continue;
			*at = 0;
			report->has_type = copy_field(report->type, sizeof(report->type), type);

			/* `<file>:<line>:<column>`, the file may contain `:` */
			char* column = strrchr(at + 4, ':');
			if (column) {
				*column = 0;
				char* line_number = strrchr(at + 4, ':');
				*column = ':';
				if (line_number)
					report->has_at = copy_field(report->at, sizeof(report->at), line_number + 1);
			}
			state = kExcerpt;
		} else if (state == kExcerpt) {
			if (line[0] == 0)
				state = kMessage;
		} else {
			report->has_message = copy_field(report->message, sizeof(report->message), line);
			break;
		}
	}
	fclose(f);
}

/** @brief Run test `errors/<file>`, from the suite directory @p errors */
static bool
run_error_test(const char* errors, const char* ft_lex, const char* file)
{
	printf("Test 'errors/%s': ", file);

	char name[NAME_MAX + 1];
	strip_suffix(name, file, ".l");

	char source[PATH_MAX];
	char expected_path[PATH_MAX];
	char dir[PATH_MAX];
	char out[PATH_MAX];
	char err[PATH_MAX];
	if (!make_path(source, "%s/%s", errors, file) ||
	    !make_path(expected_path, "%s/%s.expected", errors, name) ||
	    !make_path(dir, "%s/errors/%s", WORK_DIR, name) || !make_path(out, "%s/stdout", dir) ||
	    !make_path(err, "%s/stderr", dir))
		return false;
	if (!make_dirs(dir)) {
		printf("FAILED\n");
		return false;
	}

	error_report expected;
	if (!read_expected(expected_path, &expected))
		return false;

	const char* const generate[] = { ft_lex, "-t", source, NULL };
	const int status = run(generate, "/dev/null", out, err);

	struct stat out_stat;
	const bool wrote_output = stat(out, &out_stat) == 0 && out_stat.st_size != 0;
	error_report report;
	read_report(err, &report);

	if (has_sanitizer_report(err)) {
		printf("FAILED\n  sanitizer error in ft_lex\n");
	} else if (status != 1) {
		printf("FAILED\n  expected exit status 1, got ");
		print_status(status);
		printf("\n");
	} else if (wrote_output) {
		printf("FAILED\n  a scanner was written despite the error\n");
	} else if (!report.has_type || !report.has_at) {
		printf("FAILED\n  no error report found\n");
	} else {
		const bool type_ok = !strcmp(expected.type, report.type);
		const bool at_ok = !strcmp(expected.at, report.at);
		const bool message_ok = !expected.has_message ||
		                        (report.has_message && !strcmp(expected.message, report.message));
		if (type_ok && at_ok && message_ok) {
			printf("ok\n");
			return true;
		}
		printf("FAILED\n");
		if (!type_ok)
			printf("  type: expected `%s', got `%s'\n", expected.type, report.type);
		if (!at_ok)
			printf("  at: expected `%s', got `%s'\n", expected.at, report.at);
		if (!message_ok)
			printf("  message: expected `%s'\n           got      `%s'\n",
			       expected.message,
			       report.has_message ? report.message : "");
	}
	printf("  ft_lex standard error:\n");
	print_file(err);
	return false;
}

/* }}} */

/* {{{ Scanner tests */

/** @brief Run test `scanners/<file>`, from the suite directory @p scanners */
static bool
run_scanner_test(const char* scanners, const char* ft_lex, const char* file)
{
	printf("Test 'scanners/%s': ", file);

	char name[NAME_MAX + 1];
	strip_suffix(name, file, ".l");

	char source[PATH_MAX];
	char inputs_dir[PATH_MAX];
	char dir[PATH_MAX];
	char binary[PATH_MAX];
	if (!make_path(source, "%s/%s", scanners, file) ||
	    !make_path(inputs_dir, "%s/%s", scanners, name) ||
	    !make_path(dir, "%s/scanners/%s", WORK_DIR, name) || !make_path(binary, "%s/ft_lex", dir))
		return false;
	if (!make_dirs(dir)) {
		printf("FAILED\n");
		return false;
	}

	struct dirent** inputs;
	const int count = scandir(inputs_dir, &inputs, select_input_with_expectations, alphasort);
	if (count <= 0) {
		printf("FAILED\n  no inputs in '%s'\n", inputs_dir);
		if (count == 0)
			free(inputs);
		return false;
	}

	bool passed = build_scanner(dir, "ft_lex", ft_lex, source);
	for (int i = 0; passed && i < count; ++i) {
		const char* input_name = inputs[i]->d_name;
		char input[PATH_MAX];
		char expected[PATH_MAX];
		char out[PATH_MAX];
		char err[PATH_MAX];
		if (!make_path(input, "%s/%s", inputs_dir, input_name) ||
		    !make_path(expected, "%s/%s.expected", inputs_dir, input_name) ||
		    !make_path(out, "%s/%s.out", dir, input_name) ||
		    !make_path(err, "%s/%s.err", dir, input_name)) {
			passed = false;
			break;
		}

		const char* const scanner[] = { binary, NULL };
		const int status = run(scanner, input, out, err);
		if (has_sanitizer_report(err) || status != 0) {
			printf("FAILED\n  input '%s': ", input_name);
			print_status(status);
			printf("\n  standard error:\n");
			print_file(err);
			passed = false;
		} else if (access(expected, R_OK) != 0) {
			printf("FAILED\n  input '%s': missing '%s'\n", input_name, expected);
			passed = false;
		} else if (!same_content(expected, out)) {
			printf("FAILED\n  input '%s': output differs (- expected, + ft_lex)\n", input_name);
			const char* const diff[] = { "diff", "-u", expected, out, NULL };
			run(diff, NULL, NULL, NULL);
			passed = false;
		}
	}

	if (passed)
		printf("ok (%d input%s)\n", count, count > 1 ? "s" : "");
	for (int i = 0; i < count; ++i)
		free(inputs[i]);
	free(inputs);
	return passed;
}

/* }}} */

/* {{{ Command line tests */

/** @brief Absolute path of @p path, which must exist */
static bool
absolute_path(char* out, const char* path)
{
	if (!realpath(path, out)) {
		printf("FAILED\n  ");
		fflush(stdout);
		perror(path);
		return false;
	}
	return true;
}

/** @brief Run test `cli/<file>`, from the suite directory @p cli */
static bool
run_cli_test(const char* cli, const char* ft_lex, const char* file)
{
	printf("Test 'cli/%s': ", file);

	char name[NAME_MAX + 1];
	strip_suffix(name, file, ".sh");

	char script[PATH_MAX];
	char script_abs[PATH_MAX];
	char ft_lex_abs[PATH_MAX];
	char tests_abs[PATH_MAX];
	char dir[PATH_MAX];
	char log[PATH_MAX];
	if (!make_path(script, "%s/%s", cli, file) || !make_path(dir, "%s/cli/%s", WORK_DIR, name) ||
	    !make_path(log, "%s.log", dir) || !absolute_path(script_abs, script) ||
	    !absolute_path(ft_lex_abs, ft_lex) || !absolute_path(tests_abs, cli))
		return false;
	/* `<tests>/cli` -> `<tests>` */
	*strrchr(tests_abs, '/') = 0;

	/* Start from an empty working directory */
	const char* const clean[] = { "rm", "-rf", dir, NULL };
	if (run(clean, NULL, NULL, NULL) != 0 || !make_dirs(dir)) {
		printf("FAILED\n  cannot create '%s'\n", dir);
		return false;
	}

	fflush(stdout);
	fflush(stderr);
	const pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return false;
	}
	if (pid == 0) {
		redirect(STDIN_FILENO, "/dev/null", O_RDONLY);
		redirect(STDOUT_FILENO, log, O_WRONLY | O_CREAT | O_TRUNC);
		dup2(STDOUT_FILENO, STDERR_FILENO);
		if (chdir(dir) == -1 || setenv("FT_LEX", ft_lex_abs, 1) == -1 ||
		    setenv("TESTS", tests_abs, 1) == -1 || setenv("CC", cc, 1) == -1)
			_exit(127);
		alarm(TIMEOUT);
		execlp("sh", "sh", "-e", script_abs, (char*)NULL);
		_exit(127);
	}
	int status;
	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR) {
			perror("waitpid");
			return false;
		}
	}
	status = WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);

	if (status == 0 && !has_sanitizer_report(log)) {
		printf("ok\n");
		return true;
	}
	printf("FAILED\n  script: ");
	print_status(status);
	printf("\n  output:\n");
	print_file(log);
	return false;
}

/* }}} */

/** @brief A suite, a directory of the tests directory */
typedef struct
{
	const char* name;
	/** @brief Suffix of test files */
	const char* suffix;
	/** @brief Run the test `<name>/<file>`, from the suite directory `dir` */
	bool (*run)(const char* dir, const char* ft_lex, const char* file);
	/** @brief Selected tests, sorted */
	struct dirent** tests;
	int count;
} suite;

int
main(int argc, char** argv)
{
	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: %s <tests> <ft_lex> [filter]\n", argv[0]);
		return 2;
	}
	const char* tests_dir = argv[1];
	const char* ft_lex = argv[2];
	if (argc == 4)
		filter = argv[3];
	if (getenv("FLEX"))
		flex = getenv("FLEX");
	if (getenv("CC"))
		cc = getenv("CC");
	if (getenv("LIBL") && *getenv("LIBL"))
		libl = getenv("LIBL");
	parse_scanner_cflags(getenv("SCANNER_CFLAGS") ? getenv("SCANNER_CFLAGS") : "");

	/* The corpus suite must stay first, see the flex check below */
	suite suites[] = {
		{ .name = "corpus", .suffix = ".l", .run = run_corpus_test },
		{ .name = "scanners", .suffix = ".l", .run = run_scanner_test },
		{ .name = "errors", .suffix = ".l", .run = run_error_test },
		{ .name = "cli", .suffix = ".sh", .run = run_cli_test },
	};
	const size_t suite_count = sizeof(suites) / sizeof(suites[0]);

	/* Select tests first, to know whether flex is needed */
	for (size_t i = 0; i < suite_count; ++i) {
		char dir[PATH_MAX];
		if (!make_path(dir, "%s/%s", tests_dir, suites[i].name))
			return 2;
		listed_suite = suites[i].name;
		listed_suffix = suites[i].suffix;
		/* Sorted, so the output is the same everywhere */
		suites[i].count = scandir(dir, &suites[i].tests, select_test, alphasort);
		if (suites[i].count == -1) {
			perror(dir);
			return 2;
		}
	}

	/* Fail early when flex is missing, instead of failing every corpus test */
	if (suites[0].count > 0) {
		const char* const check[] = { flex, "--version", NULL };
		if (run(check, "/dev/null", "/dev/null", "/dev/null") == 127) {
			fprintf(stderr,
			        "%s: '%s' could not be executed, install flex or set FLEX\n",
			        argv[0],
			        flex);
			return 2;
		}
	}

	size_t passed = 0;
	size_t total = 0;
	for (size_t i = 0; i < suite_count; ++i) {
		char dir[PATH_MAX];
		if (!make_path(dir, "%s/%s", tests_dir, suites[i].name))
			return 2;
		for (int j = 0; j < suites[i].count; ++j) {
			passed += (size_t)suites[i].run(dir, ft_lex, suites[i].tests[j]->d_name);
			++total;
			free(suites[i].tests[j]);
		}
		free(suites[i].tests);
	}
	free(scanner_cflags);

	printf("Result %zu/%zu (%zu%%)\n", passed, total, total == 0 ? 100 : ((passed * 100) / total));
	return passed == total ? 0 : 1;
}
