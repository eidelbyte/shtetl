# Sprint 1: open questions

The eighteen asked on 2026-10-18, the two the issues of M1 to M7
raised, the beam's first review, Q-B1 (2026-10-20) and the two of
2026-11-03 are ruled in implementation-plan.md section 18. Two are
open, both of m4-prune.

## Q-P1 (2026-11-03): the versions of a foundation no house is on

A foundation's versions are the snapshots of its dataset on zlake, one
per roll, and their copies on zrun. Plan 8.2 keeps a version while a
house version is cloned from it, and always keeps the newest, with no
setting; it is marked PROPOSAL and was never ruled. On zrun a house
that is behind also keeps every later version, since a house rolls
through each in order (7.2, ruled).

What the proposal means in practice: a foundation with no house on it
keeps one version. After each roll, once zrun holds the new version,
the one before it is destroyed in both spaces. There is no going back
to an earlier build of a foundation unless a house still sits on it;
the way back is to roll again from the src and patches of then. On the
box today, with no houses yet, build.sh's update roll destroys the
create version as soon as the beam has carried the update.

The choice: (a) as proposed, the newest only, no setting; or (b) a
setting, foundation.keep (say 2), the newest that many kept whether or
not a house uses them, on top of those houses keep.

Recommendation: (a). A foundation version nothing runs on is a build
product that can be rebuilt; houses already keep what they run on,
and the lake keeps lake.keep versions of every house, and with them
the foundation versions those are clones of. (b) is a line in the
settings and a count in the prune, and can come later if a rollback
of a foundation alone is ever wanted.

## Q-P2 (2026-11-03): where the prune says what it did

The beam says every stream in beam.log and its own failures to syslog
once each (ruled 2026-10-20). Pruning destroys data, so what it
destroyed should be findable afterwards.

Recommendation: each version pruned is a line in beam.log, "# prune
NAME", whichever actor pruned it, and a prune that ZFS refuses is said
as the beam's failures are: once to syslog, "prune: why", and as a
line of status while it keeps failing. Nothing else goes to syslog.
The alternative is a log of its own, prune.log, which splits one
convergence's account across two files.
