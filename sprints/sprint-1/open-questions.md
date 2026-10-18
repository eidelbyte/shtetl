# Sprint 1: open questions

Asked 2026-10-18, on implementation-plan.md (draft for ruling). Each
question stands alone: what it is about, the choice, and a
recommendation. The plan's sections are named where they say more.

## 1. Where is a foundation written?

On main, a foundation is built into zlake and then beamed down to
zrun, where houses are cloned from it. Houses are written on zrun and
beamed up. So beaming runs both ways, and a house cannot roll until
its foundation's new version has come down.

The choice: keep that, or build foundations into zrun so that zrun
writes everything and zlake is only a copy that converges to it.

Recommendation: build into zrun. Beaming then runs one way, zrun to
zlake, and a house rolls as soon as its foundation has a new version.
Restore is the one beam the other way, by hand. The obj caches stay
on zlake. (Plan ground rule 3, section 8.1.)

## 2. What is the foundation part of a house version's name?

You named a house version `zrun/[Type]/<Foundation>-<House Name>`, new
for each foundation version. The foundation part has to change with
every foundation version, so it must name the version, not only the
foundation recipe.

The choice: the foundation snapshot's name without its kind
(`15.1-releng-2026-10-18T09_30_12Z-nginx`), the same with the
foundation's name in front (`generic-15.1-releng-...-nginx`), or
something shorter.

Recommendation: the snapshot's name without its kind. A house stays
on one foundation all its life, and the date to the second already
makes the name unique. shtetl never reads the name; it knows the house
from `shtetl:house` and the foundation version from `origin`.

You also wrote "now that house names are UUIDs". I read that as: the
name is unique for good, so zlake needs no archive. If you meant real
UUIDs in names or properties, say so; I would rather not, since ZFS
guids already identify every snapshot.

## 3. What are the remotes in zconfig?

