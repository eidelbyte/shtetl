# shtetl

A system automation tool for FreeBSD. It builds FreeBSD from source
into foundations, runs containers (jails) and systems (bootable roots)
as ZFS clones of them, saves each one as a delta, and carries each one
onto a newer foundation with
[zfs_rebase](https://github.com/eidelbyte/zfs_rebase_tool). It follows
mishkan and parasa, without their git layer: ZFS snapshots are the
record, and git is used only to share recipes.

Built so far (M1 and M2, planning.md 12):

- `shtetl bootstrap --lake ds --run ds --config ds --build ds`:
  datasets you have made the roots of the four spaces and laid out,
  after a table of every property it sets and your confirmation;
- `shtetl spaces`: the roots found, by the property `shtetl:space`;
- `shtetl config show [recipe]`: a recipe with its layers merged, or
  the site's settings, each value naming the file it came from (`-C
  dir` reads a configuration that is not the config root).

Everything else is still design:

- `docs/planning.md`: the plan, the rulings so far, and the probes
  that come before any code.
- `docs/story.html`: one cycle (build a foundation, create a
  container on it, update the container onto the next version) frame
  by frame. Generated: `python3 tools/story.py > docs/story.html`.
- `docs/slides.html`: the same story as slides beside one diagram
  that changes in place: datasets arrive and leave, snapshots land on
  their rows, each step's arrows draw themselves, and the view moves to
  where the step happens. Generated from the same story:
  `python3 tools/slides.py > docs/slides.html`. Plain SVG, no
  libraries.
- `docs/architecture.html`: the four spaces and the lifecycle on one
  page.
- `docs/ucl-primer.md`: the configuration format, and the rules
  shtetl adds to it.
- `probes/`: the M0 probes as scripts for the box: `sudo sh
  probes/run.sh`. See `probes/README.md`.

## Building

    make            the portable build
    make check      its unit tests and the command line end to end
    make gate       ASCII and cstyle, before a commit
    make freebsd    the FreeBSD build, linking the textproc/libucl port
    make xcheck     the FreeBSD sources compiled for FreeBSD, on the mac

The portable build compiles libucl from a FreeBSD source tree's
`contrib/libucl`. On FreeBSD that is `/usr/src`; elsewhere, name a
checkout once with `export FREEBSD_SRC=/path/to/freebsd-src`. The
tests are planned in `tests/MATRIX.md` before they are written; what
only real pools can show runs on the box (`tests/box/README.md`).

BSD 3-Clause; see `LICENSE`.
