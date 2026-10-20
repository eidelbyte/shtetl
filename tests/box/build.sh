#!/bin/sh
# tests/box/build.sh -- M3 on the box (m3-box): the daemon, started by
# rc, rolling a real foundation, box, on a pool kept from run to run,
# the box cells of tests/MATRIX.md family R: a create roll from the
# FreeBSD src remote, an update, a patch that conflicts (a stall, the
# foundation at its newest version, zbuild reset), the patch taken
# out and the roll asked again, a pkg roll, and the daemon stopped in
# a command (devfs mounted, the head written) and started again.
# The first build takes hours; nothing here asks anything, so start it
# and leave it.
#
#   make freebsd && sudo sh tests/box/build.sh
#   sudo sh tests/box/build.sh --clean      the pool and its file gone
#
# What a run keeps for the next: the pool's file in IMGDIR, the pool
# exported between runs, holding zbuild's checkout and box's obj cache,
# so later builds are incremental. Each run destroys box itself and
# writes zconfig anew. The daemon runs from /usr/local/sbin, put there
# by make install, and by service(8), as rc runs it.
#
# Settings, from the environment:
#   BRANCH   the branch to build (releng/15.1)
#   GITURL   the src remote (https://git.freebsd.org/src.git)
#   IMGDIR   where the pool's file goes (/var/tmp)
#   SIZE     the pool's file, sparse (80g)
#   SEND     no: the log is not committed and pushed back
set -u
BD=$(cd "$(dirname "$0")" && pwd) || exit 2
TOP=$(dirname "$(dirname "$BD")")
. "$TOP/probes/lib.sh"
SENDER=box
BRANCH=${BRANCH:-releng/15.1}
GITURL=${GITURL:-https://git.freebsd.org/src.git}
IMGDIR=${IMGDIR:-/var/tmp}
SIZE=${SIZE:-80g}
P=s2box
IMG=$IMGDIR/$P.img
BALT=$IMGDIR/$P
SH=/usr/local/sbin/shtetl
F=box
FD=$P/lake/foundations/$F

daemon_down() {
	service shtetl onestop >/dev/null 2>&1
	return 0
}

# The pool exported, the mounts that are not ZFS under it first.
pool_export() {
	if zpool list -H -o name $P >/dev/null 2>&1; then
		mount -p | awk -v a="$BALT/" 'index($2, a) == 1 &&
		    $3 != "zfs" { print $2 }' | sort -r | while read -r _mp; do
			umount -f "$_mp"
		done
		zpool export $P
	fi
	return 0
}

pool_down() {
	daemon_down
	pool_export
}

case ${1:-} in
"")
	;;
--clean)
	[ "$(id -u)" = 0 ] || { echo "build.sh: --clean needs root"; exit 2; }
	pool_down
	if zpool import -d "$IMGDIR" $P >/dev/null 2>&1; then
		zpool destroy -f $P
	fi
	rm -f "$IMG"
	exit 0
	;;
*)
	echo "usage: make freebsd && sudo sh tests/box/build.sh"
	echo "       sudo sh tests/box/build.sh --clean"
	exit 2
	;;
esac
[ "$(uname)" = FreeBSD ] || { echo "build.sh: FreeBSD only"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "build.sh: needs root (sudo)"; exit 2; }
[ "$(cat "$TOP/build/.flavor" 2>/dev/null)" = freebsd ] ||
    { echo "build.sh: make freebsd first"; exit 2; }
if service shtetl onestatus >/dev/null 2>&1; then
	echo "build.sh: a shtetl daemon is running: service shtetl onestop"
	exit 2
fi
# a run that was stopped left the pool imported: put away
pool_export
if [ -n "$(zfs get -H -s local -o name shtetl:space 2>/dev/null)" ]; then
	echo "build.sh: this box has shtetl spaces, which the daemon would find"
	exit 2
