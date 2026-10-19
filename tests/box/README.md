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
- family A: ./shtetl daemon started over a lake pool holding what a
  crash leaves (a version with a file written after it, a first build
  with no version, a stalled foundation), asked for its status, and
  stopped with SIGTERM. It needs a box with no shtetl space marked and
  no shtetl daemon running, and says so otherwise.

By hand, once (tests/MATRIX.md D18), the rc script:

    sudo make install
    sudo service shtetl onestart
    sudo shtetl status
    sudo service shtetl onestatus
    sudo service shtetl onestop

status prints the spaces this box has marked (none is fine); onestatus
names the pid in /var/run/shtetl.pid; after onestop both
/var/run/shtetl.sock and the process are gone. `sudo rm
/usr/local/sbin/shtetl /usr/local/etc/rc.d/shtetl` takes it away again.

`make freebsd` links the textproc/libucl port (`pkg install libucl`),
since base's libucl is private, and `make check-freebsd` runs every
unit test with it, the configuration language's against that libucl.
It needs a FreeBSD source tree for the OpenZFS headers,
/usr/src unless FREEBSD_SRC says otherwise. On the mac, `make xcheck`
(with FREEBSD_SRC set) compiles the FreeBSD sources against such a
tree for a syntax check before the box sees them.
