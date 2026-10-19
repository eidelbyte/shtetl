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

# Which flavor build/ holds: the two share no objects, and asking for
# the other empties build/ first.
FLAVOR = portable
BUILD = build

CORE_OBJS = $(BUILD)/err.o $(BUILD)/mem.o $(BUILD)/spaces.o \
	$(BUILD)/wire.o $(BUILD)/daemon.o $(BUILD)/command.o \
	$(BUILD)/foundation.o
# src/core/zfs.h has two implementations, picked at the link: the
# FreeBSD layer over libzfs, and tests/zfake.c for the tests. The mac
# has no ZFS to link a program against, so the portable flavor
# compiles the program's objects and links only the tests.
ZFS_OBJS = $(BUILD)/fbsd_zfs.o
TEST_OBJS = $(BUILD)/zfake.o $(BUILD)/zcells.o
SHTETL_OBJS = $(BUILD)/main.o $(CORE_OBJS)
TESTS = check_mem check_zfs check_spaces check_daemon check_foundation

ALL_portable = $(SHTETL_OBJS)
ALL_freebsd = shtetl $(BUILD)/box_zcheck

all: flavor $(ALL_$(FLAVOR))

flavor:
	@mkdir -p $(BUILD); \
	if [ "$$(cat $(BUILD)/.flavor 2>/dev/null)" != "$(FLAVOR)" ]; then \
	    rm -f $(BUILD)/*.o $(BUILD)/check_* $(BUILD)/box_* shtetl; \
	    echo "$(FLAVOR)" > $(BUILD)/.flavor; \
	fi

shtetl: $(SHTETL_OBJS) $(ZFS_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SHTETL_OBJS) $(ZFS_OBJS) $(LDFLAGS) \
	    $(ZFS_LIBS)

# tests/box/run.sh's program: family Z against libzfs.
$(BUILD)/box_zcheck: tests/box/zcheck.c $(BUILD)/zcells.o $(CORE_OBJS) \
	$(ZFS_OBJS)
	$(CC) $(CFLAGS) -Itests -o $@ tests/box/zcheck.c $(BUILD)/zcells.o \
	    $(CORE_OBJS) $(ZFS_OBJS) $(LDFLAGS) $(ZFS_LIBS)

freebsd:
	$(MAKE) FLAVOR=freebsd all

check-freebsd:
	$(MAKE) FLAVOR=freebsd check

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

# make check: each tests/check_*.c linked against the core and the
# fake, and run.
check: flavor $(CORE_OBJS) $(TEST_OBJS)
	@for t in $(TESTS); do \
	    $(CC) $(CFLAGS) -Itests -o $(BUILD)/$$t tests/$$t.c \
		$(CORE_OBJS) $(TEST_OBJS) $(LDFLAGS) || exit 1; \
	    ./$(BUILD)/$$t || { echo "FAIL $$t"; exit 1; }; \
	    echo "ok   $$t"; \
	done

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

.PHONY: all flavor freebsd check-freebsd check install gate xcheck clean
