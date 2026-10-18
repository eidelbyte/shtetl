# Sprint 1: open questions

The first fourteen were answered on 2026-10-18 and are ruled in
implementation-plan.md section 18. These four came out of the
answers. Each stands alone: what it is about, the choice, and a
recommendation.

## 1. How does the person answer a house's conflict?

When a house's roll meets a conflict that does not merge three ways
clean, the house stalls: the new version is destroyed, the house is
started again on its last working version, and it waits (ruled). The
conflict is still there for the next roll to meet, and someone has to
answer it.

The choice: the person restarts the roll with `shtetl roll H`, the
house is stopped and replayed again, and when the roll reaches the
conflict it waits for that person, opening zfs_rebase's picker in
their terminal, with the house down while they answer; or the stalled
roll's conflict is kept open while the house runs, and answered at
leisure, which then needs the house's newer saves caught up at the
next stop, the switch you ruled out.

Recommendation: the first. An automatic roll never waits on a person,
it stalls; a roll the person starts by hand is attended, so it waits
for them at a conflict. One sequence, and only who started it differs.

## 2. Can a house hold a foundation version that has arrived?

Your first message said updates "can be scheduled or manual"; your
answers say houses roll automatically through every foundation version
that reaches zrun. A system's roll reboots it, so a system on a
foundation that a schedule rebuilds at 03:00 reboots soon after.

The choice: the cascade is always immediate, and the schedule lives on
the foundation alone (it builds by its schedule or by hand, and its
houses follow at once); or a house may also say when it takes an
arrival (at once, in a window, or by hand only), a second gate.

Recommendation: the foundation alone, which keeps two triggers and no
more. A system that must not reboot unasked sits on a foundation that
rolls only by hand.

## 3. Does the daemon abort when memory runs out?

Most of the checks that make C verbose are for allocation failing:
every malloc, strdup and nvlist_alloc and the cleanup behind each.
The daemon is crash-only (plan ground rule 1): started again by
daemon(8), every actor reads where it stands from ZFS.

The choice: allocation failure aborts the daemon, through a few
allocators of our own that never return NULL, as libnvpair's fnvlist
functions already do; or every allocation is checked and unwound at
its call.

Recommendation: abort. A daemon that cannot allocate cannot do its
work anyway, the restart is the recovery the design already has, and
every function loses its most common error path. Errors the system can
really give, a refused zfs call or a failed child, are still checked
where they happen.

## 4. Where do several recipe repositories live in zconfig?

zconfig holds the recipes of several repositories (ruled), for
instance the upstream shtetl-recipes and the site's own. On main it
was one repository whose clone was zconfig itself.

The choice: each repository cloned into its own directory of zconfig,
named for its remote, beside shtetl.conf at zconfig's top; or the
repositories merged into one tree.

Recommendation: a directory each. A recipe is found by its name across
them, and one name in two repositories is refused, as a redefinition
is in the language; shtetl.conf, the remotes in it, stays the site's
own at the top.
