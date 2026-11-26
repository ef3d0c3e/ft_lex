#ifndef EMIT_H
#define EMIT_H

/**
 * @file
 * @brief Output helpers and automaton tables shared by the code generators
 */

#include "../automaton/automaton.h"

#include <stdio.h>

/**
 * @defgroup Emit Emitter
 * @ingroup Codegen
 * @{
 */

typedef struct
{
	FILE* out;
	/** @brief Name of the output, for references to output lines */
	const char* out_name;
	const document* doc;
	/** @brief Current output line (1-based) */
	size_t line;
} emitter;

void
emit_bytes(emitter* e, const char* bytes, size_t len);

void
emit(emitter* e, const char* str);

void __attribute__((__format__(__printf__, 2, 3)))
emitf(emitter* e, const char* fmt, ...);

/** @brief Emit a NULL-terminated array of lines */
void
emit_lines(emitter* e, const char* const* lines);

/** @brief Emit comma-separated @p values, 16 per line, each line starting with @p indent */
void
emit_values(emitter* e, const long* values, size_t count, const char* indent);

/** @brief Name of the input file @p src for messages, `<stdin>` for the standard input */
[[nodiscard]] const char*
source_display_name(const source* src);

/** @brief State index as a table value, `-1` for @ref DFA_DEAD */
[[nodiscard]] long
state_value(size_t state);

/** @} */

/**
 * @defgroup Tables Automaton tables
 * @ingroup Codegen
 * @brief The tables of a scanner, as arrays of numbers that each code generator formats
 *
 * Tables (names given in lowercase):
 *  - `yy_byte_class` (bytes, 256 entries): byte class of each byte
 *  - full table: `yy_next` (states x classes)
 *  - packed table (see @ref dfa_packed): `yy_base`, `yy_default` (parent state, `-1` for none),
 *    `yy_next`, `yy_check` (`-1` for unused entries)
 *  - `yy_starts`: start state for `condition * 2 + at_beginning_of_line`
 *  - `yy_accept_base`, `yy_accept_list`: rules accepted by state `s` are
 *    `list[base[s]..base[s + 1]]`
 *  - `yy_head_base`, `yy_head_list`: same, for rules whose pattern ends in the state
 *  - `yy_trailing_kind` (0: none, 1: fixed length trailing context, 2: fixed length pattern,
 *    3: variable) and `yy_trailing_length` (sizes)
 *  - for each rule `i` with variable trailing context: `yy_reverse_class_<i>` (bytes),
 *    `yy_reverse_next_<i>` (full table) and `yy_reverse_accept_<i>` (bytes, `1` for accepting)
 *
 * State values are `-1` for the dead state.
 * @{
 */

typedef enum
{
	/** @brief Values in `[0, 255]` */
	kTableByte,
	/** @brief Values fitting an `int` (state indices, `-1`) */
	kTableInt,
	/** @brief Non-negative sizes */
	kTableSize,
} table_type;

/** @brief Write the table @p name, in the target language */
typedef void (*table_writer)(emitter* e,
                             const char* name,
                             table_type type,
                             const long* values,
                             size_t count);

/**
 * @brief Pack @p d's transition table if requested and if it makes it smaller
 *
 * @return Whether @p packed was set, to release with @ref dfa_packed_destroy
 */
[[nodiscard]] bool
pack_if_smaller(const dfa* d, bool compress, dfa_packed* packed);

/**
 * @brief Write all the tables of @p d
 *
 * @param packed Packed transition table, NULL to write the full table
 */
void
emit_automaton_tables(emitter* e, const dfa* d, const dfa_packed* packed, table_writer write);

/** @} */

#endif // EMIT_H
