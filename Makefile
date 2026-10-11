# shtetl: the portable core by default; the freebsd target adds the
# FreeBSD layer (src/fbsd, from M2 on: planning.md 10 and 12). Plain
# POSIX make, as zfs_rebase's is: no GNU-only functions, so bmake and
# gmake both work, and object lists are explicit on purpose.

CC ?= cc
# The base flags come from the environment where it has any, and the
# ones the code needs are appended, so a port's CFLAGS and LDFLAGS
# reach the build (zfs_rebase's Makefile, review of 2026-09-11, B13).
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wcast-qual -DNDEBUG -Isrc
LDFLAGS ?=

# libucl (planning.md 5.1). The FreeBSD build links the
# textproc/libucl port: base's copy is PRIVATELIB. The portable build
# compiles libucl from a FreeBSD source tree's contrib/libucl into
# build/ucl instead, so the mac needs no package and both builds run
# libucl's own code: FREEBSD_SRC names the tree, from the command
# line or the environment (on the mac, export it once). UCL picks one
# by a computed macro name, as zfs_rebase picks its picker, so the
# file stays POSIX make; a UCL that is neither src nor port expands
# to nothing and fails at the compile of parse.c.
#
# Only parse.o gets UCL_INC: the parse is one module behind a
# format-free tree (planning.md 5.1), and the include path holds it
# to that. libucl is foreign code and is compiled with its own flags
# (gnu99, as base builds it: mum.h uses asm), never with -Werror. Its
# objects name no source as a prerequisite, so that a missing tree
# gets UCL_CHECK's message rather than make's; make clean rebuilds
# them.
FREEBSD_SRC ?= /usr/src
UCL_SRC = $(FREEBSD_SRC)/contrib/libucl
LOCALBASE = /usr/local
UCL = src
UCL_CFLAGS = -O2 -g -std=gnu99 -w -I$(UCL_SRC)/include -I$(UCL_SRC)/src \
	-I$(UCL_SRC)/uthash -I$(UCL_SRC)/klib
UCL_OBJS_src = $(BUILD)/ucl/ucl_emitter_streamline.o \
	$(BUILD)/ucl/ucl_emitter_utils.o $(BUILD)/ucl/ucl_emitter.o \
	$(BUILD)/ucl/ucl_hash.o $(BUILD)/ucl/ucl_msgpack.o \
	$(BUILD)/ucl/ucl_parser.o $(BUILD)/ucl/ucl_schema.o \
	$(BUILD)/ucl/ucl_sexp.o $(BUILD)/ucl/ucl_util.o
UCL_OBJS_port =
UCL_OBJS = $(UCL_OBJS_$(UCL))
UCL_INC_src = -I$(UCL_SRC)/include
UCL_INC_port = -I$(LOCALBASE)/include
UCL_INC = $(UCL_INC_$(UCL))
UCL_LIBS_src = -lm
UCL_LIBS_port = -L$(LOCALBASE)/lib -lucl
UCL_LIBS = $(UCL_LIBS_$(UCL))
UCL_CHECK = @test -f $(UCL_SRC)/src/ucl_parser.c || { \
	echo "no libucl source in $(UCL_SRC): make FREEBSD_SRC=<a FreeBSD"; \
	echo "src tree>, or UCL=port to link the textproc/libucl port"; \
	exit 1; }

# Which flavor build/ holds, as in zfs_rebase: the two do not share
# objects, and asking for the other empties build/ first.
FLAVOR = portable
BUILD = build

CORE_OBJS = $(BUILD)/err.o $(BUILD)/tree.o $(BUILD)/parse.o \
	$(BUILD)/root.o $(BUILD)/syntax.o $(BUILD)/schema.o $(BUILD)/site.o \
	$(BUILD)/cascade.o $(BUILD)/show.o
SHTETL_OBJS = $(BUILD)/main.o $(CORE_OBJS) $(UCL_OBJS)
TESTS = check_parse check_schema check_site check_cascade check_show

all: build shtetl

build: flavor
	@mkdir -p $(BUILD)/ucl

