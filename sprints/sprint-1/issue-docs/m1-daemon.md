# m1-daemon: the daemon and the command

## What was built

- src/core/wire.[ch]: the records the command and the daemon say to
  each other: a kind byte, a big-endian length, that many bytes. The
  command sends its arguments as one record (each ended by a NUL);
  the daemon answers with standard output, standard error and the exit
  status, one record each.
- src/core/daemon.[ch]:
  - sh_daemon_listen: an flock on /var/run/shtetl.pid, which then
    holds the pid. A second daemon says "already running". Then the
    socket /var/run/shtetl.sock, anything left at its path replaced,
    mode 0600, listening.
  - sh_daemon_run: the spaces found and their skeleton made (the
    daemon does not start if they are refused); then one kqueue loop
    over the socket, each connection, SIGTERM and SIGINT, until a
    signal; then the socket is removed.
  - The verbs are a table, the usage beside it. M1's one verb is
    `status`, which prints the spaces; m1-foundation-start adds the
    actors to it.
- src/core/command.c: sh_command sends its arguments as they are and
  copies each reply record to the stream it names until the exit
  status.
- src/main.c: `shtetl daemon` runs the daemon in the foreground, its
  reports going to syslog (with LOG_PERROR, to standard error as
  well) through a line-buffered funopen stream. Every other use is
  the command.
- etc/rc.d/shtetl starts it under daemon(8), whose pidfile is shtetl's
  own; make install puts the program in sbin and the script in
  etc/rc.d.
- src/core/mem: sh_memstream, open_memstream as shtetl's allocators
  are, logged and the process ended on failure.

## Decisions

- The command decides nothing (PROPOSAL in the issue). It sends its
  arguments, and the daemon answers, the usage included. The daemon
  never writes to a person's terminal; it frames what the act wrote,
  and the command prints it. An attended roll will pass descriptors
  (m5-queue), and its picker runs as a child of the daemon on them.
- Every reply goes out through EVFILT_WRITE, never written at once:
  one path, partial writes included, which every test runs, and a
  reader that does not read holds up no one (D17).
- A connection whose first record is not a request is hung up on. It
  is not a person's command: the socket is root's alone, and nothing
  else speaks to it.
- Every descriptor the daemon holds is closed on exec. Otherwise a
  child it runs later (a build, M3) would keep the pidfile's lock
  after the daemon died, and the next start would say "already
  running".
- A socket's mode is 0777 less the umask, so the umask is 0177 for the
  bind; 077 gave 0700 (found by D15).
- No peer check (getpeereid): the socket's mode lets only root
  connect. The issue left that open; a check would be a second copy
  of the file's permission.

## Verified

- make check: check_daemon, D1 to D17, with a daemon forked over the
  fake.
- leaks(1) on a live daemon after 600 commands: none. leaks --atExit
  cannot run check_daemon itself, since it hangs on the forked child.
- make xcheck: daemon.c, command.c, wire.c and main.c compile against
  FreeBSD's headers (kqueue, flock, funopen).
- make gate clean.
- Owed on the box, by hand: D18, the rc script (tests/box/README.md).

## Gotchas

- A test that forks a daemon must kill it on every way out. A failed
  CHECK exits, and the child held the test's output open, so make
  check waited forever (the first run hung). check_daemon reaps it
  with atexit.
- macOS leaks(1) --atExit hangs on a forked child that exits, so the
  daemon is checked live from outside instead.

## Commit

The commit after 6153fae, "daemon: the loop, its lock and socket, and
the command".
