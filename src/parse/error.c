#include "parse.h"

#include <stdio.h>

static const char* error_names[] = {
	[kParseGeneric] = "Error",
	[kParseExpectedToken] = "Expected Token",
	[kParseUnexpectedToken] = "Unexpected Token",
	[kParseRedefinition] = "Redefinition",
	[kParseUndefined] = "Undefined",
};

static const char* error_messages[] = {
	[kParseGeneric] = "",
	[kParseExpectedToken] = "expected `{0:.{1}}'",
	[kParseUnexpectedToken] = "unexpected `{0:.{1}}'",
	[kParseRedefinition] = "`{0:.{1}}' is already defined",
	[kParseUndefined] = "`{0:.{1}}' is not defined",
};

/**
 * @brief Build cursor from absolute position
 *
 * @p pos must be within the document, or equal to its size (end of document)
 */
[[nodiscard]]
static cursor
make_cursor(const document* doc, size_t pos)
{
	assert(pos <= doc->size);

	cursor cursor = { 0, 1, 0 };
	while (cursor.pos != pos) {
		++cursor.column;
		if (doc->content[cursor.pos] == '\n') {
			cursor.column = 0;
			++cursor.line;
		}
		++cursor.pos;
	}
	return cursor;
}

/**
 * @brief Get the byte-span `[start, end)` of the line containing @p pos
 *
 * The span excludes the terminating newline. If @p pos is on a newline, the line is the one that
 * newline terminates.
 */
static void
get_line_span(const document* doc, size_t pos, size_t* start, size_t* end)
{
	assert(pos <= doc->size);

	size_t i = pos;
	while (i > 0 && doc->content[i - 1] != '\n')
		--i;
	*start = i;

	const char* line_end = memchr(doc->content + pos, '\n', doc->size - pos);
	*end = line_end ? (size_t)(line_end - doc->content) : doc->size;
}

void
parser_display_error(struct format_output* out, const parse_error* err, const document* doc)
{
	assert(err->status != kParseOk);
	assert(err->start <= err->end && err->end <= doc->size);

	const auto start = make_cursor(doc, err->start);
	/* Use the last byte covered by the span, so a span ending right after a newline does not
	 * display the following line */
	const auto end = make_cursor(doc, err->end > err->start ? err->end - 1 : err->start);

	/* Lines are displayed relative to the input file containing the error */
	size_t file_line;
	const source* src = document_locate(doc, err->start, start.line, &file_line);
	const size_t line_offset = start.line - file_line;

	format(out,
	       "ERR[{}]: {} at {}:{}:{}\n",
	       (int)err->status,
	       error_names[err->status],
	       src->name,
	       file_line,
	       start.column + 1);

	/* Show line content */
	const auto gutter_width = snprintf(NULL, 0, "%zu", end.line - line_offset);
	size_t pos = err->start;
	for (size_t line = start.line; line <= end.line; ++line) {
		size_t line_start, line_end;
		get_line_span(doc, pos, &line_start, &line_end);
		pos = line_end + 1;

		format(out, " {:>{1}} | ", line - line_offset, gutter_width);

		/* Split the line into: [prefix | colored | suffix], where each component can have a size
		 * of 0 but the total length must equal to the line's length */
		const auto line_length = line_end - line_start;
		const size_t color_start = err->start > line_start ? err->start : line_start;
		const size_t color_end = err->end < line_end ? err->end : line_end;
		size_t prefix = line_length;
		size_t suffix = 0;
		if (color_start < color_end) {
			prefix = color_start - line_start;
			suffix = line_end - color_end;
		}
		const size_t colored = line_length - prefix - suffix;
		assert(prefix + colored + suffix == line_length);

		(void)format_output_write(out, doc->content + line_start, prefix);
		if (colored != 0) {
			format(out, "{fg#ff0000}");
			(void)format_output_write(out, doc->content + line_start + prefix, colored);
			format(out, "{/0}");
		}
		(void)format_output_write(out, doc->content + line_start + prefix + colored, suffix);
		format(out, "\n");

		/* Nothing to color (empty span, or span covering only the newline/end of document): point
		 * at the error's start with a caret instead */
		if (line == start.line && colored == 0) {
			format(out, " {:>{1}} | ", "", gutter_width);
			for (size_t i = line_start; i < err->start; ++i)
				format(out, "{}", doc->content[i] == '\t' ? "\t" : " ");
			format(out, "{fg#ff0000}^{/0}\n");
		}
	}

	/* Show diagnostic */
	format(out, "\n");
	switch (err->status) {
		case kParseExpectedToken:
		case kParseUnexpectedToken:
		case kParseRedefinition:
		case kParseUndefined:
			format(out, error_messages[err->status], err->args.token.str, err->args.token.len);
			if (err->reason)
				format(out, ": {}", err->reason);
			break;
		case kParseGeneric:
			format(out, "{}", err->reason ? err->reason : "unknown error");
			break;
		default:
			assert(0);
	}
	format(out, "\n");
}
