#include "emit.h"

#include <limits.h>
#include <stdarg.h>

void
emit_bytes(emitter* e, const char* bytes, size_t len)
{
	fwrite(bytes, 1, len, e->out);
	for (size_t i = 0; i < len; ++i)
		e->line += bytes[i] == '\n';
}

void
emit(emitter* e, const char* str)
{
	emit_bytes(e, str, strlen(str));
}

void
emitf(emitter* e, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char small[256];
	const int len = vsnprintf(small, sizeof(small), fmt, args);
	va_end(args);
	assert(len >= 0);

	if ((size_t)len < sizeof(small)) {
		emit_bytes(e, small, (size_t)len);
		return;
	}
	char* buf = xmalloc((size_t)len + 1);
	va_start(args, fmt);
	vsnprintf(buf, (size_t)len + 1, fmt, args);
	va_end(args);
	emit_bytes(e, buf, (size_t)len);
	free(buf);
}

void
emit_lines(emitter* e, const char* const* lines)
{
	for (size_t i = 0; lines[i]; ++i)
		emit(e, lines[i]);
}

void
emit_values(emitter* e, const long* values, size_t count, const char* indent)
{
	for (size_t i = 0; i < count; ++i) {
		if (i % 16 == 0)
			emitf(e, "\n%s", indent);
		emitf(e, "%ld,", values[i]);
		if (i % 16 != 15 && i + 1 != count)
			emit(e, " ");
	}
	emit(e, "\n");
}

const char*
source_display_name(const source* src)
{
	return strcmp(src->name, "-") ? src->name : "<stdin>";
}

long
state_value(size_t state)
{
	if (state == DFA_DEAD)
		return -1;
	assert(state <= INT_MAX);
	return (long)state;
}

bool
pack_if_smaller(const dfa* d, bool compress, dfa_packed* packed)
{
	if (!compress)
		return false;
	*packed = dfa_pack(d);
	if (dfa_packed_entries(packed, d->state_count) < d->state_count * d->class_count)
		return true;
	dfa_packed_destroy(packed);
	return false;
}

/** @brief Write `<prefix>byte_class` and `<prefix>next`, the full transition table of @p d */
static void
write_full_table(emitter* e, const dfa* d, const char* class_name, const char* next_name, table_writer write)
{
	long classes[256];
	for (size_t c = 0; c < 256; ++c)
		classes[c] = (long)d->byte_class[c];
	write(e, class_name, kTableByte, classes, 256);

	const size_t count = d->state_count * d->class_count;
	long* next = xmalloc(count * sizeof(*next));
	for (size_t i = 0; i < count; ++i)
		next[i] = state_value(d->transitions[i]);
	write(e, next_name, kTableInt, next, count);
	free(next);
}

/** @brief Write the packed transition table */
static void
write_packed_table(emitter* e, const dfa* d, const dfa_packed* packed, table_writer write)
{
	long classes[256];
	for (size_t c = 0; c < 256; ++c)
		classes[c] = (long)d->byte_class[c];
	write(e, "yy_byte_class", kTableByte, classes, 256);

	const size_t n = d->state_count;
	long* values = xmalloc((packed->size > n ? packed->size : n) * sizeof(long));
	for (size_t s = 0; s < n; ++s) {
		assert(packed->base[s] <= INT_MAX);
		values[s] = (long)packed->base[s];
	}
	write(e, "yy_base", kTableInt, values, n);
	for (size_t s = 0; s < n; ++s)
		values[s] = packed->parent[s] == DFA_PACK_NONE ? -1 : state_value(packed->parent[s]);
	write(e, "yy_default", kTableInt, values, n);
	for (size_t i = 0; i < packed->size; ++i)
		values[i] = state_value(packed->next[i]);
	write(e, "yy_next", kTableInt, values, packed->size);
	for (size_t i = 0; i < packed->size; ++i)
		values[i] = packed->check[i] == DFA_PACK_NONE ? -1 : state_value(packed->check[i]);
	write(e, "yy_check", kTableInt, values, packed->size);
	free(values);
}

