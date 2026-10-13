# Box tests

What the mac cannot run: shtetl against real ZFS. Two pools on
memory disks, imported with an altroot under `TMPDIR`, so that
`/containers` and every other mountpoint shtetl sets lands there and
not on the system's own root.

    git pull
    make freebsd
    sudo sh tests/box/run.sh

It runs `shtetl spaces`, `config show` and `bootstrap` as a person
would, checks each result with `zfs get`, and writes the run to
`tests/box/results/<host>-<UTC time>.log`. Each line is a cell of
`tests/MATRIX.md` (families D and B) and `ok` or `FAIL`. At the end
the log is committed and pushed, passed or failed, as the person who
ran sudo (or doas), with their git identity and credentials
(`tests/box/send.sh`); where the push cannot authenticate, as with an
ssh agent sudo dropped, the commit stays and the script says to `git
push`. `SEND=no` keeps the log here. `tests/box/build.sh`, a real
build of a foundation, ends the same way.

It needs root, the freebsd build of shtetl (`make freebsd`, which
links the textproc/libucl package), and for the recipes clone
(B13) git; without git that case is skipped. It refuses to start
while its pools (`shbtank`, `shbfast`) exist, or while any other pool
has a shtetl root, which discovery would find. `sudo sh
tests/box/run.sh --clean` removes what a crash left.
