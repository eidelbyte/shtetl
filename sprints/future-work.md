# Future work

Work no sprint's plan holds yet, each with why it is wanted and what
it would touch. An item moves into a sprint's plan when it is ruled.

## Container requirements (asked 2026-11-03)

The author: "containers shouldn't pin foundation names, just
requirements like version and kernel options"; "we'll run a
compatibility check between the container and the picked foundation";
and "a container config file won't define a foundation by name, just
by needed constraint. Any foundation that meets those constraints will
be available for the container" (plan 18). So a container's recipe
states constraints and names no foundation; every foundation that
meets them is available to it, and the person picks one as they make
the container, `shtetl create H --on F`, the check refusing one that
does not meet them. Today a house recipe states only
`ver` constraints (docs/configuration.md 4), held against the picked
foundation's branch (m5-create). Nothing a container states can ask
for a kernel option, a build option or a point on a foundation's
timeline.

To flesh out:

- The list of requirements a container may state: the version (the
  branch, a release, a patch level, or a src commit); kernel options
  (devices and options of the kernel config); build options
  (buildopts lines such as WITHOUT_LIB32); what else a house cannot
  run without.
- Where a requirement is read on a foundation version: from the
  shaken foundation.conf the version carries (plan 5), its facts, not
  its name.
- What the check says when the picked foundation does not meet them:
  the create refused with the requirement it fails, as a ver that
  does not hold is now.
- The foundations available to a container: those its constraints
  allow, for a picker to list (shtetl create H with no --on, or the
  TUI).
- A container rebased onto another foundation during an update, a pick
  again and the check, then the roll's replay onto the new foundation's
  version: "strange, but totally doable" (the author). Plan 17 holds
  moving a house to another foundation recipe out of scope; this
  would bring it in.
- What a house does when its foundation's next version stops meeting
  them: it stays on its version, or stalls, as a ver that no longer
  holds does today.

## Foundations on demand, out of sight (ruled 2026-11-03, Q-F1)

The author: "the user should really only care about containers and
systems and foundations get built behind the scenes to fulfill
requirements", and "just prune unused foundations from everywhere and
just keep required + forward" (plan 18).

- The person asks for containers and systems, picking each one's
  foundation, and states their requirements (above), which are
  checked against the pick. shtetl builds a picked foundation that is
  not built yet, and rolls, keeps and prunes it itself. Foundations
  leave the person's other commands and status: `shtetl roll F` and
  `shtetl pkg F` become shtetl's own acts, or a house's.
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

## Container dependencies (ruled 2026-11-05)

The author: "We need to create a dependency startup tree however. Put
this in future work with the rest of the containers dependency work"
(plan 18). A container's recipe names the containers it needs with
`requires` (docs/configuration.md 4). Today that is a check of the
language and nothing more: each entry is a container recipe, and not
the container itself. Nothing reads it when a container is made or
started, and the daemon's start starts every container's jail at once
(m5-jail).

To flesh out:

- The startup tree: the containers started in the order `requires`
  gives, a required container's jail started before the jails of the
  containers that require it, and those with nothing between them at
  once.
- A cycle, a container that requires another that requires it in
  turn: refused where the recipes are read, as a container requiring
  itself is now.
- A required container whose jail does not start: whether the
  containers that require it are left unstarted, and said, or started
  anyway.
- Stopping in the reverse order, a container before what it requires.
  The daemon's stop stops no jail (ruled 2026-11-05, Q-J1), so who
  stops the containers when the host shuts down, and in that order, is
  to be asked.
- A roll of a required container: whether the jails of those that
  require it are stopped and started around its swap, or left
  running.
- A create checking that each of `requires` is a container on zrun
  already, as main's create did (ruled 2026-10-14 on main: that it is
  there, not that it runs).
- One container mounting another's data, left for later on main
  (2026-10-14).
