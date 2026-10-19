#!/bin/sh
# The gate, and exactly two checks: ASCII over every file of ours, and
# cstyle over the C sources. make check is a target of its own; what
# this says when it says "clean" is those two things and nothing more.
# tools/cstyle.pl is OpenZFS's, copied verbatim (CDDL, header intact).
# Portable: no grep -P, which BSD grep lacks.
set -u
cd "$(dirname "$0")/.." || exit 1
rc=0
nonascii=$(printf '[^\t -~]')
# tools/cstyle.pl is left out by name: it is carried OpenZFS code, and
# holds 0x01 bytes of its own. probes/results is the box's output.
if LC_ALL=C grep -rn "$nonascii" Makefile README.md LICENSE CLAUDE.md \
    .gitignore etc src tests sprints probes/*.sh probes/README.md \
    tools/*.sh tools/*.py; then
	echo "gate: non-ASCII bytes above"; rc=1
fi
srcs=$(find src tests -name '*.[ch]' | sort)
if perl tools/cstyle.pl -cpP $srcs; then
	echo "gate: cstyle ok on $(echo $srcs | wc -w | tr -d ' ') files"
else
	rc=1
fi
[ $rc -eq 0 ] && echo "gate: clean"
exit $rc
