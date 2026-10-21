# probe-boot-envs

## What was built

probes/P3-boot-envs.sh, cells P3.1 - P3.7, a vm probe: two versions
under <pool>/s2sys, each a clone of the environment running now with
var and usr/local datasets of its own (filled from that environment's
snapshot and given a marker), switched by bootfs and back, over four
runs and three reboots.

## Decisions

- From the sources: rc.d/zfsbe REQUIREs mountcritlocal and rc.d/zfs
  REQUIREs zfsbe, so the booted environment's children are mounted
  before zfs mount -a. zfsbe mounts every child of the booted root
  that is not canmount=off; so a version's children are
  canmount=noauto, and zfs mount -a never takes another version's.
  The plan's 7.4 said this of the roots only; the children need it
  as much.
- Each run finds its stage from ZFS: which environment is booted,
  whether s2sys exists, s2probe:home and s2probe:v1/v2 on it. No
  state file, as for shtetl itself.
- The checkout must be outside the boot environment (outside_be in
  lib.sh): a stage's results committed inside v1 would not be there
  when v2 boots.
- The loader's boot environment menu cannot be read by a script;
  P3.6 is the person's to report.
- The children are filled with tar from the environment's snapshot
  through `mount -t zfs` on a scratch mount point, as rc.d/zfsbe
  mounts them, since their mountpoint property is the live /var.

## Verified

sh -n, dash -n. A VM run is owed (FreeBSD 15.1, root on ZFS).

## Commit

3b62deb

## VM run 1

shtetlvm, FreeBSD's 15.1-RELEASE ZFS VM image under bhyve on KIDON
(probes/vm.sh), 2026-10-21, at 3ae9d48 (results eb1e88b): 15 of 15, in
three runs after the setup's (whose summary was not kept: it checks
nothing). A first try at stage 2 after reboot -r stopped, rightly, on
the environment it started from, bootfs being the loader's (f551fb7
says so in the probe's words now); that run's results were removed in
the VM before they were collected. P3.4: the image's zroot/var/audit,
crash, log, mail and tmp mount below each version's own /var, shared by
every version. P3.6 is the author's to say: tested once said.
