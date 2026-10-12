# M3: foundations (worklog)

2026-10-12, branch impl/m3. planning.md 12's M3: foundation build and
update into zlake, patches and the pause included; pkg-upgrade. Cut
into the tracker issues m3-version, m3-zfs, m3-build, m3-patches,
m3-update and m3-pkg-upgrade (tracker/shtetl.json, f118120).

## m3-version (ac8101a)

A version's name (6.1) and the ver check (7.1 step 1), in
src/core/version.c, portable, over values the build will hand in: the
recipe's branch, the checkout's REVISION, the build's time and the
checkout's commit.

- **The branch is a spelling now.** src_branch was any string; 6.1's
  names need main, stable/N or releng/N.M, so the schema refuses
  anything else when the recipe is read (sh_syn_branch in syntax.c,
  with the other spellings).
- **REVISION from newvers.sh -V.** main's major is in the checkout's
  sys/conf/newvers.sh. Asking it (-V REVISION, its documented way)
  rather than reading the shell file; its one line, REVISION="N.M",
  is the only output taken, anything else refused.
- **Two checks.** Before the checkout only the branch is known:
  releng/N.M must satisfy every ver, stable/N leave some N.x they
  all allow (Helly in one dimension: the constraints already meet,
  so each meeting N.x is enough), main passes. After the checkout
  REVISION is checked exactly; stable/15's REVISION may be 15.3.
- **The day is UTC.** gmtime_r, whatever TZ says; the test moves TZ
  to UTC+14 and UTC-11 around midnight.
- **The counter** compares whole names: the -pkg twin and another
  day's name do not count. Names are read only for this; versions are
  ordered by createtxg (6.1).

Verification: check_version, 97 checks (V1 - V8); check_schema for
CK23; make check, gate, xcheck.

Gotchas for the build: sh_version_name wants the full commit (40 hex)
or at least 12 hex digits, lowercase as git prints them; the build
asks git for HEAD's full sha.
