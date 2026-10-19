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

# libzfs (plan 10): FreeBSD installs libzfs.h and libnvpair.h but not
# the OpenZFS headers they assume (sys/fs/zfs.h, libspl's Solaris
# types), so the FreeBSD layer takes them from a source tree, with the
# include set zfs_rebase's Makefile uses. Only src/fbsd gets them:
# libspl ships headers that would shadow FreeBSD's for everything else.
FREEBSD_SRC ?= /usr/src
ZFS_TOP = $(FREEBSD_SRC)/sys/contrib/openzfs
ZFS_CFLAGS = -I$(ZFS_TOP)/lib/libspl/include/os/freebsd \
	-I$(ZFS_TOP)/lib/libspl/include \
	-I$(ZFS_TOP)/include/os/freebsd -I$(ZFS_TOP)/include \
	-include $(ZFS_TOP)/include/os/freebsd/spl/sys/ccompile.h \
	-include $(FREEBSD_SRC)/sys/modules/zfs/zfs_config.h \
	-DNEED_SOLARIS_BOOLEAN -DHAVE_ISSETUGID -DHAVE_STRLCAT -DHAVE_STRLCPY
ZFS_LIBS = -lzfs -lzfs_core -lnvpair

# libucl (docs/configuration.md 2). The FreeBSD build links the
# textproc/libucl port: base's copy is PRIVATELIB. The portable build
# compiles libucl from the source tree's contrib/libucl into build/ucl,
# so the mac needs no package and both builds run libucl's own code
# (on the mac, FREEBSD_SRC names a FreeBSD tree). UCL picks one by a
# computed macro name, so the file stays POSIX make. Only parse.o gets
# UCL_INC: the parse is the one module that knows the format. libucl
# is foreign code, compiled with its own flags (gnu99, as base builds
# it) and never with -Werror.
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

# Which flavor build/ holds: the two share no objects, and asking for
# the other empties build/ first.
FLAVOR = portable
BUILD = build

# The language alone needs no ZFS, which is what lets docshow run the
# documentation's examples.
LANG_OBJS = $(BUILD)/err.o $(BUILD)/mem.o $(BUILD)/tree.o \
	$(BUILD)/parse.o $(BUILD)/root.o $(BUILD)/syntax.o \
	$(BUILD)/schema.o $(BUILD)/site.o $(BUILD)/cascade.o $(BUILD)/show.o
CORE_OBJS = $(LANG_OBJS) $(BUILD)/spaces.o $(BUILD)/wire.o \
	$(BUILD)/daemon.o $(BUILD)/command.o $(BUILD)/foundation.o
# src/core/zfs.h has two implementations, picked at the link: the
# FreeBSD layer over libzfs, and tests/zfake.c for the tests. The mac
# has no ZFS to link a program against, so the portable flavor
# compiles the program's objects and links only the tests.
ZFS_OBJS = $(BUILD)/fbsd_zfs.o
TEST_OBJS = $(BUILD)/zfake.o $(BUILD)/zcells.o
SHTETL_OBJS = $(BUILD)/main.o $(CORE_OBJS)
TESTS = check_mem check_zfs check_spaces check_daemon check_foundation \
	check_parse check_schema check_site check_cascade check_show

ALL_portable = $(SHTETL_OBJS)
ALL_freebsd = shtetl $(BUILD)/box_zcheck

all: flavor $(ALL_$(FLAVOR))

