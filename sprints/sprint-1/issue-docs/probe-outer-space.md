# probe-outer-space

## What was built

probes/P4-outer-space.sh, cells P4.1 - P4.8, a vm probe: the outer
space protocol ruled 2026-10-15 with an rc script, sent with the
root, that does a switch's work when / is on the memory pool, and
three runs across `reboot -r` and two reboots.

## Decisions

- The person runs `reboot -r`, not the probe, so the first run's log
  is committed and pushed before the reroot ends everything.
- The memory disk is sized from the root snapshot's referenced bytes,
  twice that rounded up to a GiB (ruled 2026-10-15, Q-AG: sizes
  computed, not settings), and labelled s2zram so it is found again.
- The memory pool is made with cachefile=none, so no later boot of
  the environment that made it looks for a pool that cannot be there.
- Outer space reports through user properties on <pool>/s2os, on
  the disk pool, since the memory root is gone after the real reboot:
  the state lives in ZFS here too.
- The first run takes away whatever an earlier first run left before
  it sets up again. So a plain reboot where reboot -r was meant, or a
  second run before the reroot, is only stage 1 again and not a case
  of its own.
- What the probe cannot record: if the disk pool were not reachable
  in outer space, the rc script's writes fail and the machine reboots
  into the unchanged bootfs; the console is the witness. The README
  says so.

## Verified

sh -n, dash -n; the rc script it writes rendered and checked with
sh -n. A VM run is owed, after P3.

## Commit

720760e
