#!/bin/sh
# tests/box/build.sh -- M3 on the box (m3-box): the daemon, started by
# rc, rolling a real foundation, box, on a pool kept from run to run,
# the box cells of tests/MATRIX.md family R: a create roll from the
# FreeBSD src remote, an update, a patch that conflicts (a stall, the
# foundation at its newest version, zbuild reset), the patch taken
# out and the roll asked again, a pkg roll, and the daemon stopped in
# a command (devfs mounted, the head written) and started again. Among
# the rolls, M4 (m4-box), cells B28 and P24 of families B and P: box
# pulled to the run by hand and a house stand-in's versions S1 - S3
# made on it, beamed to the lake, a stream cut and resumed, and the
# prune at each convergence, the run keeping 1 and the lake 2. After
# them, M5's first container (m5-jail, cells H17, N14, N15 and N17):
# s2web made by shtetl create on box's newest version, its jail run
# from the block the daemon wrote, the daemon stopped and started
# again around it, the jail removed from its block.
# The first build takes hours; nothing here asks anything, so start it
# and leave it.
#
#   make freebsd && sudo sh tests/box/build.sh
#   sudo sh tests/box/build.sh --clean      the pool and its file gone
#
# What a run keeps for the next: the pool's file in IMGDIR, the pool
# exported between runs, holding zbuild's checkout and box's obj cache,
# so later builds are incremental. Each run destroys what the last
# made, box, its copy, the stand-ins and s2web, and writes zconfig
# anew. The daemon runs from /usr/local/sbin, put there by make
# install, and by service(8), as rc runs it. s2web's jail and its block
# in /etc/jail.conf.d are taken away before the pool is exported: the
# daemon's stop leaves a jail running (ruled 2026-11-05), and a jail
# holds the pool busy.
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
RD=$P/run/foundations/$F
LC=$P/lake/containers
RC=$P/run/containers
BEAMLOG=/var/log/shtetl/beam.log
J=s2web
JC=/etc/jail.conf.d/$J.conf

daemon_down() {
	service shtetl onestop >/dev/null 2>&1
	return 0
}

