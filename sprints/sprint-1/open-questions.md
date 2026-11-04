# Sprint 1: open questions

The eighteen asked on 2026-10-18, the two the issues of M1 to M7
raised, the beam's first review, Q-B1 (2026-10-20), the five of
2026-11-03, Q-P1, Q-P2 and Q-F1 among them, and Q-J1 (2026-11-05)
are ruled in implementation-plan.md section 18. One is open.

## Q-H1 (2026-11-05): one house's failure when the daemon starts

When the daemon starts, it puts every house in place from ZFS, each
one's active version mounted at /containers/H, and starts its jail.
As built (m5-create), if ZFS refuses any step of putting one house in
place, the daemon's whole start fails: it exits, and no foundation,
beam or other house runs until a person starts it again. Build run 9
met this: a bug, now fixed, set s2web's mountpoint again while its
jail ran, ZFS said "busy", and the daemon did not come up. The plan
asks each actor to stop on its own when something fails, say so, and
hold nobody up (section 1).

The choice: (a) keep it as built: such a refusal should never
happen, and a daemon that will not start makes it plain. (b) The
house ZFS refuses is said to syslog, "house H: why", and left out:
not mounted, its jail not started, its status line "house H failed:
why" until the daemon's next start tries it again from ZFS; the
daemon and every other house start as usual. A jail still running
from before is left running, since the daemon's stop does not stop
jails either (Q-J1).

Recommendation: (b), as the plan's rule asks. One container's trouble
should not keep every other container, and the foundations' rolls,
from running; and since only the start knows of the failure, a
restart is the retry, as it is for a crash.
