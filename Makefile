# shtetl: the portable build by default, for the mac and its tests;
# make freebsd is the FreeBSD build. Plain POSIX make, as zfs_rebase's
# is: no GNU-only functions, so bmake and gmake both work, and object
# lists are explicit on purpose.

CC ?= cc
# The base flags come from the environment where it has any, and the
# ones the code needs are appended, so a port's CFLAGS and LDFLAGS
# reach the build.
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wcast-qual -Isrc
LDFLAGS ?=

# Which flavor build/ holds: the two share no objects, and asking for
# the other empties build/ first.
FLAVOR = portable
BUILD = build

CORE_OBJS = $(BUILD)/err.o $(BUILD)/mem.o
SHTETL_OBJS = $(BUILD)/main.o $(CORE_OBJS)
TESTS = check_mem

all: flavor shtetl

flavor:
	@mkdir -p $(BUILD); \
	if [ "$$(cat $(BUILD)/.flavor 2>/dev/null)" != "$(FLAVOR)" ]; then \
	    rm -f $(BUILD)/*.o $(BUILD)/check_* shtetl; \
	    echo "$(FLAVOR)" > $(BUILD)/.flavor; \
	fi

shtetl: $(SHTETL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SHTETL_OBJS) $(LDFLAGS)

freebsd:
	$(MAKE) FLAVOR=freebsd all

check-freebsd:
	$(MAKE) FLAVOR=freebsd check

$(BUILD)/main.o: src/main.c
	$(CC) $(CFLAGS) -c -o $@ src/main.c
$(BUILD)/err.o: src/core/err.c src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ src/core/err.c
$(BUILD)/mem.o: src/core/mem.c src/core/mem.h
	$(CC) $(CFLAGS) -c -o $@ src/core/mem.c

# make check: each tests/check_*.c linked against the core and run.
check: flavor $(CORE_OBJS)
	@for t in $(TESTS); do \
	    $(CC) $(CFLAGS) -Itests -o $(BUILD)/$$t tests/$$t.c \
		$(CORE_OBJS) $(LDFLAGS) || exit 1; \
	    ./$(BUILD)/$$t || { echo "FAIL $$t"; exit 1; }; \
	    echo "ok   $$t"; \
	done

gate:
	sh tools/gate.sh

clean:
	rm -rf $(BUILD) shtetl

.PHONY: all flavor freebsd check-freebsd check gate clean
