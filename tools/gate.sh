#!/bin/sh
# Pre-commit gate, and exactly two checks: ASCII over every file of
# ours, and cstyle over the C sources. It does not run make check,
# which is a target of its own. What this says when it says "clean"
# is those two things and nothing more.
# cstyle.pl is OpenZFS's, copied verbatim from zfs_rebase_tool (CDDL,
# header intact). Portable: no grep -P, which BSD grep lacks.
set -u
cd "$(dirname "$0")/.." || exit 1
rc=0
nonascii=$(printf '[^\t -~]')
# tools/cstyle.pl is the one file left out, by name: it is carried
# OpenZFS code, and it holds lines with 0x01 bytes of its own, which
# it uses as a comment placeholder. probes/results is the box's
# output, not ours.
if LC_ALL=C grep -rn "$nonascii" Makefile README.md LICENSE .gitignore \
    docs src tests tracker probes/*.sh probes/README.md \
    tools/*.sh tools/*.py; then
	echo "gate: non-ASCII bytes above"; rc=1
fi
srcs=$(find src tests -name '*.[ch]' | sort)
if [ -n "$srcs" ]; then
	if perl tools/cstyle.pl -cpP $srcs; then
		echo "gate: cstyle ok on $(echo $srcs | wc -w | tr -d ' ') files"
	else
		rc=1
	fi
fi
[ $rc -eq 0 ] && echo "gate: clean"
exit $rc
