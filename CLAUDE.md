# shtetl: guidelines

All lines of code must be justified. Simplicity is good. Edge cases
are a sign of bad pipelining. Instead of catching all edge-cases,
write interfaces that make them disappear.

## Pipelines, not special cases

- A special case is a finding, not a task. When code wants a branch
  for a state the pipeline should never reach, find the stage that
  produces the state and change its interface so the state cannot
  exist. When no such change is in sight, write the case in the
  worklog and ask before adding the branch.
- Write new, switch once. A stage writes only into something new: a
  new dataset, a new snapshot. What runs is not touched until one
  atomic switch, the last step. A failure before the switch drops the
  new thing, and nothing needs putting back.
- One owner per dataset. A dataset is written in one space; every
  other copy of it is converged to the owner's.
- State lives in ZFS. An actor keeps nothing a restart would lose:
  starting, it reads where it stands from ZFS. No state files, no
  records kept beside the data.
- Facts come from their binary source: properties, origins,
  createtxg, guids, nvlists. Names are for people; nothing parses a
  name or printed text. The shaken recipe and zfs_rebase's resolution
  are the text contracts, and nothing else is parsed.
- Do not check in advance what ZFS, jail(8) or pkg(8) refuse on their
  own; report their refusal. A precheck is a second copy of a rule.
- No temp files. Nothing is written outside the thing being built.
- Every line is owed to an issue. No option, hook or generality that
  no issue needs yet; nothing is released, so nothing is kept for
  compatibility.

## Trust, in order

1. The FreeBSD and OpenZFS sources (~/freebsd-development/freebsd,
   ~/freebsd-development/zfs) and what the box shows.
2. Primary sources: man pages, the handbook, the tools' own docs.
3. Our own docs, with skepticism. The first history (branch main) is
   the design this history replaces; take its probe results and
   rulings as evidence, not its structure as a template.

## Work

- One commit per unit of work. If code from an earlier unit has to
  change because of an unforeseen bug, roll back and fix it in that
  unit's commit.
- Commits: `Signed-off-by: Eidel Solomon <mail@eidelbyte.net>`, made
  with that git identity, no Co-authored line. Messages
  `area: what changed`, 72 columns.
- ASCII only, in code and in docs.
- Error messages are short and declarative ("pkgs: invalid key"). No
  explanation of the design, no ruling dates, no "no longer
  supported": a removed key is an invalid key.
- A TUI hands the terminal back as it found it on every way out.
- Docs are local files in this repository. Never publish them to
  claude.ai.

## C

The author: "we need to be very careful about the advantage of error
checking vs over-verbosity. Clean C doesn't have to be verbose, but
verbose C is painful."

- Kernel style: cstyle.pl, -std=c99 -Wall -Wextra -Werror
  -Wcast-qual.
- Check what can fail where it fails, once. A caller does not check
  again what its callee checked, and nothing tests an invariant the
  pipeline already guarantees.
- A function that holds resources has one way out: a single cleanup
  label, not a free and a close on every branch.
- An error is said once, where it is understood, as a short
  declarative message; the layers above pass it on and do not
  rephrase it.
- A helper that only renames a call, or wraps a single use, is a line
  to justify.

## Planning

- The plan of record is sprints/sprint-N/implementation-plan.md. The
  author's rulings are quoted there in their own words.
- The tracker is sprints/sprint-N/shtetl-sprint-N.json, driven by
  `tools/tracker.py --file <json> list|show|status|note`. Notes
  append; the field is single-valued and has been clobbered before.
  Issues are added by editing the JSON (json.dumps, indent 2,
  ensure_ascii, a final newline).
- Questions for the author go in a short file of their own, each one
  readable alone: what the thing is in plain words, the choice, and a
  recommendation. No bare identifiers or letters as the subject.

## Testing

Plan tests by plotting each phase's problem space first: enumerate
the dimensions, cross them into a cell matrix, and give every cell a
disposition -- a covering test, a mapping to an existing test, or a
documented deferral with its reason and unblocking work. The matrix
lives in tests/MATRIX.md; a cell with no row there is a hole. Test
files are segmented per phase, and each test's comment names the
cells it covers. Rebases are fiendish: a bug must have nowhere to
hide.

The mac runs the portable core, its tests and the fakes; the FreeBSD
box is the authority. A fake checks in the order the real layer
does. When the author says a box run passed, read the pushed log's
first line (the commit it ran at) and its ok/failed line before
recording anything.

## Worklogs

Every issue gets a worklog at sprints/sprint-N/issue-docs/<key>.md,
written when its code lands, not batched later: what was built and
why, decisions made on the way (above all anything beyond or against
the plan), how it was verified and what verification was deferred,
gotchas for the next issues, and the commit hash. Work that is not an
issue but changes project state (branch surgery, harness work) gets a
worklog too.
