#!/bin/sh
# probes/run.sh -- the M0 probes of docs/planning.md section 13.
#
#   sudo sh probes/run.sh               every probe that needs only ZFS
#   sudo sh probes/run.sh P1 P14        just these (world ones too)
#   sudo sh probes/run.sh --world       and the installworld ones
#   sh probes/run.sh --list             what there is
#   sudo sh probes/run.sh --clean       what a crash or KEEP=1 left
#
# Each probe gets pools of its own on memory disks (shprun, shplake,
# shplake2) and they are destroyed after it. Transcripts and a
# summary go to probes/results/<host>-<UTC time>/. See README.md.
#
# Environment: ZFS_REBASE (the binary), SRC (/usr/src), OLD_REL or
# OLD_BASE (P6, P15), WORLD_J (installworld -j), PAYLOAD, TMPDIR,
# KEEP=1 (one probe; leave its pools for a look).
set -u
PD=$(cd "$(dirname "$0")" && pwd) || exit 2
TOP=$(dirname "$PD")
. "$PD/lib.sh"

usage() {
	sed -n '3,8p' "$PD/run.sh" | sed 's/^# //'
	exit 2
}

hdr() { sed -n "s/^# $1: //p" "$2" | head -1; }

probe_file() {	# P1 | p01 | 1 -> probes/P01-*.sh
	_n=$(echo "$1" | sed 's/^[Pp]//; s/^0*//')
	case $_n in
	'' | *[!0-9]*) return 1 ;;
	esac
	for _f in "$PD"/P$(printf %02d "$_n")-*.sh; do
		[ -f "$_f" ] && echo "$_f" && return 0
	done
	return 1
}

list() {
	for f in "$PD"/P[0-9][0-9]-*.sh; do
		id=$(basename "$f" | sed 's/^P0*\([0-9]*\)-.*/P\1/')
		printf '%-4s %-5s %s\n' "$id" "$(hdr tier "$f")" "$(hdr title "$f")"
	done
}

WORLD=0
NAMED=
while [ $# -gt 0 ]; do
	case $1 in
	--list) list; exit 0 ;;
	--clean)
		[ "$(id -u)" = 0 ] || { echo "run.sh: --clean must run as root"; exit 2; }
		pools_down
		echo "run.sh: probe pools, memory disks and images removed"
		exit 0
		;;
	--world) WORLD=1 ;;
	-h | --help) usage ;;
	-*) usage ;;
	*)
		f=$(probe_file "$1") || { echo "run.sh: no probe $1"; exit 2; }
		NAMED="$NAMED $f"
		;;
	esac
	shift
done

[ "$(uname)" = FreeBSD ] || { echo "run.sh: FreeBSD only"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run.sh: must run as root (sudo)"; exit 2; }
for p in $RUNP $LAKEP $LAKE2P; do
	if zpool list -H -o name "$p" >/dev/null 2>&1; then
		echo "run.sh: pool $p exists (a KEEP=1 run, or a crash);" \
		    "remove it with: sudo sh probes/run.sh --clean"
		exit 2
	fi
done

if [ -n "$NAMED" ]; then
	FILES=$NAMED
else
	FILES=
	for f in "$PD"/P[0-9][0-9]-*.sh; do
		if [ "$(hdr tier "$f")" = world ] && [ "$WORLD" = 0 ]; then
			continue
		fi
		FILES="$FILES $f"
	done
fi
if [ "${KEEP:-0}" = 1 ] && [ "$(echo $FILES | wc -w)" -ne 1 ]; then
	echo "run.sh: KEEP=1 wants exactly one probe named"
	exit 2
fi

# zfs_rebase: ZFS_REBASE, else the one on PATH, else a checkout of
# zfs_rebase_tool beside this one. It must be the FreeBSD flavor.
ZR=${ZFS_REBASE:-}
if [ -z "$ZR" ]; then
	ZR=$(command -v zfs_rebase 2>/dev/null)
	[ -z "$ZR" ] && [ -x "$TOP/../zfs_rebase_tool/zfs_rebase" ] &&
	    ZR=$(cd "$TOP/../zfs_rebase_tool" && pwd)/zfs_rebase
fi
if [ -n "$ZR" ] && "$ZR" --abort zr-flavor-probe/none 2>&1 |
    grep -q 'not built with ZR_FREEBSD'; then
	echo "run.sh: $ZR is the portable build: make clean && make freebsd"
	exit 2
fi

OUT=$PD/results/$(hostname -s)-$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT" || exit 2
SUMMARY=$OUT/summary.txt
{
	echo "shtetl probes $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host:      $(uname -a)"
	echo "zfs:       $(zfs version 2>/dev/null | tr '\n' ' ')"
	echo "shtetl:    $(git -C "$TOP" rev-parse --short HEAD 2>/dev/null || echo '?')"
	if [ -n "$ZR" ]; then
		_zrt=$(dirname "$ZR")
		echo "zfs_rebase: $ZR $(git -C "$_zrt" rev-parse --short HEAD 2>/dev/null)"
	else
		echo "zfs_rebase: none found (ZFS_REBASE=, PATH, ../zfs_rebase_tool)"
	fi
	echo
} >"$SUMMARY"
cat "$SUMMARY"
export ZR OUT SUMMARY SRC="${SRC:-/usr/src}"

exec 3>&1
trap 'pools_down; exit 130' INT TERM
ran=0 errors=0 skipped=0
for f in $FILES; do
	PROBE=$(basename "$f" | sed 's/^P0*\([0-9]*\)-.*/P\1/')
	title=$(hdr title "$f")
	printf '%-4s %s\n' "$PROBE" "$title"
	echo "$PROBE $title" >>"$SUMMARY"
	if [ "$(hdr needs "$f")" = zfs_rebase ] && [ -z "$ZR" ]; then
		echo "$PROBE skipped: needs zfs_rebase" | tee -a "$SUMMARY" |
		    sed 's/^P[0-9]* /     /'
		skipped=$((skipped + 1))
		continue
	fi
	pools=$(hdr pools "$f")
	size=$(hdr size "$f")
	SCR=$(mktemp -d "${TMPDIR:-/tmp}/shtetl-probe.XXXXXX") || exit 2
	start=$(date +%s)
	(
		. "$f"
		for k in $pools; do
			pool_make "$(pool_of "$k")" "${size:-1g}" ||
			    die "could not make pool $(pool_of "$k")"
		done
		spaces "$pools"
		cd "$SCR" || exit 3
		probe
	) >"$OUT/$PROBE.log" 2>&1
	rc=$?
	secs=$(($(date +%s) - start))
	if [ "${KEEP:-0}" = 1 ]; then
		echo "     KEEP=1: pools left; remove with: sudo sh probes/run.sh --clean"
	else
		pools_down
	fi
	rm -rf "$SCR"
	case $rc in
	0) ran=$((ran + 1)); s="ran" ;;
	4) skipped=$((skipped + 1)); s="skipped" ;;
	*) errors=$((errors + 1)); s="ERROR (exit $rc)" ;;
	esac
	echo "     $s in ${secs}s; log $OUT/$PROBE.log"
	echo "$PROBE $s in ${secs}s" >>"$SUMMARY"
	echo >>"$SUMMARY"
done

echo "$ran ran, $errors errors, $skipped skipped" | tee -a "$SUMMARY"
_u=${SUDO_USER:-${DOAS_USER:-}}
[ -n "$_u" ] && chown -R "$_u" "$OUT"
cat <<EOF

Results: $OUT
To send them back: git add probes/results && git commit -m "probes: $(hostname -s) run" && git push
EOF
[ "$errors" = 0 ]
