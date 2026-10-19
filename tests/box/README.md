# The box

shtetl's cells that need FreeBSD and real pools, run on the box:

    git pull
    make freebsd
    sudo sh tests/box/run.sh

It makes its pools on swap-backed memory disks under an altroot, as
the probes do (probes/lib.sh), and destroys them after. It will not
start while a pool of its names exists; `sudo sh tests/box/run.sh
--clean` removes what a crash left.

The log is tests/box/results/<host>-<UTC time>.log. Its first line
names the commit it ran at, and its last says "== N ok, M failed".
The script commits and pushes it as the person behind sudo; `SEND=no`
keeps it local.

What runs:

- family Z (tests/MATRIX.md): build/box_zcheck, tests/zcells.c's
  cells over libzfs, the same cells make check runs over the fake.

`make freebsd` needs a FreeBSD source tree for the OpenZFS headers,
/usr/src unless FREEBSD_SRC says otherwise. On the mac, `make xcheck`
(with FREEBSD_SRC set) compiles the FreeBSD sources against such a
tree for a syntax check before the box sees them.