# The pool exported: s2web's jail removed from its block, or without
# it where that fails, and the block gone; then the mounts that are
# not ZFS under the pool, a jail's devfs among them.
pool_export() {
	if jls -j $J >/dev/null 2>&1; then
		jail -f "$JC" -r $J >/dev/null 2>&1 || jail -R $J
	fi
	rm -f "$JC"
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

# The pool, made the first time, imported after; what the last run
# left gone: the house stand-ins, s2web and their copies, clones of
# box's versions, then box's copy on the run, which a new box shares
# nothing with and the beam leaves standing (plan 8.1), then box;
# zconfig written anew, the run keeping 1 version of a house and the
# lake 2; zbuild and the obj caches kept.
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
	for _d in $(for _r in $LC $RC; do
	    zfs list -H -o name -d 1 -t filesystem $_r 2>/dev/null |
	    tail -n +2; done) $RD $FD; do
		if zfs list -H -o name $_d >/dev/null 2>&1; then
			x zfs destroy -r $_d || return 1
		fi
	done
	CONF=$(dir $P/config)
	SRCD=$(dir $P/build)/src
	x rm -rf "$CONF/foundations" "$CONF/containers" "$CONF/patches" \
	    "$CONF/shtetl.conf" &&
	    x mkdir -p "$CONF/foundations" "$CONF/containers" \
	    "$CONF/patches/local" &&
	    printf '%s\n' 'remotes {' "    src = \"$GITURL\";" '}' \
	    'run { keep = 1; }' 'lake { keep = 2; }' >"$CONF/shtetl.conf"
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

# The beam's convergence over: its status, once it is not converging.
# A roll tells the beam as it ends, so after ask the beam has begun.
beamed() {
	while "$SH" status 2>&1 | grep -q '^beam converging'; do
		sleep 5
	done
	"$SH" status 2>&1 | grep '^beam '
}

# box's copy on the run made whole at its version SNAP, a parent first,
# as a house's pull will make it (m5-create): the beam makes none.
pull() {	# SNAP
	for _d in $(zfs list -H -o name -r -t filesystem $FD); do
		xs "zfs send $_d@$1 | zfs receive -u -o canmount=noauto \
		    $RD${_d#$FD}@$1" || return 1
	done
}

# The house stand-in's version SN on the run, a clone of box's copy at
# SNAP, shtetl:house standin; with FILL, 8 MiB written in it, so that a
# stream of it can be cut; its snapshot cN taken last, kind create for
# the first, update after, which makes it complete.
standin() {	# N SNAP [FILL]
	_s=$RC/S$1
	x zfs clone -o shtetl:house=standin -o canmount=noauto $RD@$2 $_s ||
	    return 1
	if [ -n "${3:-}" ]; then
		x zfs mount $_s && xs "dd if=/dev/random of=$(dir $_s)/fill \
		    bs=64k count=128 2>/dev/null" && x zfs unmount $_s ||
		    return 1
	fi
	x zfs snapshot -o shtetl:kind=$([ "$1" = 1 ] && echo create ||
	    echo update) $_s@c$1
}

# SN's stream to the lake cut part way, as probe P1 cuts one: a
# mebibyte of it into receive -s, a receive to resume left there.
breakoff() {	# N SNAP
	xs "zfs send -i $RD@$2 $RC/S$1@c$1 | head -c 1048576 |
	    zfs receive -s -u -o origin=$FD@$2 $LC/S$1@c$1 2>/dev/null"
	return 0
}

# The filesystems below ROOT's top, by name, on one line.
tops() {	# ROOT
	zfs list -H -o name -d 1 -t filesystem "$1" | tail -n +2 |
	    tr '\n' ' '
}

# Whether the snapshot NAME is there: yes or no.
has() {		# NAME
	zfs list -H -o name "$1" >/dev/null 2>&1 && echo yes || echo no
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
	cell R2 "box and usr/local canmount noauto, set there" \
	    "$(zfs get -H -s local -o value canmount $FD $FD/usr/local |
	    tr '\n' ' ')" "noauto noauto "
	cell R2 "its log: the build's step, the version" \
	    "$(grep -c -e '^# build$' -e "^# version ${_v#*@}\$" \
	    "/var/log/shtetl/$F.log")" 2
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

	# B28, P24 (m4-box): the beam and the prune with a house stand-in.
	# Nothing on the run relies on box: no copy of it there. Pulled by
	# hand at its first version, as a house's pull will, and S1 cloned
	# from it before any convergence could prune the copy.
	cell P24 "no house: no copy of box on the run" \
	    "$(beamed) $(has $RD)" "beam converged no"
	_v1=$(newest $FD)
	pull "${_v1#*@}" && standin 1 "${_v1#*@}"

	# R5: an update
	cell R5 "an update roll" "$(ask roll)" \
	    "foundation $F idle $V-DATE-update"
	cell R5 "kind update" "$(prop shtetl:kind "$(newest $FD)")" update
	_v=$(newest $FD)
	_v2=$_v
	# B28: the update beamed onto the pulled copy; S1 to the lake, a
	# clone of box's first version there, arrived, canmount=noauto
	cell B28 "converged" "$(beamed)" "beam converged"
	cell B28 "box's update beamed onto its copy" \
	    "$(prop guid "$RD@${_v2#*@}")" "$(prop guid "$_v2")"
	cell B28 "S1 on the lake, a clone of box's first version" \
	    "$(prop origin $LC/S1) $(prop guid $LC/S1@c1)" \
	    "$_v1 $(prop guid $RC/S1@c1)"
	cell B28 "arrived: its kind there, canmount=noauto set there" \
	    "$(prop shtetl:kind $LC/S1@c1) $(zfs get -H -s local -o value \
	    canmount $LC/S1)" "create noauto"
	cell P24 "S1 the only version: kept; box's versions all kept" \
	    "$(has $RC/S1) $(has "$_v1") $(has "$RD@${_v1#*@}")" "yes yes yes"
	# S2 on the update, 8 MiB in it, its stream to the lake cut
	standin 2 "${_v2#*@}" fill && breakoff 2 "${_v2#*@}"
	cell B28 "S2's stream cut: a receive to resume on the lake" \
	    "$(prop receive_resume_token $LC/S2 | cut -c1-2)" 1-

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
	_v3=$(newest $FD)
	# B28: the cut stream resumed by the next convergence, first
	cell B28 "converged" "$(beamed)" "beam converged"
	cell B28 "S2's receive resumed: no token, S2 there whole" \
	    "$(prop receive_resume_token $LC/S2) $(prop guid $LC/S2@c2)" \
	    "- $(prop guid $RC/S2@c2)"
	cell B28 "the resume in beam.log" \
	    "$(tail -n "+$BEAMFROM" $BEAMLOG | grep -c "^# resume $LC/S2\$")" 1
	# P24: the run keeps 1: S1 pruned, its copy having arrived; box's
	# copy from S2's version on; the lake keeps 2, S1's version with them
	cell P24 "the run: S2 alone, S1 pruned" "$(tops $RC)" "$RC/S2 "
	cell P24 "box's first version gone from the run, kept on the lake" \
	    "$(has "$RD@${_v1#*@}") $(has "$_v1")" "no yes"
	cell P24 "the lake: S1 and S2" "$(tops $LC)" "$LC/S1 $LC/S2 "
	standin 3 "${_v3#*@}"
	# R8: a pkg roll (U7); the version before it read first, as the
	# prune takes it once the beam has carried the new one
	_c=$(prop shtetl:commit "$(newest $FD)")
	cell R8 "a pkg roll" "$(ask pkg)" "foundation $F idle $V-DATE-pkg"
	cell R8 "kind pkg, the version's commit" \
	    "$(prop shtetl:kind "$(newest $FD)") $(prop shtetl:commit \
	    "$(newest $FD)")" "pkg $_c"
	_v=$(newest $FD)
	# P24: S3 beamed; the run keeps S3, the lake S2 and S3; box's first
	# version gone from both spaces, its next from the run, where S3's
	# version is now the oldest any house is on; each pruning said
	cell P24 "converged" "$(beamed)" "beam converged"
	cell P24 "the run: S3 alone; the lake: S2 and S3" \
	    "$(tops $RC)/ $(tops $LC)" "$RC/S3 / $LC/S2 $LC/S3 "
	cell P24 "box's first version gone from both spaces" \
	    "$(has "$_v1") $(has "$RD@${_v1#*@}")" "no no"
	cell P24 "its update: on the lake for S2, gone from the run" \
	    "$(has "$_v2") $(has "$RD@${_v2#*@}")" "yes no"
	_h="$(has "$_v3") $(has "$RD@${_v3#*@}")"
	cell P24 "S3's version and the pkg on both" \
	    "$_h $(has "$_v") $(has "$RD@${_v#*@}")" "yes yes yes yes"
	cell P24 "each pruned in beam.log" \
	    "$(tail -n "+$BEAMFROM" $BEAMLOG | grep '^# prune ' | sort |
	    tr '\n' ' ')" "$(printf '# prune %s\n' "$LC/S1" "$RC/S1" \
	    "$RC/S2" "$RD@${_v1#*@}" "$RD@${_v2#*@}" "$_v1" | sort |
	    tr '\n' ' ')"
	# The stand-ins done with. S3 goes before the daemon is started
	# again (R9): its start would start every house's jail, and a
	# stand-in holds no house.conf. Nothing on the run relies on box
	# then, so the prune takes its copy, and s2web pulls it (H17).
	x zfs destroy -r $RC/S3

	# R9: stopped in a command, devfs mounted, the head written (a
	# marker by commands-external); started again (X8)
	recipe 'commands-external { mark = "touch $SHTETL_ROOT/root/R9"; }' \
	    'commands { slow = "sleep 600"; }'
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
	cell R9 "and the roll's marker in it" \
	    "$(ls "$_head/root/R9" 2>/dev/null)" "$_head/root/R9"
	x service shtetl onestart
	sleep 5
	cell R9 "started again: idle at the newest, no stall" \
	    "$("$SH" status 2>&1 | grep "^foundation $F ")" \
	    "foundation $F idle ${_v#*@}"
	cell R9 "devfs gone" \
	    "$(mount -p | awk -v d="$_head/dev" '$2 == d { print $3 }')" ""
	cell R9 "the head unmounted" "$(prop mounted $FD)" no
	# written, read after a rollback, is 1K, not 0 (build run 1): the
	# marker says what the roll wrote is gone
	cell R9 "what the roll wrote gone" \
	    "$(inhead 'ls root/R9' 2>/dev/null)" ""

	# R10: of all the rolls, syslog heard R6's stall and nothing else
	cell R10 "syslog: one line, the stall" \
	    "$(tail -n "+$LOGFROM" /var/log/daemon.log | grep 'shtetl\[' |
	    sed 's/.*shtetl\[[0-9]*\]: //')" \
	    "foundation $F: patches/local: 2-b.patch: exited 1"
}

# s2web's status line, once it is neither being made nor started; ""
# when it is not there.
house_settled() {
	while :; do
		_l=$("$SH" status 2>&1 | grep "^house $J ")
		case $_l in
		*" creating "*|*" starting "*)
			sleep 5
			;;
		*)
			echo "$_l" | sed -E "s/$DATE/DATE/"
			return 0
			;;
		esac
	done
}

# What is mounted at PATH/dev: its type, or "".
devfs_at() {	# PATH
	mount -p | awk -v d="$1/dev" '$2 == d { print $3 }'
}

# M5's first container (m5-jail), after the rolls: R10 has read syslog.
house() {
	printf '%s\n' \
	    'commands { motd = "echo s2web > /etc/motd.template"; }' \
	    'jailconf = "allow.raw_sockets;";' >"$CONF/containers/$J.conf"
	_f=$(newest $FD)
	# H17: made on box's newest version, its copy on the run pulled
	# whole, the stand-ins gone and the copy pruned with them
	cell H17 "nothing on the run relies on box: no copy of it there" \
	    "$(beamed) $(has $RD)" "beam converged no"
	x "$SH" create $J --on $F
	cell H17 "s2web made on box, active" "$(house_settled)" \
	    "house $J active $V-DATE-$J"
	_h=$(zfs get -H -o name,value -d 1 -t filesystem shtetl:house $RC |
	    awk -v j=$J '$2 == j { print $1 }')
	cell H17 "its root a clone of box's copy at that version" \
	    "$(prop origin "$_h" 2>&1)" "$RD@${_f#*@}"
	_p=$(dir "$_h")
	cell H17 "placed at the run's containers/s2web" "$_p" "$(dir $RC)/$J"
	# N17: the jail made from the block the daemon wrote
	cell N17 "the block: the version's path, the recipe's jailconf" \
	    "$(grep -c -x -e "$(printf '\tpath = "%s";' "$_p")" \
	    -e "$(printf '\tallow.raw_sockets;')" "$JC" 2>&1)" 2
	cell N17 "the jail runs at that path" "$(jls -j $J path 2>&1)" "$_p"
	cell N17 "devfs at its dev" "$(devfs_at "$_p")" devfs
	cell N17 "jexec reaches it: what the chroot's command wrote" \
	    "$(jexec $J cat /etc/motd.template 2>&1)" $J
	cell N17 "pkg -j reaches it: the foundation's package" \
	    "$(pkg -j $J query %n tree 2>&1)" tree
	# N15: the daemon stopped, the jail runs on (ruled 2026-11-05)
	_jid=$(jls -j $J jid 2>&1)
	x service shtetl onestop
	cell N15 "the daemon stopped: the jail runs on" \
	    "$(jls -j $J name jid 2>&1)" "$J $_jid"
	# N14: started again, jail -cm leaves the running jail as it is
	x service shtetl onestart
	sleep 5
	cell N14 "started again: active" "$(house_settled)" \
	    "house $J active $V-DATE-$J"
	cell N14 "the same jail, devfs mounted there once" \
	    "$(jls -j $J jid 2>&1) $(mount -p | awk -v d="$_p/dev" \
	    '$2 == d' | wc -l | tr -d ' ')" "$_jid 1"
	cell N14 "syslog: no line of s2web's" \
	    "$(tail -n "+$LOGFROM" /var/log/daemon.log |
	    grep -c "shtetl\[[0-9]*\]: house $J:")" 0
	# N17: removed from its block, devfs with it
	x jail -f "$JC" -r $J
	_rc=$?
	cell N17 "jail -r: the jail gone, and its devfs" \
	    "$_rc $(jls -j $J jid 2>/dev/null)/$(devfs_at "$_p")" "0 /"
}

mkdir -p "$BD/results" || exit 2
OUT=$BD/results/build-$(date -u +%Y%m%dT%H%M%SZ).log
LOGFROM=$(($(cat /var/log/daemon.log 2>/dev/null | wc -l) + 1))
BEAMFROM=$(($(cat $BEAMLOG 2>/dev/null | wc -l) + 1))
trap 'pool_down; exit 130' INT TERM
{
	echo "shtetl $(git -C "$TOP" rev-parse --short HEAD 2>/dev/null)" \
	    "build on $(hostname -s) $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "host: $(uname -rm)"
	echo "branch: $BRANCH from $GITURL"
	echo
	if x make -C "$TOP" install && pool_up &&
	    x service shtetl onestart; then
		sleep 5
		rolls
		house
	else
		echo "FAIL setup: make install, the pool, the daemon"
	fi
	pool_down
	echo
	# a roll's own output is its log's (m3-logs); syslog has a line
	# for each roll that fails
	echo "syslog, daemon.log from the run's start:"
	tail -n "+$LOGFROM" /var/log/daemon.log | grep 'shtetl\['
	echo
	echo "the last roll's log, /var/log/shtetl/$F.log, its step lines:"
	grep '^# ' "/var/log/shtetl/$F.log"
	echo
	echo "the beam's log, $BEAMLOG, from the run's start:"
	tail -n "+$BEAMFROM" $BEAMLOG
} >"$OUT" 2>&1
ok=$(grep -c '^ok ' "$OUT")
failed=$(grep -c '^FAIL ' "$OUT")
echo "== $ok ok, $failed failed" >>"$OUT"
cat "$OUT"
send "build: $ok ok, $failed failed" "$OUT"
[ "$failed" = 0 ]
