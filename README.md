# shtetl

A system automation tool for FreeBSD on ZFS. It builds FreeBSD from
source into foundations, runs houses -- containers (jails) and
systems (bootable roots) -- as clones of them, and rolls each house
onto each new foundation with
[zfs_rebase](https://github.com/eidelbyte/zfs_rebase_tool).

This branch is a second history, started 2026-10-18 to re-architect
shtetl as actors over ZFS. The first history is branch main.

- CLAUDE.md: how the work is done.
- sprints/sprint-1/implementation-plan.md: the plan of record.
- sprints/sprint-1/open-questions.md: what waits on the author.

BSD 3-Clause; see LICENSE.
