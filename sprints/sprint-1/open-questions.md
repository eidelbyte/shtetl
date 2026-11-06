# Sprint 1: open questions

One open, Q-A1. The eighteen asked on 2026-10-18, the two the issues
of M1 to M7 raised, the beam's first review, Q-B1 (2026-10-20), the
five of 2026-11-03, Q-P1, Q-P2 and Q-F1 among them, and Q-J1 and Q-H1
(2026-11-05) are ruled in implementation-plan.md section 18.

## Q-A1: zfs_rebase's closing advice on an attended roll's terminal

When shtetl roll H meets a conflict from a terminal, the picker step
runs zfs_rebase -c -m -i on that terminal, so what zfs_rebase prints
as it ends lands there too. Its last lines are advice for a person
who drives zfs_rebase by hand, and in a shtetl roll they are wrong.
Both were seen in the dev VM on 2026-11-06 (m5-queue's worklog):

- Answered and written, the roll going on: "... is the rebased tree,
  unmounted; place it with zfs inherit mountpoint ... or zfs set
  mountpoint=PATH ...". shtetl places the tree itself, in its next
  step.
- Stopped with ^C: "the resolution ... stands at the conflicts gate;
  continue with: zfs_rebase -c ... -i". shtetl aborts that run next,
  so there is nothing to continue. The command's own last line,
  "shtetl: H: stalled picker: exited 1", is the true one.

The choice: leave the lines as they are; have shtetl print a line of
its own after them saying how the roll ended, rolled or stalled; or
give zfs_rebase a way to leave the advice out when a program drives
it, which is a change in zfs_rebase_tool.

Recommendation: the zfs_rebase change. The advice is the tool's to
give or leave out, a program that drives the tool knows the next step
better than the tool does, and shtetl's reply already says how the
roll ended when it stalls.