flavor:
	@mkdir -p $(BUILD)/ucl; \
	if [ "$$(cat $(BUILD)/.flavor 2>/dev/null)" != "$(FLAVOR)" ]; then \
	    rm -f $(BUILD)/*.o $(BUILD)/ucl/*.o $(BUILD)/check_* \
		$(BUILD)/box_* shtetl; \
	    echo "$(FLAVOR)" > $(BUILD)/.flavor; \
	fi

shtetl: $(SHTETL_OBJS) $(ZFS_OBJS) $(UCL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SHTETL_OBJS) $(ZFS_OBJS) $(UCL_OBJS) \
	    $(LDFLAGS) $(ZFS_LIBS) $(UCL_LIBS)

# tests/box/run.sh's program: family Z against libzfs.
$(BUILD)/box_zcheck: tests/box/zcheck.c $(BUILD)/zcells.o $(CORE_OBJS) \
	$(ZFS_OBJS) $(UCL_OBJS)
	$(CC) $(CFLAGS) -Itests -o $@ tests/box/zcheck.c $(BUILD)/zcells.o \
	    $(CORE_OBJS) $(ZFS_OBJS) $(UCL_OBJS) $(LDFLAGS) $(ZFS_LIBS) \
	    $(UCL_LIBS)

freebsd:
	$(MAKE) FLAVOR=freebsd UCL=port all

check-freebsd:
	$(MAKE) FLAVOR=freebsd UCL=port check

$(BUILD)/main.o: src/main.c src/core/daemon.h src/core/err.h \
	src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/main.c
$(BUILD)/wire.o: src/core/wire.c src/core/wire.h src/core/mem.h
	$(CC) $(CFLAGS) -c -o $@ src/core/wire.c
$(BUILD)/daemon.o: src/core/daemon.c src/core/daemon.h src/core/err.h \
	src/core/foundation.h src/core/mem.h src/core/spaces.h \
	src/core/wire.h src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/core/daemon.c
$(BUILD)/foundation.o: src/core/foundation.c src/core/foundation.h \
	src/core/err.h src/core/mem.h src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/core/foundation.c
$(BUILD)/command.o: src/core/command.c src/core/daemon.h \
	src/core/err.h src/core/wire.h
	$(CC) $(CFLAGS) -c -o $@ src/core/command.c
$(BUILD)/err.o: src/core/err.c src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ src/core/err.c
$(BUILD)/mem.o: src/core/mem.c src/core/mem.h
	$(CC) $(CFLAGS) -c -o $@ src/core/mem.c
$(BUILD)/spaces.o: src/core/spaces.c src/core/spaces.h src/core/err.h \
	src/core/mem.h src/core/zfs.h
	$(CC) $(CFLAGS) -c -o $@ src/core/spaces.c
$(BUILD)/fbsd_zfs.o: src/fbsd/zfs.c src/core/zfs.h src/core/err.h \
	src/core/mem.h
	$(CC) $(CFLAGS) $(ZFS_CFLAGS) -c -o $@ src/fbsd/zfs.c
$(BUILD)/zfake.o: tests/zfake.c tests/zfake.h src/core/zfs.h \
	src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ tests/zfake.c
$(BUILD)/zcells.o: tests/zcells.c tests/zcells.h src/core/zfs.h \
	src/core/err.h
	$(CC) $(CFLAGS) -c -o $@ tests/zcells.c
$(BUILD)/tree.o: src/core/tree.c src/core/tree.h src/core/mem.h
	$(CC) $(CFLAGS) -c -o $@ src/core/tree.c
$(BUILD)/parse.o: src/core/parse.c src/core/parse.h src/core/err.h \
	src/core/mem.h src/core/tree.h
	$(CC) $(CFLAGS) $(UCL_INC) -c -o $@ src/core/parse.c
$(BUILD)/root.o: src/core/root.c src/core/root.h src/core/err.h \
	src/core/mem.h src/core/parse.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/root.c
$(BUILD)/syntax.o: src/core/syntax.c src/core/syntax.h
	$(CC) $(CFLAGS) -c -o $@ src/core/syntax.c
$(BUILD)/schema.o: src/core/schema.c src/core/schema.h src/core/err.h \
	src/core/site.h src/core/syntax.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/schema.c
$(BUILD)/site.o: src/core/site.c src/core/site.h src/core/err.h \
	src/core/parse.h src/core/root.h src/core/schema.h src/core/syntax.h \
	src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/site.c
$(BUILD)/cascade.o: src/core/cascade.c src/core/cascade.h src/core/err.h \
	src/core/mem.h src/core/parse.h src/core/root.h src/core/schema.h \
	src/core/site.h src/core/syntax.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/cascade.c
$(BUILD)/show.o: src/core/show.c src/core/show.h src/core/cascade.h \
	src/core/schema.h src/core/site.h src/core/tree.h
	$(CC) $(CFLAGS) -c -o $@ src/core/show.c

# libucl's objects name no source as a prerequisite, so that a missing
# tree gets UCL_CHECK's words rather than make's; make clean rebuilds
# them.
UCL_CHECK = @test -f $(UCL_SRC)/src/ucl_parser.c || { \
	echo "no libucl source in $(UCL_SRC): make FREEBSD_SRC=<a FreeBSD"; \
	echo "src tree>, or UCL=port to link the textproc/libucl port"; \
	exit 1; }
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

# make check: each tests/check_*.c linked against the core and the
# fake, and run; then the examples of docs/configuration.md
# (tools/doccheck.py; skipped, and said so, where there is no python3).
check: unit doc

unit: flavor $(CORE_OBJS) $(TEST_OBJS) $(UCL_OBJS)
	@for t in $(TESTS); do \
	    $(CC) $(CFLAGS) -Itests -o $(BUILD)/$$t tests/$$t.c \
		$(CORE_OBJS) $(TEST_OBJS) $(UCL_OBJS) $(LDFLAGS) \
		$(UCL_LIBS) || exit 1; \
	    ./$(BUILD)/$$t || { echo "FAIL $$t"; exit 1; }; \
	    echo "ok   $$t"; \
	done

$(BUILD)/docshow: tests/docshow.c $(LANG_OBJS) $(UCL_OBJS)
	$(CC) $(CFLAGS) -o $@ tests/docshow.c $(LANG_OBJS) $(UCL_OBJS) \
	    $(LDFLAGS) $(UCL_LIBS)

doc: flavor $(BUILD)/docshow
	@if command -v python3 >/dev/null 2>&1; then \
	    python3 tools/doccheck.py docs/configuration.md \
		$(BUILD)/docshow; \
	else \
	    echo "doc: no python3: docs/configuration.md not checked"; \
	fi

# The program in sbin, since the daemon runs as root, and its rc
# script; build the freebsd flavor first.
PREFIX ?= /usr/local
install:
	install -d $(DESTDIR)$(PREFIX)/sbin $(DESTDIR)$(PREFIX)/etc/rc.d
	install -m 0555 shtetl $(DESTDIR)$(PREFIX)/sbin/shtetl
	install -m 0555 etc/rc.d/shtetl $(DESTDIR)$(PREFIX)/etc/rc.d/shtetl

gate:
	sh tools/gate.sh

# The FreeBSD sources compiled for FreeBSD on a machine that is not:
# a syntax check against a FreeBSD tree's headers, not a build.
xcheck:
	sh tools/xcheck-freebsd.sh

clean:
	rm -rf $(BUILD) shtetl

.PHONY: all flavor freebsd check-freebsd check unit doc install gate xcheck \
	clean
