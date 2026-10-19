#!/bin/sh
# tests/box/run.sh -- shtetl's cells on the box: make check-freebsd, the
# unit tests on this libc and the textproc/libucl port; then, on
# throwaway pools made as the probes make theirs (probes/lib.sh),
# family Z over libzfs (build/box_zcheck), and a daemon (./shtetl)
# started over what a crash left, family A.
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
	echo "usage: make freebsd && sudo sh tests/box/run.sh"
	echo "       sudo sh tests/box/run.sh --clean"
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
# The daemon's cells start a daemon, which finds every space marked on
# this box, and answers on the one socket.
if [ -n "$(zfs get -H -s local -o name shtetl:space 2>/dev/null)" ]; then
	echo "run.sh: this box has shtetl spaces, which the daemon would find"
	exit 2
fi
SH=$TOP/shtetl
if "$SH" status >/dev/null 2>&1; then
	echo "run.sh: a shtetl daemon is running: service shtetl onestop"
	exit 2
fi

cell() {	# CELL WHAT GOT WANT
	if [ "$3" = "$4" ]; then
		echo "ok   $1 $2"
	else
		echo "FAIL $1 $2: got [$3], wanted [$4]"
	fi
}

# Family A: a daemon started over what a crash left on a lake of its
# own, asked for its status, and stopped.
daemon_cells() {
	F=$LAKE/foundations
	if ! { pool_make $LAKE &&
	    x zfs set shtetl:space=lake $LAKE &&
	    x zfs create -o canmount=off $F &&
	    x zfs create -o mountpoint=/generic $F/generic &&
	    x touch "$(dir $F/generic)/a" &&
	    x zfs snapshot $F/generic@v1 &&
	    x touch "$(dir $F/generic)/b" &&
	    x zfs create -o mountpoint=/first $F/first &&
	    x touch "$(dir $F/first)/a" &&
	    x zfs create -o "shtetl:stall=build: make exited 2" $F/stalled &&
	    x zfs snapshot $F/stalled@v1; }; then
		echo "FAIL setup: the lake and its foundations"
		pools_down
		return
	fi
	"$SH" daemon &
	_d=$!
	for _i in 1 2 3 4 5 6 7 8 9 10; do
		"$SH" status >/dev/null 2>&1 && break
		sleep 1
	done
	cell A1 "status" "$("$SH" status 2>&1)" "space lake $LAKE
foundation generic idle v1
foundation stalled stalled build: make exited 2"
	cell A2 "generic unmounted" "$(prop mounted $F/generic)" no
	x zfs mount $F/generic
	cell A2 "what came after v1 gone" "$(ls "$(dir $F/generic)" |
	    tr '\n' ' ')" "a "
	x zfs unmount $F/generic
	cell A3 "first, with no version, gone" \
	    "$(zfs list -H -o name $F/first 2>/dev/null)" ""
	cell A3 "and logged" "$(grep -c 'foundation first: a first build undone' \
	    "$OUT")" 1
	kill -TERM $_d
	wait $_d
	cell D10 "SIGTERM: exit 0" "$?" 0
	cell D10 "the socket gone" "$(ls /var/run/shtetl.sock 2>/dev/null)" ""
	pools_down
}

mkdir -p "$BD/results" || exit 2
OUT=$BD/results/$(hostname -s)-$(date -u +%Y%m%dT%H%M%SZ).log
trap 'pools_down; exit 130' INT TERM
{
	echo "shtetl $(git -C "$TOP" rev-parse --short HEAD 2>/dev/null)" \
	    "box on $(hostname -s) $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host: $(uname -rm)"
	echo "zfs: $(zfs version 2>/dev/null | tr '\n' ' ')"
	echo
	# as the person behind sudo, so the build stays theirs
	echo "make check-freebsd: the unit tests, on this libc and libucl"
	if [ -n "${SUDO_USER:-}" ]; then
		su "$SUDO_USER" -c "make -C '$TOP' check-freebsd"
	else
		make -C "$TOP" check-freebsd
	fi
	echo
	echo "family Z, over libzfs"
	if pool_make $RUN && zfs create $RUN/zc; then
		"$TOP/build/box_zcheck" $RUN/zc
	else
		echo "FAIL setup: pool $RUN and $RUN/zc"
	fi
	pools_down
	echo
	echo "family A, the daemon on real pools"
	daemon_cells
	echo
} >"$OUT" 2>&1
ok=$(grep -c '^ok ' "$OUT")
failed=$(grep -c '^FAIL ' "$OUT")
echo "== $ok ok, $failed failed" >>"$OUT"
cat "$OUT"
send "$OUT" "$ok ok, $failed failed"
[ "$failed" = 0 ]
