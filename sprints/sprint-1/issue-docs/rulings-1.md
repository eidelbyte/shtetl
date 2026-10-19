# rulings-1: the M0 ruling

## What was done

The author's answers, eighteen questions in all, quoted in plan
section 18 as they came: the fourteen of 2026-10-18 (c471d6c), three
of four more on 2026-10-19 (422bf9c) and the last (d31aee8). The box
runs folded into plan 14.1 as they came back: run 1 (P1, P5 tested),
run 2 (P2 tested), run 3 (P6 tested, 16d9d46). Then the issues of M1
to M7 written into the tracker, 32 of them, each with what it needs,
its files, and how it is verified.

## Decisions

- P3 and P4 (the VM probes) no longer hold up the ruling: they are
  required by m6-boot-envs and m6-outer-space, the only issues that
  stand on them, so M1 to M5 go ahead without a VM.
- The author's "now continue development on shtetl" (2026-10-20) is
  taken as the go this issue's description waits for.
- Issues decided where the plan is silent, each marked PROPOSAL in its
  description: the daemon's paths and its lock (m1-daemon); the
  command printing the reply, so the daemon never writes to a
  person's terminal (m1-daemon), with descriptors passed only for an
  attended roll (m5-queue); no bootstrap verb, the skeleton made from
  one table when the daemon starts (m1-spaces); the foundation
  actor's start as one rule, every dataset rolled back to its newest
  snapshot or destroyed when it has none (m1-foundation-start);
  restore making the restored version newest with a fresh update
  snapshot (m5-restore).
- Two questions asked (open-questions.md): how the beam reaches a lake
  on another host (needed for m4-lake-remote), and who pulls the
  recipe repositories (needed for m2-remotes).
- The M1 order: m1-tree, m1-zfs, m1-spaces, m1-daemon,
  m1-foundation-start. The language (m2-language) needs only m1-tree.

## Commit

The issues: the commit after 16d9d46, "tracker: the issues of M1 to
M7".
