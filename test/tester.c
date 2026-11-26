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
 * @brief Corpus test runner, comparing ft_lex with flex
 *
 * Usage: `tester <corpus> <ft_lex> [filter]`
 *
 * For every `<name>.yy` file of the corpus whose name contains `filter`:
 *  1. A scanner is generated with both ft_lex and flex (`$FLEX`, `flex` by default), and
 *     compiled with `$CC` (`cc` by default) and the flags in `$SCANNER_CFLAGS`, separated by
 *     spaces. Both are linked with the lex library `$LIBL`, when set.
 *  2. Both scanners are run on every file of the `<name>/` directory, as their standard input.
 *  3. The test passes when, for every input, both scanners exit with the same status and write
 *     the same standard output and standard error, without any sanitizer report.
 *
 * `.yy` files must define `main` and `yywrap`, or rely on `$LIBL` for them. Their actions should
 * print a description of each token, which is what gets compared.
 *
 * Generated files and outputs are kept in `build/test/<name>/` for inspection.
 * Exits with status 0 when all selected tests pass.
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

/** @brief Select `.yy` files matching @ref filter */
static int
select_test(const struct dirent* ent)
{
	const size_t len = strlen(ent->d_name);
	if (len < 3 || strcmp(ent->d_name + len - 3, ".yy"))
		return 0;
	return !filter || strstr(ent->d_name, filter);
}

/** @brief Select input files, ignoring hidden files */
static int
select_input(const struct dirent* ent)
{
	return ent->d_name[0] != '.';
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

static bool
run_test(const char* corpus, const char* ft_lex, const char* file)
{
	printf("Test '%s': ", file);

	/* `<name>.yy` */
	char name[NAME_MAX + 1];
	snprintf(name, sizeof(name), "%.*s", (int)(strlen(file) - 3), file);

	char source[PATH_MAX];
	char inputs_dir[PATH_MAX];
	char dir[PATH_MAX];
	char binary_ft_lex[PATH_MAX];
	char binary_flex[PATH_MAX];
	if (!make_path(source, "%s/%s", corpus, file) ||
	    !make_path(inputs_dir, "%s/%s", corpus, name) ||
	    !make_path(dir, "%s/%s", WORK_DIR, name) ||
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

int
main(int argc, char** argv)
{
	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: %s <corpus> <ft_lex> [filter]\n", argv[0]);
		return 2;
	}
	const char* corpus = argv[1];
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

	/* Fail early when flex is missing, instead of failing every test */
	const char* const check[] = { flex, "--version", NULL };
	if (run(check, "/dev/null", "/dev/null", "/dev/null") == 127) {
		fprintf(stderr, "%s: '%s' could not be executed, install flex or set FLEX\n", argv[0], flex);
		return 2;
	}

	/* Sorted, so the output is the same everywhere */
	struct dirent** entries;
	const int count = scandir(corpus, &entries, select_test, alphasort);
	if (count == -1) {
		perror(corpus);
		return 2;
	}

	size_t passed = 0;
	const size_t total = (size_t)count;
	for (size_t i = 0; i < total; ++i) {
		passed += (size_t)run_test(corpus, ft_lex, entries[i]->d_name);
		free(entries[i]);
	}
	free(entries);
	free(scanner_cflags);

	printf("Result %zu/%zu (%zu%%)\n", passed, total, total == 0 ? 100 : ((passed * 100) / total));
	return passed == total ? 0 : 1;
}
