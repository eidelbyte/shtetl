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

# libzfs (planning.md 10): FreeBSD installs libzfs.h and libnvpair.h
# but not the OpenZFS headers they assume (sys/fs/zfs.h, libspl's
# Solaris types), so the freebsd flavor takes them from the source
# tree, with the include set zfs_rebase's Makefile uses. Only the ZFS
# layer, src/fbsd/zfs.c, gets them: libspl ships headers that would
# shadow FreeBSD's for everything else.
ZFS_TOP = $(FREEBSD_SRC)/sys/contrib/openzfs
ZFS_CFLAGS = -I$(ZFS_TOP)/lib/libspl/include/os/freebsd \
	-I$(ZFS_TOP)/lib/libspl/include \
	-I$(ZFS_TOP)/include/os/freebsd -I$(ZFS_TOP)/include \
	-include $(ZFS_TOP)/include/os/freebsd/spl/sys/ccompile.h \
	-include $(FREEBSD_SRC)/sys/modules/zfs/zfs_config.h \
	-DNEED_SOLARIS_BOOLEAN -DHAVE_ISSETUGID -DHAVE_STRLCAT -DHAVE_STRLCPY
ZFS_LIBS = -lzfs -lzfs_core -lnvpair

# Which flavor build/ holds, as in zfs_rebase: the two do not share
# objects, and asking for the other empties build/ first.
FLAVOR = portable
BUILD = build

CORE_OBJS = $(BUILD)/err.o $(BUILD)/tree.o $(BUILD)/parse.o \
	$(BUILD)/root.o $(BUILD)/syntax.o $(BUILD)/schema.o $(BUILD)/site.o \
	$(BUILD)/cascade.o $(BUILD)/show.o $(BUILD)/run.o $(BUILD)/spaces.o \
	$(BUILD)/boot.o $(BUILD)/version.o $(BUILD)/build.o $(BUILD)/patch.o \
	$(BUILD)/beam.o $(BUILD)/save.o $(BUILD)/edit.o $(BUILD)/zwalk.o \
	$(BUILD)/create.o $(BUILD)/rebase.o $(BUILD)/jail.o \
	$(BUILD)/update.o $(BUILD)/cutover.o $(BUILD)/cfgsnap.o
# src/core/zfs.h has three implementations, picked at the link: the
# portable shtetl gets src/core/zstub.c, which refuses every call; the
# FreeBSD one gets src/fbsd/zfs.c, over libzfs; the unit tests get
# tests/zfake.c, a pool in memory.
ZFS_OBJS_portable = $(BUILD)/zstub.o
ZFS_OBJS_freebsd = $(BUILD)/fbsd_zfs.o
ZFS_OBJS = $(ZFS_OBJS_$(FLAVOR))
TEST_OBJS = $(BUILD)/zfake.o $(BUILD)/rbfake.o
# tests/box/zcheck.c's objects besides the ZFS it checks. make check
# links it against the stub, so a missing one shows on the mac, and
# runs its cells over the fake (tests/box/zcheck-fake.c).
BOX_OBJS = $(BUILD)/err.o $(BUILD)/tree.o
SHTETL_OBJS = $(BUILD)/main.o $(CORE_OBJS) $(ZFS_OBJS) $(UCL_OBJS)
TESTS = check_parse check_schema check_site check_cascade check_show \
	check_run check_spaces check_boot check_version check_zfs \
	check_build check_patch check_beam check_save check_create \
	check_rebase check_update check_cfgsnap

all: build shtetl

build: flavor
	@mkdir -p $(BUILD)/ucl