You wrote that zconfig is "JUST a collection of remotes and recipes".
I can read "remotes" two ways: the places shtetl fetches from or
sends to (the FreeBSD src repository, patch repositories, the recipes'
own git repository, and the lake when it is on another host), or
several git remotes that recipes come from (the upstream
shtetl-recipes and the site's own).

Recommendation: the first. A remote is a named place, declared once
in shtetl.conf and used by name in recipes and settings. Tell me if
you meant the second.

## 4. Is zconfig still a ZFS dataset?

zconfig is never snapshotted now, so it could be a plain directory.

The choice: keep it a dataset found by `shtetl:space=config`, or make
it a directory at a fixed path.

Recommendation: keep it a dataset. All four spaces are then found by
one rule, and nothing new has to be invented.

## 5. Does a house stack on its foundation's recipe as built?

A house's recipe stacks on its foundation's (their ver constraints
both hold, pkg lists join, a command piece the foundation ran is not
run again). The foundation's recipe in zconfig may have changed since
the version the house is on was built.

The choice: stack the house on the foundation's recipe in zconfig as
it is today, or on the shaken recipe that the foundation version
carries in /var/db/shtetl/foundation.conf.

Recommendation: on the one the version carries. It is what the
version was built with, and it cannot drift. (Plan section 5.)

## 6. How does a container know which version is live?

A container has several versions, each mounted at its own path. Its
jail needs one path, and the switch should be one atomic act.

The choice: a symlink `/containers/<house>` naming the live version,
switched with rename(2), with the jail's path set to the link; or a
property on the live version, which takes two acts to move (set on
the new, clear on the old).

Recommendation: the link. jail(2) follows it once, when the jail is
created, and holds the root, so switching it under a running jail
changes nothing until the next start (checked in
sys/kern/kern_jail.c; probe P2 on the box). The jail block in
/etc/jail.conf.d is written whole at every start, from the shaken
recipe in the live version's first snapshot, so it never needs
switching. A system's live version is the pool's bootfs, already one
act. (Plan section 7.4.)

## 7. When a foundation fails to build, does it pause or start over?

On main, a failed patch pauses the build for you to fix, with
--continue, --skip and --abort (you asked for that on 2026-10-11).
You now say a foundation whose patches fail "stops rolling".

The choice: keep the pause, or roll the foundation back to its newest
snapshot, mark it stalled, and start the next roll from the beginning.

Recommendation: start over. Fix the patch in zconfig, then run
`shtetl roll F`. Until you do, the foundation does not roll on its
schedule either, since you said it "stops rolling"; a property on its
dataset holds the reason, so the stall survives a restart. The warm
obj cache makes starting over cheap, and the pause, its record and
its three verbs all go away. (Plan sections 6 and 7.5.)

## 8. What happens when a newer foundation version arrives while a house has one waiting?

A house can have a rolled version waiting for its switch, or a roll
stuck on a conflict, when its foundation builds yet another version.

The choice: roll again onto the newest and throw the waiting version
away, or let the waiting one finish and switch first.

Recommendation: one roll in flight per house, never thrown away. The
house switches to the version it rolled, then rolls onto whatever is
newest at that moment. Nothing you answered in a conflict is lost,
and a house that is behind skips straight to the newest after each
switch. (Plan section 7.2.)

## 9. What happens when the switch's catch-up conflicts?

At the switch, the house is stopped and its final save is replayed
onto the waiting version. A change made since the roll can conflict
with the new foundation.

The choice: leave the house stopped until you answer, or start it
again on the version that was live and stall.

Recommendation: start it on the version that was live, and stall. The
switch has not happened, so the start that ends every switch simply
starts the old version. You answer the conflict, and the next switch
goes through. (Plan section 7.3.)

## 10. How should a stall reach you?

A stall means a foundation or house has stopped rolling and needs
you. You said an alert system could be added.

Recommendation, for this sprint: `shtetl status` lists every stall,
and each stall is logged to syslog. Next, a notifier that runs one
command you name in shtetl.conf, with the stall's object and reason
in its environment, so mail or anything else is your choice. (Plan
section 9, M7.)

## 11. Should zrun wait for zlake before it prunes a version?

zrun keeps fewer versions than zlake. If zlake is unreachable for a
while, zrun could prune a version zlake never received, and that
version would be lost.

The choice: prune zrun by its count regardless, or prune a version
from zrun only once zlake has it.

Recommendation: only once zlake has it. Losing a backup without a
word is worse than a disk filling, which `shtetl status` and the
notifier can show. This is the one place any actor waits for the beam.
(Plan section 8.2.)

## 12. Is shtetl a daemon now?

Actors need a process that lives: rolls run while houses run,
schedules fire, beams retry. On main, shtetl was a command and the
schedules went in a cron file (you ruled cron on 2026-10-14).

The choice: a daemon started by rc, with the `shtetl` command as a
client that sends it messages over a local socket and timers in
place of cron; or commands run from cron, each one an actor for as
long as it runs.

Recommendation: the daemon. Each act then has one path, whether a
timer or a person sent it, and the cron file goes away. (Plan section
9.)

## 13. Would you write and review Elixir?

Section 10 of the plan finds Elixir viable and a good fit for the
part of shtetl that coordinates foundations, houses and the beam,
with C kept for ZFS and UCL behind ports. The cost is a second
language that you would read in every review.

The choice: Elixir for the coordinator if the spike (S1, S2) passes,
or C throughout with a kqueue reactor and the same actors.

Recommendation: Elixir, if you are willing to read it; otherwise C,
and the design does not change. The spike runs either way, so your
answer can wait for its numbers.

## 14. What does a roll do when a house's recipe changed?

A house's recipe in zconfig may have gained a package or a command
since its live version was made. The roll replays what the house has;
on main, an update never ran pkg or commands again.

The choice: the roll ignores the change until a new house is made, or
the roll adds what the new recipe adds.

Recommendation: the roll adds it, after the replay and before the
version's snapshot: packages the recipe added are installed, and
command pieces that never ran run once. Nothing is removed, since the
language cannot remove. The version's house.conf then says what is
true of it.
