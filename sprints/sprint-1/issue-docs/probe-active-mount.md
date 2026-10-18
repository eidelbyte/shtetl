# probe-active-mount

Was probe-live-link (d6d8a22): a symlink naming a container's live
version. Ruled 2026-10-18 (plan 18, answer 6): only one version of a
house is mounted, the active one, and a roll swaps the mounted
dataset. The probe was rewritten for that.

## What was built

probes/P2-active-mount.sh, cells P2.1 - P2.7: two versions of a
container, each a root and a var child, every dataset canmount=noauto;
the active one's root given the mountpoint /containers/H; a jail with
devfs on it; the next one mounted at its own path beside it, as during
a roll; the swap with the jail stopped; the jail started again.

## Decisions

- canmount=noauto on every dataset of every version (canmount does not
  inherit, main's P7), so nothing mounts a version but shtetl. P2.1
  shows it by exporting and importing the probe pool, as a boot would,
  not with zfs mount -a, which would reach every pool on the box.
- The active version is at /containers/<house> by its root's
  mountpoint, the children following; the swap puts the old one's
  mountpoint back (zfs inherit) and sets the new one's. P2.2 and P2.5
  record whether libzfs mounts or moves a dataset when its mountpoint
  is set (main's F11 found it mounts canmount=on ones), and the probe
  mounts what is left itself.
- During a roll the new version is mounted at its own path beside the
  active one: zfs_rebase reads every side at its mountpoint, so the
  two cannot share one path while the replay runs (plan 7.1). P2.4
  checks both are up at their paths.
- P2.7 records what ZFS does when a second version is mounted over the
  active one's path: whether keeping one mounted is shtetl's to do.
- The probe pool has an altroot, so mountpoints are set relative to
  it (/containers/H) and read back with it.
- The plan's proposal that the active version is the newest complete
  one, with no property marking it, rests on this probe and on 7.2's
  order (the update snapshot last).

## Verified

sh -n, dash -n. Box run owed.

## Commits

d6d8a22 (the symlink probe), 5011677 (rewritten)

## Box run 1

KIDON, 2026-10-19, at 68bc7f8 (results 1c23d85).
P2.1 - P2.4 ok: canmount=noauto versions stay unmounted through an
export and import; setting a noauto dataset's mountpoint does not
mount it (P2.2's answer: no); two versions mount side by side at their
own paths.

P2.5 failed: devfs still mounted at H/dev after jail -r, and v1 could
not be unmounted under it, which ended the probe. The probe's fault,
not the model's: the jail was made from parameters on the command
line, and jail -r loads the configuration file and undoes mount.devfs
only for a jail it names (usr.sbin/jail/jail.c; -r takes no
param=value at all). Fixed in b7c72e3: the probe writes the jail's
block to a file and uses jail -f for -c and -r, which is how shtetl
will make its jails, from /etc/jail.conf.d. Plan 7.3 now says so.
Rerun owed.
