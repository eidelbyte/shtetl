# Sprint 1: open questions

The eighteen asked on 2026-10-18, the two the issues of M1 to M7
raised, and the beam's first review are ruled in implementation-plan.md
section 18. One is open.

## Q-B1 (2026-10-20): when the beam may destroy to recover

Plan 8.1 says nothing is destroyed on a failure. A stream that breaks
off leaves what arrived and a resume token on the copy, the next
convergence resumes it, and the retries back off from a minute to an
hour: a lost pool or a cut stream heals by itself. Two failures never
heal by themselves, because what the copy holds can no longer meet the
owner's history:

1. A resume token whose stream's snapshot is gone from the owner (a
   person destroyed it; pruning waits for the beam, so prune does
   not). The resume is refused every time, and the token blocks every
   other receive into that copy.
2. A copy whose newest snapshot is newer than the newest it shares
   with its owner: a snapshot a person took on the copy, or the copy
   of a version the owner destroyed before all of it arrived. Every
   incremental onto it is refused.

Either failure holds up only that dataset and what needs it (ruled
2026-10-20), and syslog hears it once. It stays until the person runs
zfs receive -A, or destroys the copy's snapshot.

Recommendation: the beam recovers both by itself, destroying only what
blocks it. For 1, it takes the token away, as receive -A does, when
the resume is refused because the snapshot it sends or the one it was
sent from is gone. For 2, it destroys the copy's snapshots newer than
the newest it shares with the owner, as receive -F rolls back. It says
each once in syslog, and the next look sends the copy what it lacks.
What is lost is a partial receive of a snapshot that no longer exists,
or snapshots on a copy, which is not its own owner (plan 2, rule 3).
Every other failure keeps everything. The layer reports "gone" as a
fact, not in words, so the beam does not read text.
