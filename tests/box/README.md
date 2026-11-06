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
the containers s2web and s2app, and writes zconfig anew, the run keeping 1
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
running jail, and removes the jail from its block. Then it makes
s2app, with no package of its own, on the same version, pkg-rolls box
and lets the beam carry the new version to the run, and asks shtetl
roll s2app twice: with a command piece that fails in the chroot, which
must leave s2app stalled on its old version, the new one destroyed and
its devfs released; then without it, which must carry what the
container wrote onto the new version by the real zfs_rebase, installed
where the daemon's PATH finds it. Then it pkg-rolls box once more,
and s2app must roll onto the new version by itself, the daemon asking
it when the beam's convergence ends. Its rolls are asked from no
terminal, so none of them waits on a person; an attended roll's picker
is checked by hand. Real jails run on the box for that
time; they and their blocks in /etc/jail.conf.d are taken away before
the pool is exported, on every way out. The log is
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

## The dev VM: the box tests driven over ssh

tests/box/devvm.sh makes shtetl-dev, a VM on the box for driving these
tests from another machine over ssh without a person at the box:
FreeBSD's own ZFS VM image of the box's release under bhyve, as
probes/vm.sh makes its scratch VM, but bridged to the box's network,
with sshd taking keys only and one user, claude, with sudo and no
password. Inside it root is the VM's own, with pools, memory disks and
jails of its own, which is what run.sh and build.sh need; the box's
pools and jails are out of its reach. 16G of memory, 4 CPUs and a
160G sparse disk by default (build.sh's pool file is 80G of it).

On the machine that will drive it, a key of its own:

    ssh-keygen -t ed25519 -N '' -C claude@shtetl-dev -f ~/.ssh/shtetl-dev
    scp ~/.ssh/shtetl-dev.pub box:/tmp/

On the box, once, the UEFI firmware, then the VM made and booted; a
free address on the box's network outside its DHCP range, or DEV_ADDR
left out for DHCP:

    sudo pkg install bhyve-firmware
    sudo env DEV_ADDR=192.168.1.50/24 sh tests/box/devvm.sh make /tmp/shtetl-dev.pub
    sudo sh tests/box/devvm.sh run

run leaves its console on the terminal. The first boot grows the disk
to its size; then root logs in there with no password (only there:
sshd takes neither root nor passwords), `ifconfig vtnet0` shows the
address where DHCP gave it, and ~. leaves the console with the VM
running on. From the driving machine:

    ssh -i ~/.ssh/shtetl-dev claude@192.168.1.50 sudo zpool list

lists the VM's own zroot. What the tests need is set up from there over
ssh: /usr/src from the release's src.txz (make freebsd's OpenZFS
headers), the shtetl and zfs_rebase_tool checkouts pushed to it, and
zfs_rebase built and installed with its picker; run.sh and build.sh
then run in it with SEND=no, their logs copied back and committed from
the driving machine, since the VM holds no key to push with. tmux is
there for the attended roll's picker. A wireless interface cannot be
bridged: NIC names another. `sudo sh tests/box/devvm.sh stop` powers
it off; `destroy`, then `make`, makes it anew.
