# Sprint 1: open questions

The eighteen asked on 2026-10-18, the two the issues of M1 to M7
raised, the beam's first review, Q-B1 (2026-10-20), and the five of
2026-11-03, Q-P1, Q-P2 and Q-F1 among them, are ruled in
implementation-plan.md section 18. One is open.

## Q-J1 (2026-11-05): the containers when the daemon stops

When the shtetl daemon starts, it starts every container's jail
(m5-jail, as its issue says). Nothing says what happens to the jails
when the daemon stops, and the daemon stops in two ways: a person
runs `service shtetl stop` or `restart`, or the host shuts down, when
rc stops the daemon on its way down. As built, the daemon's stop
leaves every jail running, and its next start finds them running and
leaves them so (jail -cm). Then at a shutdown nothing runs a
container's own rc.shutdown: the kernel's last sweep sends its
processes SIGTERM and then SIGKILL, which a database in a container
may not get through cleanly.

The choice: (a) keep it as built: the containers outlive the daemon,
so restarting shtetl, to upgrade it say, never takes a container
down, and a shutdown stops them only by that sweep. (b) The
containers run while the daemon runs: its stop stops every jail with
jail -r, which runs the container's rc.shutdown and takes its devfs
away, and only then does the daemon exit; a restart of shtetl
restarts every container, and a shutdown stops each one properly.

Recommendation: (b). A container stopped badly at every shutdown
costs more than a container restarted at the rare upgrade of shtetl,
and the daemon's start and stop become a pair. It is a few lines in
the daemon's stop, the houses' stops before it exits; until the
ruling, the daemon stops none.
