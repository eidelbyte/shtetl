# new-history: the second history

Asked 2026-10-18: "We need to zoom out and re-architect. I'd like a
new branch with a new history", then "Make a new sprint folder, and
don't be afraid to work from scratch. Borrow project planning ideas
from the zfs_rebase local sprint json as well as its ruleset".

## What was built

- Branch v2, made with no parent (`git worktree add --orphan`), so
  it shares no history with main. main is untouched at 4c0f2c7.
- CLAUDE.md: the author's sentence first, then what it means for this
  code (write new and switch once, one owner per dataset, state in
  ZFS, no prechecks of what ZFS refuses, every line owed to an
  issue), then the zfs rebase project's rules: the order of trust
  from its CLAUDE.md, one commit per unit, the identity, ASCII, and
  matrices and worklogs; and from its memory: short errors with no
  legacy, questions that stand alone, reading the box's log, the
  terminal handed back, docs kept local.
- tools/tracker.py: the zfs rebase project's tracker, the default
  file now sprints/sprint-1/shtetl-sprint-1.json.
- sprints/sprint-1/: implementation-plan.md (a draft for ruling, in
  the sprint 4 plan's shape), open-questions.md (fourteen questions),
  shtetl-sprint-1.json (the zfs rebase tracker's _meta and _schema,
  the issues of M0).
- LICENSE carried unchanged; a short README.md.

## Decisions beyond the author's message

All are marked PROPOSAL in the plan or asked in open-questions.md:

- zrun owns every dataset and the beam runs one way (question 1).
- A house version is named for the foundation snapshot without its
  kind (question 2).
- A container's live version is a symlink switched by rename(2); a
  system's is bootfs, and its versions are boot environments mounted
  by rc.d/zfsbe (question 6; probes P2, P3).
- A jail's tree is untrusted: the host reads a house's shaken recipe
  from the version's create or update snapshot, never from the live
  tree, and writes the jail block whole at each start.
- A stall is either an open zfs_rebase run (a conflict) or a
  `shtetl:stall` property holding the reason (anything else). Found
  while re-reading the draft: without the property, a house whose
  roll failed would see its foundation still newer and roll straight
  into the same failure, forever, and a restart would do the same.
- One roll in flight per house, never superseded (question 8).
- No abort of a conflicted roll: every conflict can be answered.
- The issues of M1 to M7 are not written yet; their files depend on
  the runtime, which waits on the spike.

## Verified

- `tools/tracker.py list` and `show` read the tracker; every issue has
  the schema's fields, its requires name issues in the file, and its
  epic and milestone are in _meta.
- Every file is ASCII (a byte check over the tree).
- Claims about FreeBSD and ZFS checked in the local sources before
  the plan made them: lzc_receive_resumable, lzc_send_resume and
  zfs_send_resume_token_to_nvlist (include/libzfs_core.h,
  include/libzfs.h); receive_resume_token (zfs_prop.c); jail(2)
  resolving its path with FOLLOW once and holding the vnode
  (sys/kern/kern_jail.c); rc.d/zfsbe mounting the booted
  environment's children; .include in jail.conf(5).

Nothing ran on the box; M0 is the box's.

## Gotchas

- Sessions start in ~/freebsd-development, so this CLAUDE.md is not
  loaded on its own there. Read it before any shtetl work.
- Work in a job worktree of ~/shtetl, not ~/shtetl itself, which the
  author keeps on main.

## Commits

0d4f276 (rules, license), 408a682 (tracker), 880c162 (plan,
questions, tracker JSON, README).
