# Future work

Work no sprint's plan holds yet, each with why it is wanted and what
it would touch. An item moves into a sprint's plan when it is ruled.

## Container requirements (asked 2026-11-03)

The author: "containers shouldn't pin foundation names, just
requirements like version and kernel options". Today a house recipe
states only `ver` constraints (docs/configuration.md 4), and m5-create
clones a new house from the newest foundation version on zrun that its
`ver` allows. Nothing a container states can ask for a kernel option,
a build option or a point on a foundation's timeline.

To flesh out:

- The list of requirements a container may state: the version (the
  branch, a release, a patch level, or a src commit); kernel options
  (devices and options of the kernel config); build options
  (buildopts lines such as WITHOUT_LIB32); what else a house cannot
  run without.
- Where a requirement is read on a foundation version: from the
  shaken foundation.conf the version carries (plan 5), its facts, not
  its name.
- What happens when no foundation version meets them: a foundation
  made for them (below).
- What a house does when its foundation's next version stops meeting
  them: it stays on its version, or stalls, as a ver that no longer
  holds does today.

## Foundations on demand (asked 2026-11-03, open as Q-F1)

The author asked what happens when foundation X is at commit Y and a
container needs commit Y-3, and ruled that a foundation nothing on
zrun relies on may be deleted from existence, since nothing holds
state in a foundation.

As built (m4-prune), Y-3 is reachable in the first case below, and in
the second only if the lake kept it:

- zrun's copy of X still holds Y-3, because a house there is on Y-3
  or older: the new house is cloned from it.
- zrun holds nothing of X: a pull from Y-3 on works if the lake still
  holds Y-3. The lake keeps only X's newest and the versions something
  relies on, so Y-3 is most likely gone.
- zrun's copy of X starts after Y-3: Y-3 cannot be put into it. ZFS
  grows a dataset's history forward only: an incremental lands only on
  the copy's newest snapshot, and a full stream only where there is no
  dataset (cell Z14's refusals).

Proposal: a foundation's timeline only moves forward, and going back
is a new foundation, never a rewind.

- A container whose requirements only an older point meets gets a
  foundation of its own, a branch: built from the src commit that
  meets them, or, where the lake still holds that version, cloned
  from it there, so that it shares its blocks and needs no build. The
  branch rolls forward on its own line, with its own houses and its
  own copy on zrun, pulled from its first version; X and X's houses
  are untouched.
- Foundations are made when a container's requirements need one and
  no foundation meets them, and a foundation nothing on zrun relies on
  goes whole, in both spaces. The lake's copies of house versions are
  clones of the foundation versions they were made on; they would be
  promoted first, so that they hold those blocks themselves and keep
  working as restore points.
- A foundation is then known by what it meets (properties on its
  dataset), not by its recipe's name alone, and there may be more than
  one of a recipe.

Why it waits: today a foundation is made per recipe and rolled by
`shtetl roll`, and no house exists yet. Deleting a foundation nothing
on zrun relies on, as things stand, would destroy every foundation as
soon as it is built. It comes with the requirements above and with
houses (M5), which are what relies on a foundation.
