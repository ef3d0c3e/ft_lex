#include "error.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

enum error_type
{
	kErrorTypeGeneric,
	kErrorTypeSyscall,
};

error
error_generic(const char* msg)
{
	assert(msg != NULL);
	return (error){ .type = kErrorTypeGeneric,
	                .generic = {
		              .msg = msg,
		            } };
}

error
error_syscall(const char* msg, const char* call)
{
	assert(msg != NULL);
	assert(call != NULL);
	assert(errno != 0);
	return (error){ .type = kErrorTypeSyscall,
	                .syscall = {
		              .msg = msg,
		              .call = call,
		              .errsv = errno,
		            } };
}

int
display_error(struct format_output* out, const error* error)
{
	switch (error->type) {
		case kErrorTypeGeneric:
			return format(out, "ERROR | {}\n", error->generic.msg);
		case kErrorTypeSyscall:
			return format(out,
			              "ERROR | {} in {}(2): {}\n",
			              error->syscall.msg,
			              error->syscall.call,
			              strerror(error->syscall.errsv));
		default:
			assert(0 && "Invalid error type");
	}
}
