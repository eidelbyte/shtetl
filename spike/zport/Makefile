# spike/zport: the spike's ZFS port, built on FreeBSD (the box) into
# ../roll/priv/zport, where the release takes it. The OpenZFS include
# set is main's (its Makefile, ZFS_CFLAGS): FreeBSD installs libzfs.h
# but not the headers it assumes. ei comes with Erlang.
FREEBSD_SRC ?= /usr/src
ZFS_TOP = $(FREEBSD_SRC)/sys/contrib/openzfs
ERL_ROOT != erl -noshell -eval 'io:format("~s", [code:root_dir()]), halt().'

CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wcast-qual
ZFS_CFLAGS = -I$(ZFS_TOP)/lib/libspl/include/os/freebsd \
	-I$(ZFS_TOP)/lib/libspl/include \
	-I$(ZFS_TOP)/include/os/freebsd -I$(ZFS_TOP)/include \
	-include $(ZFS_TOP)/include/os/freebsd/spl/sys/ccompile.h \
	-include $(FREEBSD_SRC)/sys/modules/zfs/zfs_config.h \
	-DNEED_SOLARIS_BOOLEAN -DHAVE_ISSETUGID -DHAVE_STRLCAT -DHAVE_STRLCPY

../roll/priv/zport: zport.c
	mkdir -p ../roll/priv
	$(CC) $(CFLAGS) $(ZFS_CFLAGS) -I$(ERL_ROOT)/usr/include -o $@ \
	    zport.c -L$(ERL_ROOT)/usr/lib -lei -lzfs -lzfs_core -lnvpair \
	    -lpthread
