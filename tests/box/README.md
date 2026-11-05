# The box

shtetl's cells that need FreeBSD and real pools, run on the box:

    git pull
    make freebsd
    sudo sh tests/box/run.sh

It makes its pools on swap-backed memory disks under an altroot, as
the probes do (probes/lib.sh), and destroys them after. It will not
start while a pool of its names exists; `sudo sh tests/box/run.sh
--clean` removes what a crash left.

The log is tests/box/results/<UTC time>.log. Its first line
names the commit it ran at, and its last says "== N ok, M failed".
The script commits and pushes it as the person behind sudo; `SEND=no`
keeps it local. What it sends names neither the box nor its person
(probes/README.md).

What runs:

- make check-freebsd, as the person behind sudo: every unit test on
  this libc and the textproc/libucl port, each program an ok or FAIL
  line of the log.
- family Z (tests/MATRIX.md): build/box_zcheck, tests/zcells.c's
  cells over libzfs, the same cells make check runs over the fake.
- family A: ./shtetl daemon started over a lake pool holding what a
  crash leaves (a version with a file written after it, a first build
  with no version, a stalled foundation), asked for its status, and
  stopped with SIGTERM. It needs a box with no shtetl space marked and
  no shtetl daemon running, and says so otherwise.

M3 and M4 on the box, a real foundation rolled by the daemon (family
R), beamed and pruned (B28, P24), and M5's first container, its jail,
a save and a pkg upgrade (H17, N14, N15, N17, V10):

    git pull
    make freebsd
    sudo sh tests/box/build.sh

It installs shtetl (make install) and starts it with service(8), as rc
would, on the pool s2box: a sparse file in IMGDIR (/var/tmp), imported
with an altroot beside it for the run and exported after, so it is
gone from the box between runs and run.sh never meets its spaces. The
pool keeps zbuild's checkout and the obj cache from run to run, so
only the first build takes hours; each run destroys what the last
made, the foundation box, its copy on the run, the house stand-ins and
the container s2web, and writes zconfig anew, the run keeping 1
version of a house and the lake 2. It asks shtetl roll and shtetl pkg
as a person does, waits on shtetl status, and reads what came of each
from ZFS and the head. Between the rolls it does what M5's houses
will: box pulled to the run by hand at its first version, and
versions S1 - S3 of a house stand-in cloned on box's copy, one with
8 MiB in it, whose stream to the lake it cuts; the daemon's beam
carries the rest and resumes the cut, and its prune keeps what each
space needs. After the rolls it asks shtetl create s2web --on box,
and reads the jail the daemon made: its path and devfs, jexec and
pkg -j into it; it asks shtetl save s2web and shtetl pkg s2web, which
upgrades the container's own package, jq, and reads both snapshots
and the save on the lake; it stops and starts the daemon around the
running jail, and removes the jail from its block. A real jail runs on the
box for that time; it and its block in /etc/jail.conf.d are taken
away before the pool is exported, on every way out. The log is
tests/box/results/build-<UTC time>.log, sent as run.sh's is, with the
run's lines of /var/log/daemon.log and of beam.log at its end. BRANCH,
GITURL, IMGDIR and SIZE change what it builds and where; `sudo sh
tests/box/build.sh --clean` takes the pool and its file away, the
kept build with them.

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
