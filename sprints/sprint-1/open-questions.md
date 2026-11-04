# Sprint 1: open questions

The eighteen asked on 2026-10-18, the two the issues of M1 to M7
raised, the beam's first review, Q-B1 (2026-10-20), and the four of
2026-11-03, Q-P1 and Q-P2 among them, are ruled in
implementation-plan.md section 18. One is open.

## Q-F1 (2026-11-03): going back on a foundation's timeline

A foundation is one dataset on the lake whose snapshots are its
versions, one per roll, and its copy on the run holds the versions
houses there rely on. ZFS grows a dataset's history forward only, so
once the run's copy starts after a version, that version cannot be
put back into it, and the lake keeps only a foundation's newest and
what something relies on. A container that needs an older point than
a foundation's newest (X at commit Y, the container needing Y-3)
therefore has nowhere to come from, unless a house already sits on
Y-3 or older.

The choice: (a) never rewind a timeline. An older point is a
foundation of its own, a branch, made for the container's
requirements: built from the src commit that meets them, or cloned
from the lake's version where it is still there. Foundations are made
when requirements need one and none meets them, and a foundation
nothing on zrun relies on goes whole, from both spaces, the lake's
house copies promoted first so they keep working as restore points.
(b) Keep a longer history on the lake, so that a pull can reach back
further on the one line: older points come quickly, but only as far
back as the history kept, and every house on the line still rolls
forward through every version after the one it was made on.

Recommendation: (a), written out in sprints/future-work.md beside the
container requirements it depends on, and built with them after M5.
Until then M4 stays as built: the lake keeps a foundation's newest,
because deleting a foundation nothing on zrun relies on, with no house
yet anywhere, would destroy every foundation as soon as it is built.
