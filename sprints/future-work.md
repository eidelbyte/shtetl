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

## Foundations on demand, out of sight (ruled 2026-11-03, Q-F1)

The author: "the user should really only care about containers and
systems and foundations get built behind the scenes to fulfill
requirements", and "just prune unused foundations from everywhere and
just keep required + forward" (plan 18).

- The person asks for containers and systems and states their
  requirements (above). shtetl finds a foundation that meets them, or
  builds one, and rolls, keeps and prunes it itself. Foundations leave
  the person's commands and status: `shtetl roll F` and `shtetl pkg F`
  become shtetl's own acts, or a house's.
- A foundation nothing on zrun relies on goes from both spaces. The
  lake's copies of house versions are clones of the foundation
  versions they were made on; they are promoted first, so that they
  hold those blocks themselves and stay restore points. Whether the
  foundation's obj cache in foundation-builds goes with it, or stays
  to make the next build of it short, is to be asked.
- Required and forward is built already (m4-prune): in both spaces a
  foundation keeps every version from the oldest one something is a
  clone of. Until foundations are made on demand, the lake keeps a
  foundation's newest when nothing requires it, since otherwise every
  foundation would go as soon as it is built.
- Not handled, by ruling: a container whose requirements only a point
  older than a foundation's newest meets. ZFS grows a dataset's
  history forward only (an incremental lands only on a copy's newest
  snapshot, cell Z14), so such a point would be a foundation of its
  own, a branch, should it ever be wanted.
- A foundation is known by what it meets, properties on its dataset,
  not by a recipe's name alone, so that one recipe can have more than
  one foundation.
- The pull (m5-create, m5-restore): a house needing a foundation zrun
  does not hold pulls it from the version it is made on, forward. The
  prune takes a copy nothing on zrun is a clone of, so the pull and
  the house's clone must leave no convergence's end between them, or
  the pull is marked in ZFS as relied on.
