# probe-live-link

## What was built

probes/P2-live-link.sh, cells P2.1 - P2.7: two versions, each a
dataset with /rescue/sh as bin/sh and its name in /which; a jail made
through the symlink /containers/H; the link moved by `mv -h` of a new
link over it (one rename); what the running jail sees and writes; a
restart; devfs mounted and unmounted.

## Decisions

- From the sources, before the probe: jail(8) refuses a symlink only
  below the jail's path (usr.sbin/jail/command.c check_path), so the
  path may be the link itself; mount(8) resolves its mount point with
  realpath (sbin/mount/mount.c), so devfs lands under the version's
  own path; and at removal jail(8) checks path/dev through the link.
  So a link moved under a running jail with devfs leaves devfs
  mounted on the old version. P2.7 records it as an answer, and it is
  why the plan's switch stops the house before it moves the link.
- The expected paths are realpath'd, since the kernel keeps a jail's
  global path (kern_jail.c vn_path_to_global_path).

## Verified

sh -n, dash -n. Box run owed.

## Commit

d6d8a22
