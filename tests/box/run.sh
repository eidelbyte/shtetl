#!/bin/sh
# tests/box/run.sh -- shtetl's cells on the box, on throwaway pools made
# as the probes make theirs (probes/lib.sh): family Z over libzfs
# (build/box_zcheck).
#
#   make freebsd && sudo sh tests/box/run.sh
#   sudo sh tests/box/run.sh --clean     what a crash left
#
# The log is tests/box/results/<host>-<UTC time>.log: its first line
# the commit it ran at, its last "== N ok, M failed". It is committed
# and pushed as the person behind sudo; SEND=no keeps it local.
set -u
BD=$(cd "$(dirname "$0")" && pwd) || exit 2
TOP=$(dirname "$(dirname "$BD")")
. "$TOP/probes/lib.sh"
SENDER=box

case ${1:-} in
"")
	;;
--clean)
	[ "$(id -u)" = 0 ] || { echo "run.sh: --clean needs root"; exit 2; }
	pools_down
	exit 0
	;;
*)
	sed -n '6,7p' "$BD/run.sh" | sed 's/^# //'
	exit 2
	;;
esac
[ "$(uname)" = FreeBSD ] || { echo "run.sh: FreeBSD only"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run.sh: needs root (sudo)"; exit 2; }
[ "$(cat "$TOP/build/.flavor" 2>/dev/null)" = freebsd ] ||
    { echo "run.sh: make freebsd first"; exit 2; }
for p in $RUN $LAKE; do
	if zpool list -H -o name "$p" >/dev/null 2>&1; then
		echo "run.sh: pool $p exists: sudo sh tests/box/run.sh --clean"
		exit 2
	fi
done

mkdir -p "$BD/results" || exit 2
OUT=$BD/results/$(hostname -s)-$(date -u +%Y%m%dT%H%M%SZ).log
trap 'pools_down; exit 130' INT TERM
{
	echo "shtetl $(git -C "$TOP" rev-parse --short HEAD 2>/dev/null)" \
	    "box on $(hostname -s) $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host: $(uname -rm)"
	echo "zfs: $(zfs version 2>/dev/null | tr '\n' ' ')"
	echo
	echo "family Z, over libzfs"
	if pool_make $RUN && zfs create $RUN/zc; then
		"$TOP/build/box_zcheck" $RUN/zc
	else
		echo "FAIL setup: pool $RUN and $RUN/zc"
	fi
	pools_down
	echo
} >"$OUT" 2>&1
ok=$(grep -c '^ok ' "$OUT")
failed=$(grep -c '^FAIL ' "$OUT")
echo "== $ok ok, $failed failed" >>"$OUT"
cat "$OUT"
send "$OUT" "$ok ok, $failed failed"
[ "$failed" = 0 ]
