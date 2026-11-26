#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <format.h>

typedef struct
{
	uint8_t type;
	union
	{
		struct
		{
			const char* msg;
		} generic;

		struct
		{
			const char* msg;
			/** @brief System call */
			const char* call;
			/** @brief `errno` value */
			int errsv;
		} syscall;
	};
} error;

int
display_error(struct format_output* out, const error* error);
[[nodiscard]] error
error_generic(const char* msg);
[[nodiscard]] error
error_syscall(const char* msg, const char* call);

/** @brief Value of `EXPECTED()`, which holds nothing */
typedef struct
{
	uint8_t unused_;
} monostate;

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
// `T` if given, @ref monostate otherwise
#define TYPE(...) CAT(TYPE_, __VA_OPT__(T))(__VA_ARGS__)
#define TYPE_() monostate
#define TYPE_T(T) T
// 1 when given more than one argument, 0 otherwise
#define HAS_COMMA(...)                                                                    \
	ARG16(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, )
#define ARG16(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, ...) \
	_16

#define EXPECTED_(T)                                                                       \
	struct CAT(expected__, T)                                                       \
{                                                                                            \
	int8_t flag;                                                                             \
	union                                                                                    \
	{                                                                                        \
		T value;                                                                             \
		error error;                                                                   \
	};                                                                                       \
}
#define OK_() ((struct expected__monostate){ .flag = 0 })
#define OK_T(T, ...) ((struct expected__##T){ .flag = 0, .value = __VA_ARGS__ })
#define ERR_0(err) ((struct expected__monostate){ .flag = 1, .error = (err) })
#define ERR_1(T, ...) ((struct expected__##T){ .flag = 1, .error = (__VA_ARGS__) })

#define EXPECTED(...) EXPECTED_(TYPE(__VA_ARGS__))
#define EXPECTED_OK(...) CAT(OK_, __VA_OPT__(T))(__VA_ARGS__)
#define EXPECTED_ERR(...)                                                                  \
	CAT(ERR_, HAS_COMMA(__VA_ARGS__))(__VA_ARGS__)

#endif // UTIL_H
