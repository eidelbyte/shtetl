# Results that name neither the box nor its person

Not a tracker issue: harness work. The author asked, 2026-11-03,
whether the logs leak computer or user names, "if so we should fix
that". They did.

## What leaked

- The box's host name: in every result's file name and first line, in
  the commit subject send wrote ("box: <host>: 127 ok"), and in the
  host field of the syslog lines build.sh copies.
- The person's login: the checkout lives under their home, and its
  path showed in commands x echoes ("make -C <path> install") and in
  make's own "stopped making ... in <path>" lines.
- The worklogs, the plan, the matrix and the tracker named the box in
  their accounts of each run.
- The compressed install logs of main's probes, swept into v2 by
  eb1e88b, held FreeBSD's "toolchain metadata from build at <host>"
  line.

## What was built

- Results are named by their UTC time alone:
  probes/results/<time>/, tests/box/results/<time>.log and
  build-<time>.log. The commit subject is the summary line alone.
- send, the one way results leave the box, rewrites every file it
  sends: the checkout's path becomes ".", the host's short name "box"
  and the person's name "user", as whole words, whatever printed them.
  A log's first line still says where it ran, so the box's read "on
  box" and the VM's "on shtetlvm", a name the harness gave.
- The tree: 25 result files and directories renamed to their time (the
  VM's too, so one rule names all), their contents scrubbed the same
  way, the 39 compressed logs decompressed, scrubbed and recompressed;
  the docs' accounts of each run say "the box" or no name.

## Decisions

- One scrub at the exit, not a fix at each source. The sources are
  open-ended: make prints its directory on any failure, zpool and the
  FreeBSD build print the host name, and the next tool will print
  something else. send already sees every file that leaves.
- Whole words, so a name inside another word stays. An empty host name
  makes the pattern [[:<:]][[:>:]], which matches nowhere, so no
  check is needed for it.
- History is not rewritten. Every commit before these two still holds
  the names, in files and in 17 commit subjects; main's tree holds
  them in 46 files, 20 with the login, and 170 paths, and its history
  in 26 subjects. Removing them means rewriting both branches and
  force-pushing, which changes every commit hash the docs cite. That
  is the author's call; nothing here does it.

## Verification

- send's sed and find, run with the mac's BSD tools on copies of a
  run.sh log, a build.sh log and a probe summary: no name left, the
  echoed make line reads "make -C . install".
- The tree: no host name, login or home path left in any tracked
  file, the compressed logs included (decompressed and searched).
  Only /home/shtetl remains, the VM's checkout, a name the harness
  gave.
- Box run 9 and build run 6 (2026-11-04, results 45b83bd and
  b85742f): files named by their time alone, commit subjects "box:
  132 ok, 0 failed" and "box: build: 30 ok, 0 failed", first lines
  "on box", the echoed "make -C . install", syslog's host field "box";
  no host name, login or home path in either.

## Gotchas

- A results file name made from anything but the time brings the leak
  back: sed cannot rename.
- send scrubs in place, so the local copy matches what was pushed.
- main is the first history and the default branch; it was not
  touched.

## Commits

53a8cf4 (the harness), and the commit that renames and scrubs the
tree, with this worklog.