/** @brief Write the rule lists of each state as `<name>_base` and `<name>_list` */
static void
write_rule_lists(emitter* e, const dfa* d, bool accepts, const char* name, table_writer write)
{
	long* base = xmalloc((d->state_count + 1) * sizeof(*base));
	size_t total = 0;
	for (size_t i = 0; i < d->state_count; ++i) {
		base[i] = (long)total;
		total += accepts ? d->states[i].accept_count : d->states[i].head_end_count;
	}
	base[d->state_count] = (long)total;

	long* list = xmalloc(total * sizeof(*list));
	size_t n = 0;
	for (size_t i = 0; i < d->state_count; ++i) {
		const dfa_state* state = &d->states[i];
		const size_t count = accepts ? state->accept_count : state->head_end_count;
		for (size_t j = 0; j < count; ++j)
			list[n++] = (long)(accepts ? state->accepts[j] : state->head_ends[j]);
	}

	char table[64];
	snprintf(table, sizeof(table), "%s_base", name);
	write(e, table, kTableInt, base, d->state_count + 1);
	snprintf(table, sizeof(table), "%s_list", name);
	write(e, table, kTableInt, list, total);
	free(base);
	free(list);
}

void
emit_automaton_tables(emitter* e, const dfa* d, const dfa_packed* packed, table_writer write)
{
	if (packed)
		write_packed_table(e, d, packed, write);
	else
		write_full_table(e, d, "yy_byte_class", "yy_next", write);

	long* starts = xmalloc(d->start_count * sizeof(*starts));
	for (size_t i = 0; i < d->start_count; ++i)
		starts[i] = state_value(d->starts[i]);
	write(e, "yy_starts", kTableInt, starts, d->start_count);
	free(starts);

	write_rule_lists(e, d, true, "yy_accept", write);
	write_rule_lists(e, d, false, "yy_head", write);

	long* kinds = xmalloc(d->rule_count * sizeof(*kinds));
	long* lengths = xmalloc(d->rule_count * sizeof(*lengths));
	for (size_t i = 0; i < d->rule_count; ++i) {
		switch (d->rules[i].kind) {
			case kTrailingNone:
				kinds[i] = 0;
				break;
			case kTrailingFixedTail:
				kinds[i] = 1;
				break;
			case kTrailingFixedHead:
				kinds[i] = 2;
				break;
			case kTrailingVariable:
				kinds[i] = 3;
				break;
		}
		assert(d->rules[i].length <= LONG_MAX);
		lengths[i] = (long)d->rules[i].length;
	}
	write(e, "yy_trailing_kind", kTableInt, kinds, d->rule_count);
	write(e, "yy_trailing_length", kTableSize, lengths, d->rule_count);
	free(kinds);
	free(lengths);

	for (size_t i = 0; i < d->rule_count; ++i) {
		if (d->rules[i].kind != kTrailingVariable)
			continue;
		const dfa* reverse = &d->reverse_trailing[i];
		assert(reverse->start_count == 1);

		char class_name[64];
		char next_name[64];
		char accept_name[64];
		snprintf(class_name, sizeof(class_name), "yy_reverse_class_%zu", i);
		snprintf(next_name, sizeof(next_name), "yy_reverse_next_%zu", i);
		snprintf(accept_name, sizeof(accept_name), "yy_reverse_accept_%zu", i);
		write_full_table(e, reverse, class_name, next_name, write);

		long* accept = xmalloc(reverse->state_count * sizeof(*accept));
		for (size_t s = 0; s < reverse->state_count; ++s)
			accept[s] = reverse->states[s].accept_count != 0;
		write(e, accept_name, kTableByte, accept, reverse->state_count);
		free(accept);
	}
}
