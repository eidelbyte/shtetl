# Sprint 1: open questions

The eighteen asked on 2026-10-18 are ruled in implementation-plan.md
section 18. Two new ones came with writing the issues of M1 to M7.
Neither holds up M1 to M3.

## 1. How does the beam reach a lake on another host?

When zlake is on another machine, the lake remote names it, and the
beam must learn which snapshots that machine already holds (by guid)
and send it streams that resume where a cut left them.

- One way: the run host reaches the lake host over ssh and runs zfs(8)
  there: `zfs list` for what it holds, `zfs receive -s` for the
  streams. Nothing to install on the lake host, but shtetl would parse
  `zfs list`'s printed output, which the ground rules forbid.
- The other: a shtetl daemon runs on the lake host too, and the run
  host's beam speaks to it over ssh in shtetl's own framed records
  (the ones the command already uses). Each end reads guids from
  libzfs and receives with lzc_receive_resumable, so nothing parses
  text, and the beam's code is the same on both ends.

Recommendation: the second. It is needed for m4-lake-remote only.

## 2. Who updates the recipe repositories in zconfig?

Each recipe repository is cloned into a directory of zconfig of its
own. Someone has to bring in new commits from its remote.

- shtetl clones a repository that is missing and never pulls one. A
  recipe changes only when the person pulls or edits it, and the next
  roll of what it builds picks the change up (ruled: "the user can
  guide all this").
- shtetl pulls every recipe repository before each foundation roll,
  so a scheduled roll always builds the remote's latest recipes.

Recommendation: the first. The src remote and the patch repositories
are still fetched by every foundation roll, since they are the build's
inputs. It is needed for m2-remotes.