flavor:
	@mkdir -p $(BUILD)/ucl; \
	if [ "$$(cat $(BUILD)/.flavor 2>/dev/null)" != "$(FLAVOR)" ]; then \
	    rm -f $(BUILD)/*.o $(BUILD)/ucl/*.o shtetl $(BUILD)/box_zcheck; \
	    echo "$(FLAVOR)" > $(BUILD)/.flavor; \
	fi

shtetl: build $(SHTETL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SHTETL_OBJS) $(LDFLAGS) $(UCL_LIBS)

# tests/box/run.sh's programs come with it: box_zcheck, the Z cells
# against libzfs.
freebsd:
	$(MAKE) FLAVOR=freebsd UCL=port CFLAGS="$(CFLAGS) -DSH_FREEBSD" \
	    LDFLAGS="$(LDFLAGS) $(ZFS_LIBS)" all $(BUILD)/box_zcheck

check-freebsd:
	$(MAKE) FLAVOR=freebsd UCL=port CFLAGS="$(CFLAGS) -DSH_FREEBSD" \
	    LDFLAGS="$(LDFLAGS) $(ZFS_LIBS)" check

$(BUILD)/main.o: src/main.c src/core/beam.h src/core/boot.h \
	src/core/create.h src/core/edit.h src/core/save.h \
	src/core/build.h src/core/cascade.h src/core/err.h src/core/patch.h \
	src/core/root.h src/core/schema.h src/core/show.h src/core/site.h \
	src/core/spaces.h src/core/tree.h src/core/zfs.h
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
$(BUILD)/run.o: src/core/run.c src/core/run.h src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ src/core/run.c
$(BUILD)/spaces.o: src/core/spaces.c src/core/spaces.h src/core/err.h \
	src/core/tree.h src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/core/spaces.c
$(BUILD)/boot.o: src/core/boot.c src/core/boot.h src/core/err.h \
	src/core/run.h src/core/site.h src/core/spaces.h src/core/syntax.h \
	src/core/tree.h src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/core/boot.c
$(BUILD)/build.o: src/core/build.c src/core/build.h src/core/cascade.h \
	src/core/cfgsnap.h src/core/err.h src/core/spaces.h \
	src/core/syntax.h src/core/tree.h src/core/version.h src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/core/build.c
$(BUILD)/version.o: src/core/version.c src/core/version.h \
	src/core/err.h src/core/syntax.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/version.c
$(BUILD)/beam.o: src/core/beam.c src/core/beam.h src/core/build.h \
	src/core/err.h src/core/spaces.h src/core/zfs.h src/core/zwalk.h
	$(CC) $(CFLAGS) -c -o $@ src/core/beam.c
$(BUILD)/save.o: src/core/save.c src/core/save.h src/core/beam.h \
	src/core/err.h src/core/spaces.h src/core/syntax.h src/core/zfs.h \
	src/core/zwalk.h
	$(CC) $(CFLAGS) -c -o $@ src/core/save.c
$(BUILD)/create.o: src/core/create.c src/core/create.h src/core/beam.h \
	src/core/cfgsnap.h src/core/jail.h \
	src/core/build.h src/core/cascade.h src/core/err.h src/core/run.h \
	src/core/save.h src/core/spaces.h src/core/tree.h src/core/version.h \
	src/core/zfs.h src/core/zwalk.h
	$(CC) $(CFLAGS) -c -o $@ src/core/create.c
$(BUILD)/update.o: src/core/update.c src/core/update.h src/core/beam.h \
	src/core/cfgsnap.h src/core/build.h src/core/cascade.h \
	src/core/err.h src/core/jail.h \
	src/core/parse.h src/core/rebase.h src/core/run.h src/core/save.h \
	src/core/spaces.h src/core/tree.h src/core/version.h src/core/zfs.h \
	src/core/zwalk.h
	$(CC) $(CFLAGS) -c -o $@ src/core/update.c
$(BUILD)/cutover.o: src/core/cutover.c src/core/cutover.h \
	src/core/build.h src/core/cascade.h src/core/err.h src/core/jail.h \
	src/core/save.h src/core/spaces.h src/core/tree.h src/core/update.h \
	src/core/zfs.h src/core/zwalk.h
	$(CC) $(CFLAGS) -c -o $@ src/core/cutover.c
$(BUILD)/jail.o: src/core/jail.c src/core/jail.h src/core/build.h \
	src/core/err.h src/core/run.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/jail.c
$(BUILD)/rebase.o: src/core/rebase.c src/core/rebase.h src/core/err.h \
	src/core/run.h
	$(CC) $(CFLAGS) -c -o $@ src/core/rebase.c
$(BUILD)/cfgsnap.o: src/core/cfgsnap.c src/core/cfgsnap.h \
	src/core/err.h src/core/zfs.h src/core/zwalk.h
	$(CC) $(CFLAGS) -c -o $@ src/core/cfgsnap.c
$(BUILD)/zwalk.o: src/core/zwalk.c src/core/zwalk.h src/core/err.h \
	src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/core/zwalk.c
$(BUILD)/edit.o: src/core/edit.c src/core/edit.h src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ src/core/edit.c
$(BUILD)/patch.o: src/core/patch.c src/core/patch.h src/core/err.h \
	src/core/parse.h src/core/syntax.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/patch.c
$(BUILD)/zstub.o: src/core/zstub.c src/core/zfs.h src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ src/core/zstub.c
$(BUILD)/fbsd_zfs.o: src/fbsd/zfs.c src/core/zfs.h src/core/err.h
	$(CC) $(CFLAGS) $(ZFS_CFLAGS) -c -o $@ src/fbsd/zfs.c
$(BUILD)/box_zcheck: tests/box/zcheck.c $(BOX_OBJS) $(ZFS_OBJS)
	$(CC) $(CFLAGS) -o $@ tests/box/zcheck.c $(BOX_OBJS) $(ZFS_OBJS) \
	    $(LDFLAGS)
$(BUILD)/zfake.o: tests/zfake.c tests/zfake.h src/core/zfs.h src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ tests/zfake.c
$(BUILD)/rbfake.o: tests/rbfake.c tests/rbfake.h src/core/rebase.h \
	src/core/zfs.h src/core/err.h
	$(CC) $(CFLAGS) -Isrc -c -o $@ tests/rbfake.c
$(BUILD)/zcheck_main.o: tests/box/zcheck.c src/core/zfs.h src/core/err.h
	$(CC) $(CFLAGS) -Dmain=zcheck_main -c -o $@ tests/box/zcheck.c

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
# (tests/run-cli.sh), then the examples of docs/configuration.md
# (tools/doccheck.py; skipped, and said so, where there is no
# python3), then tests/box/run.sh's box_zcheck: linked, and its cells
# run over the fake. Everything here runs on the mac.
check: unit cli doc box

unit: build $(CORE_OBJS) $(TEST_OBJS) $(UCL_OBJS)
	@for t in $(TESTS); do \
	    $(CC) $(CFLAGS) -o $(BUILD)/$$t tests/$$t.c $(CORE_OBJS) \
		$(TEST_OBJS) $(UCL_OBJS) $(LDFLAGS) $(UCL_LIBS) || exit 1; \
	    ./$(BUILD)/$$t || { echo "FAIL $$t"; exit 1; }; \
	    echo "ok   $$t"; \
	done

cli: shtetl
	sh tests/run-cli.sh

doc: shtetl
	@if command -v python3 >/dev/null 2>&1; then \
	    python3 tools/doccheck.py docs/configuration.md ./shtetl; \
	else \
	    echo "doc: no python3: docs/configuration.md not checked"; \
	fi

box: build $(BUILD)/box_zcheck $(BUILD)/zcheck_main.o $(BOX_OBJS) \
	$(BUILD)/zfake.o
	@echo "ok   box_zcheck links"
	@$(CC) $(CFLAGS) -Itests -o $(BUILD)/zcheck_fake \
	    tests/box/zcheck-fake.c $(BUILD)/zcheck_main.o $(BOX_OBJS) \
	    $(BUILD)/zfake.o $(LDFLAGS) || exit 1; \
	out=$$(./$(BUILD)/zcheck_fake 2>&1) || { echo "$$out"; \
	    echo "FAIL zcheck over the fake"; exit 1; }; \
	echo "ok   zcheck over the fake ($$(echo "$$out" | grep -c '^ok ')" \
	    "cells)"

gate:
	sh tools/gate.sh

# The FreeBSD sources compiled for FreeBSD on a machine that is not:
# a syntax check against a FreeBSD tree's headers, not a build.
xcheck:
	sh tools/xcheck-freebsd.sh

clean:
	rm -rf $(BUILD) shtetl

.PHONY: all build flavor freebsd check check-freebsd unit cli doc box \
	gate xcheck clean