fi
case $BRANCH in
releng/*)	V=${BRANCH#releng/}-releng ;;
stable/*)	V=${BRANCH#stable/}-stable ;;
*)		echo "build.sh: BRANCH releng/N.M or stable/N"; exit 2 ;;
esac

cell() {	# CELL WHAT GOT WANT
	if [ "$3" = "$4" ]; then
		echo "ok   $1 $2"
	else
		echo "FAIL $1 $2: got [$3], wanted [$4]"
	fi
}

# The pool, made the first time, imported after; box gone, zconfig
# written anew, zbuild and the obj caches kept.
pool_up() {
	if [ ! -f "$IMG" ]; then
		x truncate -s "$SIZE" "$IMG" &&
		x zpool create -R "$BALT" -O mountpoint=/ -O atime=off \
		    -O compression=zstd $P "$IMG" &&
		x zfs create -o shtetl:space=lake $P/lake &&
		x zfs create -o shtetl:space=config $P/config &&
		x zfs create -o shtetl:space=build $P/build &&
		x zfs create -o shtetl:space=run $P/run || return 1
	else
		x zpool import -d "$IMGDIR" -R "$BALT" $P || return 1
	fi
	if zfs list -H -o name $FD >/dev/null 2>&1; then
		x zfs destroy -r $FD || return 1
	fi
	CONF=$(dir $P/config)
	SRCD=$(dir $P/build)/src
	x rm -rf "$CONF/foundations" "$CONF/patches" "$CONF/shtetl.conf" &&
	    x mkdir -p "$CONF/foundations" "$CONF/patches/local" &&
	    printf '%s\n' 'remotes {' "    src = \"$GITURL\";" '}' \
	    >"$CONF/shtetl.conf"
}

# foundations/box.conf: what every run builds, and the lines LINES.
recipe() {	# LINES...
	{
		printf '%s\n' "src_branch = \"$BRANCH\";" \
		    'buildopts = <<EOD' 'WITHOUT_TESTS=yes' \
		    'WITHOUT_LIB32=yes' 'WITHOUT_DEBUG_FILES=yes' 'EOD' \
		    'datasets { "usr/local" {} }' 'pkg = [ "tree" ];'
		for _l; do
			echo "$_l"
		done
	} >"$CONF/foundations/$F.conf"
}

MOTD='commands { motd = "echo shtetl-box > /etc/motd.template"; }'

# box's status line, once it neither waits nor rolls.
settled() {
	while :; do
		_l=$("$SH" status 2>&1 | grep "^foundation $F ")
		case $_l in
		*" waiting"|*" rolling "*)
			sleep 30
			;;
		*)
			echo "$_l"
			return 0
			;;
		esac
	done
}

# shtetl VERB box asked, and waited out: its status line, the version
# named with DATE for its date.
ask() {		# VERB
	if ! "$SH" "$1" $F; then
		echo "shtetl $1 $F refused"
		return 1
	fi
	settled | sed -E "s/$DATE/DATE/"
}

DATE='[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}_[0-9]{2}_[0-9]{2}Z'

newest() {	# DS: its newest snapshot's name
	zfs list -H -t snapshot -d 1 -o name -s createtxg "$1" | tail -1
}

# box's head mounted, and its usr/local; CMD run in it; unmounted.
inhead() {	# CMD
	zfs mount $FD && zfs mount $FD/usr/local || return 1
	(cd "$(dir $FD)" && sh -c "$1")
	_rc=$?
	zfs unmount $FD/usr/local
	zfs unmount $FD
	return $_rc
}

rolls() {
	recipe "$MOTD"
	# R1 - R4: a create roll, by the daemon rc started
	cell R1 "a create roll (F17, GS10)" "$(ask roll)" \
	    "foundation $F idle $V-DATE-create"
	_v=$(newest $FD)
	cell R2 "every dataset snapshotted, kind create" \
	    "$(zfs get -H -r -t snapshot -o value shtetl:kind $FD |
	    sort -u)" create
	cell R2 "shtetl:commit the checkout's" \
	    "$(prop shtetl:commit "$_v")" "$(git -C "$SRCD" rev-parse HEAD)"
	cell R2 "usr a placeholder" "$(prop canmount $FD/usr)" off
	cell R3 "a world and a kernel (K19)" \
	    "$(inhead 'ls bin/sh boot/kernel/kernel' 2>&1 | tr '\n' ' ')" \
	    "bin/sh boot/kernel/kernel "
	cell R3 "the package, in usr/local (X9)" \
	    "$(inhead 'ls usr/local/bin/tree' 2>&1)" usr/local/bin/tree
	cell R3 "the command run in the chroot" \
	    "$(inhead 'cat etc/motd.template' 2>&1)" shtetl-box
	cell R3 "no resolv.conf left" \
	    "$(inhead 'ls etc/resolv.conf' 2>/dev/null)" ""
	cell R4 "foundation.conf schg (F18)" \
	    "$(inhead 'ls -lo var/db/shtetl/foundation.conf' 2>&1 |
	    awk '{ print $1, $5 }')" "-r--r--r-- schg"
	cell R4 "the head unmounted" "$(prop mounted $FD)" no

	# R5: an update
	cell R5 "an update roll" "$(ask roll)" \
	    "foundation $F idle $V-DATE-update"
	cell R5 "kind update" "$(prop shtetl:kind "$(newest $FD)")" update
	_v=$(newest $FD)

	# R6: two patches of one line, the second a conflict
	x sed -i '' '1s/.*/shtetl box a/' "$SRCD/README.md"
	git -C "$SRCD" diff >"$CONF/patches/local/1-a.patch"
	x git -C "$SRCD" checkout -q README.md
	x sed -i '' '1s/.*/shtetl box b/' "$SRCD/README.md"
	git -C "$SRCD" diff >"$CONF/patches/local/2-b.patch"
	x git -C "$SRCD" checkout -q README.md
	recipe "$MOTD" 'patches { "patches/local" {} }'
	cell R6 "a conflict: stalled" "$(ask roll)" \
	    "foundation $F stalled patches/local: 2-b.patch: exited 1"
	cell R6 "shtetl:stall" "$(prop shtetl:stall $FD)" \
	    "patches/local: 2-b.patch: exited 1"
	cell R6 "at its newest version still" "$(newest $FD)" "$_v"
	cell R6 "zbuild reset" "$(git -C "$SRCD" status --porcelain)" ""

	# R7: the patch taken out, the roll asked again
	x rm "$CONF/patches/local/2-b.patch"
	cell R7 "asked again: the stall cleared, rolled" "$(ask roll)" \
	    "foundation $F idle $V-DATE-update"
	cell R7 "the patch in the checkout" \
	    "$(head -1 "$SRCD/README.md")" "shtetl box a"
	cell R7 "no stall" "$(prop shtetl:stall $FD)" -
	_v=$(newest $FD)

	# R8: a pkg roll (U7)
	cell R8 "a pkg roll" "$(ask pkg)" "foundation $F idle $V-DATE-pkg"
	cell R8 "kind pkg, the version's commit" \
	    "$(prop shtetl:kind "$(newest $FD)") $(prop shtetl:commit \
	    "$(newest $FD)")" "pkg $(prop shtetl:commit "$_v")"
	_v=$(newest $FD)

	# R9: stopped in a command, devfs mounted; started again (X8)
	recipe 'commands { slow = "sleep 600"; }'
	x "$SH" roll $F
	_slow="^foundation $F rolling commands: slow"
	until "$SH" status 2>&1 | grep -q "$_slow"; do
		if ! "$SH" status 2>&1 | grep -q "^foundation $F rolling"; then
			echo "R9: box no longer rolling: $("$SH" status 2>&1)"
			break
		fi
		sleep 30
	done
	_head=$(dir $FD)
	x service shtetl onestop
	cell R9 "stopped: devfs left on the head" \
	    "$(mount -p | awk -v d="$_head/dev" '$2 == d { print $3 }')" devfs
	x service shtetl onestart
	sleep 5
	cell R9 "started again: idle at the newest, no stall" \
	    "$("$SH" status 2>&1 | grep "^foundation $F ")" \
	    "foundation $F idle ${_v#*@}"
	cell R9 "devfs gone" \
	    "$(mount -p | awk -v d="$_head/dev" '$2 == d { print $3 }')" ""
	x zpool sync $P
	cell R9 "what the roll wrote gone" "$(prop written $FD)" 0
	cell R9 "the head unmounted" "$(prop mounted $FD)" no
}

mkdir -p "$BD/results" || exit 2
OUT=$BD/results/$(hostname -s)-build-$(date -u +%Y%m%dT%H%M%SZ).log
LOGFROM=$(($(cat /var/log/daemon.log 2>/dev/null | wc -l) + 1))
trap 'pool_down; exit 130' INT TERM
{
	echo "shtetl $(git -C "$TOP" rev-parse --short HEAD 2>/dev/null)" \
	    "box build on $(hostname -s) $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host: $(uname -rm)"
	echo "branch: $BRANCH from $GITURL"
	echo
	if x make -C "$TOP" install && pool_up &&
	    x service shtetl onestart; then
		sleep 5
		rolls
	else
		echo "FAIL setup: make install, the pool, the daemon"
	fi
	pool_down
	echo
	echo "syslog, daemon.log from the run's start:"
	tail -n "+$LOGFROM" /var/log/daemon.log | grep shtetl
} >"$OUT" 2>&1
ok=$(grep -c '^ok ' "$OUT")
failed=$(grep -c '^FAIL ' "$OUT")
echo "== $ok ok, $failed failed" >>"$OUT"
cat "$OUT"
send "$OUT" "build: $ok ok, $failed failed"
[ "$failed" = 0 ]
