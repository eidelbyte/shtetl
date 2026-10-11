# shtetl

A system automation tool for FreeBSD. It builds FreeBSD from source
into foundations, runs containers (jails) and systems (bootable roots)
as ZFS clones of them, saves each one as a delta, and carries each one
onto a newer foundation with
[zfs_rebase](https://github.com/eidelbyte/zfs_rebase_tool). It follows
mishkan and parasa, without their git layer: ZFS snapshots are the
record, and git is used only to share recipes.

Nothing is built yet. The design:

- `docs/planning.md`: the plan, the rulings so far, and the probes
  that come before any code.
- `docs/story.html`: one cycle (build a foundation, create a
  container on it, update the container onto the next version) frame
  by frame. Generated: `python3 tools/story.py > docs/story.html`.
- `docs/story3d.html`: the same story as slides beside a three.js
  model of the four spaces that changes in place from slide to slide.
  Generated from the same story: `python3 tools/story3d.py >
  docs/story3d.html`. It loads three.js from jsDelivr, so it needs a
  network connection; without WebGL the slides still work.
- `docs/architecture.html`: the four spaces and the lifecycle on one
  page.
- `docs/ucl-primer.md`: the configuration format, and the rules
  shtetl adds to it.

BSD 3-Clause; see `LICENSE`.
