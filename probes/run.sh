#!/bin/sh
# probes/run.sh -- the M0 probes of sprints/sprint-1/implementation-plan.md
# section 14.
#
#   sudo sh probes/run.sh            every probe but the VM ones
#   sudo sh probes/run.sh P1 P5      just these
#   sh probes/run.sh --list          what there is
#   sudo sh probes/run.sh --clean    what a crash left
#
# The log of a run is probes/results/<host>-<UTC time>/: summary.txt
# (the commit it ran at, every check, "== N ok, M failed") and one log
# per probe. It is committed and pushed as the person behind sudo;
# SEND=no keeps it local. ZFS_REBASE names zfs_rebase if PATH has none.
# A VM probe runs only when named, with SCRATCH_VM=yes. See README.md.
set -u
PD=$(cd "$(dirname "$0")" && pwd) || exit 2
TOP=$(dirname "$PD")
. "$PD/lib.sh"

hdr() { sed -n "s/^# $1: //p" "$2" | head -1; }

probe_file() {	# P1 -> probes/P1-*.sh
	for _f in "$PD/$1"-*.sh; do
		[ -f "$_f" ] && echo "$_f" && return 0
	done
	return 1
}

FILES=
while [ $# -gt 0 ]; do
	case $1 in
	--list)
		for f in "$PD"/P[0-9]*-*.sh; do
			[ -f "$f" ] || continue
			printf '%-3s %-10s %s\n' "$(basename "$f" | cut -d- -f1)" \
			    "$(hdr needs "$f")" "$(hdr title "$f")"
		done
		exit 0
		;;
	--clean)
		[ "$(id -u)" = 0 ] || { echo "run.sh: --clean needs root"; exit 2; }
		pools_down
		exit 0
		;;
	P[0-9]*)
		f=$(probe_file "$1") || { echo "run.sh: no probe $1"; exit 2; }
		FILES="$FILES $f"
		;;
	*)
		sed -n '5,8p' "$PD/run.sh" | sed 's/^# //'
		exit 2
		;;
	esac
	shift
done

[ "$(uname)" = FreeBSD ] || { echo "run.sh: FreeBSD only"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run.sh: needs root (sudo)"; exit 2; }
for p in $RUN $LAKE; do
	if zpool list -H -o name "$p" >/dev/null 2>&1; then
		echo "run.sh: pool $p exists: sudo sh probes/run.sh --clean"
		exit 2
	fi
done
if [ -z "$FILES" ]; then
	for f in "$PD"/P[0-9]*-*.sh; do
		[ -f "$f" ] || continue
		[ "$(hdr needs "$f")" = vm ] || FILES="$FILES $f"
	done
fi

ZR=${ZFS_REBASE:-$(command -v zfs_rebase 2>/dev/null)}
OUT=$PD/results/$(hostname -s)-$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT" || exit 2
SUMMARY=$OUT/summary.txt
{
	echo "shtetl $(git -C "$TOP" rev-parse --short HEAD 2>/dev/null)" \
	    "probes on $(hostname -s) $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host: $(uname -rm)"
	echo "zfs: $(zfs version 2>/dev/null | tr '\n' ' ')"
	echo "zfs_rebase: ${ZR:-none}"
	echo
} >"$SUMMARY"
cat "$SUMMARY"
export ZR

exec 3>&1
trap 'pools_down; exit 130' INT TERM
for f in $FILES; do
	PROBE=$(basename "$f" | cut -d- -f1)
	needs=$(hdr needs "$f")
	echo "$PROBE $(hdr title "$f")" | tee -a "$SUMMARY"
	case $needs in
	zfs_rebase)
		if [ -z "$ZR" ]; then
			note "skipped: needs zfs_rebase (ZFS_REBASE or PATH)"
			continue
		fi
		;;
	vm)
		if [ "${SCRATCH_VM:-no}" != yes ]; then
			note "skipped: a VM probe; SCRATCH_VM=yes on a scratch VM"
			continue
		fi
		;;
	esac
	(
		. "$f"
		for k in $(hdr pools "$f"); do
			case $k in
			run) pool_make $RUN || die "pool $RUN" ;;
			lake) pool_make $LAKE || die "pool $LAKE" ;;
			esac
		done
		probe
	) >"$OUT/$PROBE.log" 2>&1
	pools_down
	echo >>"$SUMMARY"
done

ok=$(grep -c '^P[0-9]* ok ' "$SUMMARY")
failed=$(grep -c '^P[0-9]* failed' "$SUMMARY")
echo "== $ok ok, $failed failed" | tee -a "$SUMMARY"
send "$OUT" "$ok ok, $failed failed"
[ "$failed" = 0 ]
