# spike-port

## What was built

- spike/zport/zport.c and its Makefile: the ZFS port. {packet, 4},
  terms through ei; ping, sync, exists, snapshot, clone, beam (a forked
  lzc_send into lzc_receive, as main's src/fbsd/zfs.c does it), guid,
  written, snapshots (zfs_iter_snapshots_sorted). Built on FreeBSD
  with main's OpenZFS include set, ei from code:root_dir().
- spike/roll: mix project and release. Roll.Zfs owns the port;
  Roll.Foundation is a gen_statem over a toy foundation; a
  rest_for_one supervisor over the two; Roll.port_micros/1 for the
  round trip.
- probes/S1-runtime.sh, cells S1.1 - S1.9; run.sh takes S probes and
  skips needs: elixir without mix.
- spike/README.md.

## Decisions

- No NIFs: the port is a separate process, so a crash in C cannot take
  the VM down and a long call stalls no scheduler.
- The actor keeps no sequence: where/0 reads ZFS after every step to
  choose the next. Crash hooks (roll(:clone) and so on) kill it after
  a step, once; the supervisor starts it again and it carries on.
- One roll at a time is free: steps chain as internal events, which
  gen_statem runs before waiting messages. A clause for "roll while
  rolling" would be dead code, so there is none.
- That same chaining blocks the actor during a step. Fine for a toy;
  for shtetl, plan 9 now says a long step is a watched child process.
- The toy names its snapshots r0, r1, ... : a toy's naming, not
  shtetl's; the spike is about the runtime.
- The release builds on a copy of spike/ on the throwaway pool with
  HOME there, so the box's checkout and root's home stay clean; the
  system's epmd is started first so nothing left running is on the
  pool.
- RELEASE_DISTRIBUTION=name, RELEASE_NODE=roll@127.0.0.1: rpc without
  depending on the box's hostname resolving.

## Verified

zport.c: cc -fsyntax-only -std=c99 -Wall -Wextra -Werror -Wcast-qual
on the mac against /usr/local/zfs's headers (fortify off: the SDK's
strlcat macro collides with libspl's declaration) and a stub of ei.h
with OTP's signatures, kept in the job's scratch space. The Elixir has
not been compiled: there is no Erlang on the mac. probes/S1 is sh -n
and dash -n clean. The box run (pkg install elixir; sudo sh
probes/run.sh S1) is the first build of the Elixir, and may need a
round of fixes.

## Next

spike-outer-space (S2) builds on a release that has run once, so it is
written after S1's first box run.

## Commit

fccf203
