.DEFAULT_GOAL := all

NAME := ft_lex
CC ?= cc

EXTRA_CFLAGS ?= -ggdb
CFLAGS += -Wall -Wextra -Wconversion -pedantic -std=gnu23 $(EXTRA_CFLAGS)
IFLAGS += -I./libs
LFLAGS +=

SOURCES := $(wildcard src/*.c src/*/*.c)
OBJECTS := $(addprefix build/,$(SOURCES:.c=.o))

# POSIX lex library, linked with generated scanners: `cc lex.yy.c -L. -ll`
LIBL := libl.a
LIBL_SOURCES := $(wildcard libl/*.c)
LIBL_OBJECTS := $(addprefix build/,$(LIBL_SOURCES:.c=.o))

TEST_SOURCES := $(wildcard test/*.c)
TEST_OBJECTS := $(addprefix build/,$(TEST_SOURCES:.c=.o))


# {{{ Libraries

# ----------
# Submodules
# ----------

# Fetched when the repository was cloned without `--recursive`. A single rule fetches all of them,
# so parallel builds do not run git concurrently. Nested submodules (libformat's test framework)
# are not needed to build.
SUBMODULES := libs/format/Makefile libs/cash/include/cash.h

libs/format/Makefile:
	@echo "Fetching submodules..."
	git submodule update --init

libs/cash/include/cash.h: libs/format/Makefile

# ------
# Format
# ------

LIBFORMAT_A := libs/format/libformat.a
IFLAGS += -I./libs/format/include

LIBS := $(LIBFORMAT_A)

# ------
# cash
# ------

IFLAGS += -I./libs/cash/include

# }}}

build/%.o: %.c | $(SUBMODULES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(IFLAGS) -MMD -MP -c $< -o $@

$(NAME): $(OBJECTS) $(LIBS)
	$(CC) $(EXTRA_CFLAGS) $(OBJECTS) -o $@ $(LIBS) $(LFLAGS)

-include $(OBJECTS:.o=.d)

# One object per function, so programs can replace either `main` or `yywrap`
$(LIBL): $(LIBL_OBJECTS)
	$(AR) rcs $@ $^

-include $(LIBL_OBJECTS:.o=.d)

# {{{ Tests

TESTER := build/tester
TEST_DIR := test
# Only run tests whose name (`<suite>/<file>`) contains this, e.g. `make test FILTER=errors/`
FILTER ?=
# Flex binary to use for testing
FLEX ?= flex

$(TESTER): $(TEST_OBJECTS)
	$(CC) $(EXTRA_CFLAGS) $(TEST_OBJECTS) -o $@ $(LFLAGS)

-include $(TEST_OBJECTS:.o=.d)

.PHONY: test
# Scanners are compiled with the same flags as libl, which they are linked with (e.g. sanitizers)
test: $(NAME) $(LIBL) $(TESTER)
	FLEX="$(FLEX)" CC="$(CC)" LIBL="$(LIBL)" SCANNER_CFLAGS="$(EXTRA_CFLAGS)" \
		./$(TESTER) $(TEST_DIR) ./$(NAME) $(FILTER)

# }}}

# Build libs
$(LIBFORMAT_A): | libs/format/Makefile
	@echo "Building libformat..."
	$(MAKE) -C $(dir $(LIBFORMAT_A))

.PHONY: FORCE
FORCE:

.PHONY: all
all: $(NAME) $(LIBL)

.PHONY: clean
clean:
	$(RM) $(NAME) $(LIBL)
	$(RM) -r build/

.PHONY: lclean
lclean:
	@echo "Cleaning libformat..."
	$(MAKE) -C $(dir $(LIBFORMAT_A)) clean

.PHONY: re
re: clean all