flavor:
	@mkdir -p $(BUILD)/ucl; \
	if [ "$$(cat $(BUILD)/.flavor 2>/dev/null)" != "$(FLAVOR)" ]; then \
	    rm -f $(BUILD)/*.o $(BUILD)/ucl/*.o shtetl; \
	    echo "$(FLAVOR)" > $(BUILD)/.flavor; \
	fi

shtetl: build $(SHTETL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SHTETL_OBJS) $(LDFLAGS) $(UCL_LIBS)

freebsd:
	$(MAKE) FLAVOR=freebsd UCL=port CFLAGS="$(CFLAGS) -DSH_FREEBSD" all

check-freebsd:
	$(MAKE) FLAVOR=freebsd UCL=port CFLAGS="$(CFLAGS) -DSH_FREEBSD" check

$(BUILD)/main.o: src/main.c src/core/cascade.h src/core/err.h \
	src/core/root.h src/core/schema.h src/core/show.h src/core/site.h \
	src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/main.c
$(BUILD)/err.o: src/core/err.c src/core/err.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/err.c
$(BUILD)/tree.o: src/core/tree.c src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/tree.c
$(BUILD)/parse.o: src/core/parse.c src/core/parse.h src/core/err.h \
	src/core/tree.h
	$(CC) $(CFLAGS) $(UCL_INC) -c -o $@ src/core/parse.c
$(BUILD)/root.o: src/core/root.c src/core/root.h src/core/err.h \
	src/core/parse.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/root.c
$(BUILD)/syntax.o: src/core/syntax.c src/core/syntax.h
	$(CC) $(CFLAGS) -c -o $@ src/core/syntax.c
$(BUILD)/schema.o: src/core/schema.c src/core/schema.h src/core/err.h \
	src/core/syntax.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/schema.c
$(BUILD)/site.o: src/core/site.c src/core/site.h src/core/err.h \
	src/core/parse.h src/core/root.h src/core/schema.h src/core/syntax.h \
	src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/site.c
$(BUILD)/cascade.o: src/core/cascade.c src/core/cascade.h src/core/err.h \
	src/core/parse.h src/core/root.h src/core/schema.h src/core/syntax.h \
	src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/cascade.c
$(BUILD)/show.o: src/core/show.c src/core/show.h src/core/cascade.h \
	src/core/schema.h src/core/site.h src/core/err.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/show.c

$(BUILD)/ucl/ucl_emitter_streamline.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_emitter_streamline.c
$(BUILD)/ucl/ucl_emitter_utils.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_emitter_utils.c
$(BUILD)/ucl/ucl_emitter.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_emitter.c
$(BUILD)/ucl/ucl_hash.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_hash.c
$(BUILD)/ucl/ucl_msgpack.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_msgpack.c
$(BUILD)/ucl/ucl_parser.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_parser.c
$(BUILD)/ucl/ucl_schema.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_schema.c
$(BUILD)/ucl/ucl_sexp.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_sexp.c
$(BUILD)/ucl/ucl_util.o:
	$(UCL_CHECK)
	$(CC) $(UCL_CFLAGS) -c -o $@ $(UCL_SRC)/src/ucl_util.c

# make check: the unit tests (tests/check_*.c, each linked against
# the core and run), then the command line end to end
# (tests/run-cli.sh). Everything here runs on the mac.
check: unit cli

unit: build $(CORE_OBJS) $(UCL_OBJS)
	@for t in $(TESTS); do \
	    $(CC) $(CFLAGS) -o $(BUILD)/$$t tests/$$t.c $(CORE_OBJS) \
		$(UCL_OBJS) $(LDFLAGS) $(UCL_LIBS) || exit 1; \
	    ./$(BUILD)/$$t || { echo "FAIL $$t"; exit 1; }; \
	    echo "ok   $$t"; \
	done

cli: shtetl
	sh tests/run-cli.sh

gate:
	sh tools/gate.sh

clean:
	rm -rf $(BUILD) shtetl

.PHONY: all build flavor freebsd check check-freebsd unit cli gate clean
