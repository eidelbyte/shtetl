#!/bin/sh
# M3 on real pools: shtetl build F, for real, on a throwaway pool: the
# box column of tests/MATRIX.md families F (F11 - F13, F19 among them)
# and P (P17: patches made from the branch's own tree, one already in,
# one applied, one that conflicts and pauses the build, fixed here and
# carried on into the build); then F27, box built again, an update
# over the first, on the obj cache the first left; F33, its packages
# upgraded, the update's -pkg; M8, box beamed to the run root, whole
# and then incremental; X9 and S9, the container shbweb created on
# box, its jail started, stopped and started again, saved again; and
# U9, W7 and K7 (M5): shbup created on box's first version, saved
# with an edit the update conflicts with, updated with the real
# zfs_rebase (which must be installed on shtetl's PATH), paused at the
# gate, answered by hand, taken up and cut over; shbab updated and
# abandoned. Along the way N9: the config snapshot a failed build kept
# recorded again by the next build, nothing having been written to the
# config root, and a new one once a patch is.
# A buildworld and buildkernel take hours the first time; nothing here
# asks anything, so start it and leave it.
#
# What a run takes from the last one: the pool's file stays, exported
# between runs, holding the build root (a clone of SRC) and box's obj
# cache for BRANCH. Each run renames them into the layout it
# bootstraps, a moment within a pool, and back at the end. A build
# root whose files keep their times and an obj tree of the same branch
# make the next buildworld incremental (CLEAN is off by default since
# FreeBSD 14; WITH_META_MODE in /etc/src-env.conf, with filemon
# loaded, makes it surer). Everything else is made anew each run.
#
# usage: make freebsd && sudo sh tests/box/build.sh
#        sudo sh tests/box/build.sh --clean      all of it gone, the
#                                                kept build root and
#                                                obj cache too
#
# Settings, from the environment:
#   BRANCH   the branch to build (releng/15.1)
#   GITURL   where the checkout fetches it (https://git.freebsd.org/src.git)
#   IMGDIR   where the pool's file goes (/var/tmp), on ZFS or UFS
#   SIZE     the pool's file, sparse (80g)
#   SRC      a git checkout of FreeBSD to clone from, shared (/usr/src)
#   SEND     no: the log is not committed and pushed back at the end
#            (tests/box/send.sh), as it is otherwise
#
# It touches only what it makes: the pool shbbuild on a file in
# IMGDIR, imported with an altroot under it, so every mount lands
# there; shtetl's run directories of the foundations it builds,
# /var/db/shtetl/build/box and /var/db/shtetl/build/nope; and the
# containers' jails, JAILS below, their blocks in /etc/jail.conf.d,
# their logs in /var/db/shtetl/create and /var/db/shtetl/update, and
# the rebases' run directories in /var/db/zfs_rebase/shbbuild. A run
# that was stopped is put away first; it will not start while any
# other pool has a shtetl root, or a jail of those names is there.
set -u
cd "$(dirname "$0")/../.." || exit 2
. tests/box/send.sh
BRANCH=${BRANCH:-releng/15.1}
GITURL=${GITURL:-https://git.freebsd.org/src.git}
IMGDIR=${IMGDIR:-/var/tmp}
SIZE=${SIZE:-80g}
SRC=${SRC:-/usr/src}
P=shbbuild
T=$IMGDIR/shtetl-boxbuild
ALT=$T/alt
# kept from run to run: the build root, and box's obj cache per branch
KEEP=$P/keep
OBJKEEP=$KEEP/obj-$(echo "$BRANCH" | tr / _)
OBJ=$P/shtetl/foundation-builds/box
SH=$PWD/shtetl
RUNS=/var/db/shtetl/build
X=shbweb
JAILF=/etc/jail.conf.d/$X.conf
JAILS="shbweb shbup shbup-staging shbab shbab-staging"

# A run's own files, and the run directories of the two foundations
# built here, by name.
runfiles() {
	rm -f "$T/out" "$T/err"
	rm -rf "$T/p17"
	rm -rf /var/db/shtetl/build/box /var/db/shtetl/build/nope
	for _j in $JAILS; do
		rm -rf /var/db/shtetl/create/$_j /var/db/shtetl/update/$_j
	done
	rm -rf /var/db/zfs_rebase/$P
	rmdir $RUNS /var/db/shtetl/create /var/db/shtetl/update \
	    /var/db/shtetl /var/db/zfs_rebase 2>/dev/null
	return 0
}

# The run's layout gone and what is kept put back: box's obj cache,
# and the build root, its tree back to its checkout if a build
# stopped among its patches, a root no longer; the pool exported.
teardown() {
	zpool list -H -o name $P >/dev/null 2>&1 || { runfiles; return 0; }
	# the containers' jails: stopped by their blocks, which go too
	for _j in $JAILS; do
		if jls -j $_j jid >/dev/null 2>&1; then
			if [ -f /etc/jail.conf.d/$_j.conf ]; then
				jail -f /etc/jail.conf.d/$_j.conf -r $_j
			else
				jail -r $_j
			fi
		fi
		rm -f /etc/jail.conf.d/$_j.conf
	done
	# rebases a stopped run left open: their holds would keep
	# what they read from being destroyed
	zfs get -H -r -s local -o name zfs_rebase:phase $P/run 2>/dev/null |
	    while read -r _ds; do
		zfs_rebase --abort "$_ds" >/dev/null 2>&1
	    done
	if zfs list -H -o name $OBJ >/dev/null 2>&1; then
		zfs unmount $OBJ 2>/dev/null
		zfs inherit mountpoint $OBJ
		zfs rename $OBJ $OBJKEEP || zfs destroy -r $OBJ
	fi
	if zfs list -H -o name $P/zbuild >/dev/null 2>&1; then
		if zfs list -H -o name $P/zbuild@shtetl-patch-0 \
		    >/dev/null 2>&1; then
			zfs rollback -r $P/zbuild@shtetl-patch-0
			zfs destroy $P/zbuild@shtetl-patch-0
		fi
		zfs inherit shtetl:space $P/zbuild
		zfs inherit shtetl:build $P/zbuild
		zfs rename $P/zbuild $KEEP/zbuild
	fi
	zfs destroy -R $P/run 2>/dev/null
	zfs destroy -R $P/shtetl 2>/dev/null
	zfs destroy -r $P/zconfig 2>/dev/null
	zpool export $P
	runfiles
}

# Everything, the kept build root and obj caches with it.
clean() {
	if zpool list -H -o name $P >/dev/null 2>&1; then
		zpool destroy -f $P
	fi
	rm -f "$T/$P.img"
	runfiles
	[ -d "$ALT" ] && find "$ALT" -depth -type d -empty -delete
	rmdir "$T" 2>/dev/null
	return 0
}

if [ "${1:-}" = --clean ]; then
	clean
	echo "boxbuild: cleaned"
	exit 0
fi
if [ "$(id -u)" != 0 ]; then
	echo "boxbuild: run as root: sudo sh tests/box/build.sh"
	exit 2
fi
if [ "$(uname -s)" != FreeBSD ]; then
	echo "boxbuild: FreeBSD only"
	exit 2
fi
if [ "$(cat build/.flavor 2>/dev/null)" != freebsd ] || [ ! -x "$SH" ]; then
	echo "boxbuild: make freebsd first"
	exit 2
fi
if zpool list -H -o name $P >/dev/null 2>&1; then
	echo "boxbuild: pool $P imported: a run stopped; put away first"
	teardown
fi
for _j in $JAILS; do
	if jls -j $_j jid >/dev/null 2>&1 ||
	    [ -e /etc/jail.conf.d/$_j.conf ]; then
		echo "boxbuild: a jail $_j, or its block, is there"
		exit 2
	fi
done
_roots=$(zfs get -H -s local -o name,value shtetl:space 2>/dev/null)
if [ -n "$_roots" ]; then
	echo "boxbuild: this system has shtetl roots, which discovery would find:"
	echo "$_roots"
	exit 2
fi
if [ ! -d "$SRC/.git" ]; then
	echo "boxbuild: $SRC is no git checkout of FreeBSD (set SRC=)"
	exit 2
fi
if [ "$(df "$IMGDIR" | awk 'NR == 2 { print $1 }')" = tmpfs ]; then
	echo "boxbuild: $IMGDIR is tmpfs, and the pool writes GBs (set IMGDIR=)"
	exit 2
fi
if [ "$(df -k "$IMGDIR" | awk 'NR == 2 { print $4 }')" -lt 31457280 ]; then
	echo "boxbuild: $IMGDIR has less than 30 GB free (set IMGDIR=)"
	exit 2
fi

pass=0
fail=0

ok() {
	echo "ok   $1"
	pass=$((pass + 1))
}

no() {
	echo "FAIL $1"
	fail=$((fail + 1))
}

check() {	# CELL WHAT GOT WANT
	if [ "$3" = "$4" ]; then
		ok "$1 $2"
	else
		no "$1 $2: got [$3], wanted [$4]"
	fi
}

has() {	# CELL WHAT FILE TEXT
	if grep -qF -- "$4" "$3"; then
		ok "$1 $2"
	else
		no "$1 $2: no [$4] in:"
		sed 's/^/    /' "$3" | tail -30
	fi
}

# shtetl, with no terminal on stdin: RC, $T/out, $T/err
sht() {
	"$SH" "$@" >"$T/out" 2>"$T/err" </dev/null
	RC=$?
	echo "# shtetl $* -> $RC"
}

prop() {
	zfs get -H -o value "$2" "$1" 2>/dev/null
}

# The config root's snapshots whose shtetl:commit is $1.
cfgsnaps() {
	zfs list -H -t snapshot -o name,shtetl:commit -r $P/zconfig |
	    awk -F '\t' -v m="$1" '$2 == m { print $1 }'
}

srcof() {
	zfs get -H -o source "$2" "$1" 2>/dev/null
}

# P17's file, as git's patches say it
PF=share/misc/bsd-family-tree

mkpatch() {	# FROM TO OUT [FILE]: FILE (PF) from FROM to TO, git's way
	_f=${4:-$PF}
	_a=$(git -C "$ALT/zbuild" hash-object -w "$1")
	_b=$(git -C "$ALT/zbuild" hash-object -w "$2")
	{
		echo "diff --git a/$_f b/$_f"
		echo "index $_a..$_b 100644"
		echo "--- a/$_f"
		echo "+++ b/$_f"
		diff -u "$1" "$2" | sed 1,2d
	} >"$3"
}

lookat() {	# DATASET...: each mounted where its mountpoint now says
	for _m; do
		[ "$(prop "$_m" mounted)" = yes ] || zfs mount "$_m"
	done
}

# X9 and S9: the container $X on box, after M8; nothing after a
# create that failed, and nothing written outside its mount.
container() {
	echo "== X9: the container $X on box, its jail; S9: saved again"
	_x=$P/run/containers/$X
	_lx=$P/shtetl/containers/$X
	mkdir -p "$ALT/zconfig/containers"
	cat >"$ALT/zconfig/containers/$X.conf" <<'EOF'
datasets { var { compression = "lz4"; } "srv/www" {} }
commands { hello = "echo shtetl-box > /srv/www/index.txt"; }
jailconf = "allow.raw_sockets;";
EOF
	sht create -y -m "the first" $X --on box
	sed 's/^/    /' "$T/out"
	check X9 "create's exit" "$RC" 0
	if [ "$RC" != 0 ]; then
		sed 's/^/    /' "$T/err"
		echo "skip X9 and S9: no container"
		return 0
	fi
	check X9 "box's datasets cloned at its newest, srv/www its own" \
	    "$(prop $_x origin) $(prop $_x/usr/local origin)" \
	    "$_rf@$_v2-pkg $_rf/usr/local@$_v2-pkg"
	check X9 "srv/www its own, srv a placeholder" \
	    "$(prop $_x/srv/www origin) $(prop $_x/srv canmount)" "- off"
	check X9 "seven datasets" \
	    "$(zfs list -H -o name -r $_x | wc -l | tr -d ' ')" 7
	check X9 "the recipe's lz4 set on the clone, locally" \
	    "$(prop $_x/var compression) $(srcof $_x/var compression)" \
	    "lz4 local"
	check X9 "mounted, the placeholders not" \
	    "$(prop $_x mounted) $(prop $_x/usr mounted)" "yes no"
	check X9 "srv/www mounted, srv not" \
	    "$(prop $_x/srv/www mounted) $(prop $_x/srv mounted)" "yes no"
	check X9 "writable: readonly is box's on zrun, not the clone's" \
	    "$(prop $_x readonly)" off
	_mp=$(prop $_x mountpoint)
	# what is written into the container, never the host's
	case "$_mp" in
	"$ALT"/?*)
		;;
	*)
		no "X9 its mount below the altroot: got [$_mp]"
		return 0
		;;
	esac
	has X9 "the jail's block, its path the mount" $JAILF \
	    "path = \"$_mp\";"
	has X9 "the recipe's jailconf in it" $JAILF "allow.raw_sockets;"
	check X9 "the jail running, its hostname its name" \
	    "$(jls -j $X name 2>/dev/null) $(jexec $X hostname 2>/dev/null)" \
	    "$X $X"
	check X9 "the command ran in the jail" \
	    "$(cat "$_mp/srv/www/index.txt" 2>/dev/null)" shtetl-box
	check F35 "box's external piece ran on the host, into box" \
	    "$(cat "$_mp/etc/shtetl-external" 2>/dev/null)" "box foundation"
	if [ -f /etc/resolv.conf ]; then
		check X9 "the host's resolv.conf put in, an external piece" \
		    "$(cmp -s /etc/resolv.conf "$_mp/etc/resolv.conf" &&
		    echo same)" same
	fi
	_s1=$(zfs list -H -t snapshot -o name -s createtxg $_x | tail -1)
	check X9 "the first save: its kind, the create's config snapshot" \
	    "$(prop "$_s1" shtetl:kind) $(prop "$(prop "$_s1" \
	    shtetl:config)" shtetl:commit | cut -d' ' -f1-2)" "save create $X"
	check X9 "mirrored: a clone of zlake's box" "$(prop $_lx origin)" \
	    "$P/shtetl/foundations/box@$_v2-pkg"
	jail -f $JAILF -r $X >/dev/null 2>&1
	check X9 "stopped by its block, devfs with it" \
	    "$(jls -j $X jid 2>/dev/null)$(mount -p |
	    grep -c "^devfs[[:space:]]*$_mp/dev[[:space:]]")" 0
	jail -f $JAILF -c $X >/dev/null 2>&1
	check X9 "started again from its block" \
	    "$(jls -j $X name 2>/dev/null)" $X
	sht create -y $X --on box
	check X9 "created again: refused" "$RC" 1
	has X9 "for it exists" "$T/err" "container $X exists"
	echo second >"$_mp/srv/www/second.txt"
	sht save -m "the second" $X
	sed 's/^/    /' "$T/out"
	check S9 "the second save's exit" "$RC" 0
	[ "$RC" = 0 ] || sed 's/^/    /' "$T/err"
	check S9 "two saves on zrun" \
	    "$(zfs list -H -t snapshot -o name $_x | grep -c @save-)" 2
	check S9 "both mirrored in zlake" \
	    "$(zfs list -H -t snapshot -o name $_lx | grep -c @save-)" 2
	_s2=$(zfs list -H -t snapshot -o name -s createtxg $_lx | tail -1)
	check S9 "the second's message in zlake, its config carried" \
	    "$(prop "$_s2" shtetl:commit), $(prop "$_s2" shtetl:config)" \
	    "the second, $(prop "$_s1" shtetl:config)"
	check S9 "srv/www's mirror whole, then from its first" \
	    "$(prop $_lx/srv/www origin) $(zfs list -H -t snapshot \
	    -o name $_lx/srv/www | grep -c @save-)" "- 2"
}

# zfs_rebase where shtetl runs it from (its SH_RUN_PATH), or nothing.
zfs_rebase_path() {
	for _d in /sbin /bin /usr/sbin /usr/bin /usr/local/sbin \
	    /usr/local/bin; do
		[ -x "$_d/zfs_rebase" ] && { echo "$_d/zfs_rebase"; return 0; }
	done
	return 1
}

# The resolution at $1: every name still unanswered answered onto.
answer_onto() {
	sed -e 's/ conflict \([0-9]*\) -$/ conflict \1 onto/' \
	    -e 's/^#unanswered [0-9]*$/#unanswered 0/' "$1" >"$1.box" &&
	    mv "$1.box" "$1"
}

# The update of $1 taken through every gate it pauses at, each answered
# onto: RC its last exit; SEEN 1 if a gate's resolution named $2.
update_through() {
	sht update -y "$1"
	sed 's/^/    /' "$T/out"
	SEEN=0
	_n=0
	while [ "$RC" = 3 ] && [ $_n -lt 20 ]; do
		_res=$(sed -n 's/.*answer \(.*\.resolution\), then.*/\1/p' \
		    "$T/out")
		[ -n "$_res" ] || break
		grep -q "$2" "$_res" && SEEN=1
		answer_onto "$_res"
		sht update --continue "$1"
		sed 's/^/    /' "$T/out"
		_n=$((_n + 1))
	done
	[ "$RC" = 0 ] || sed 's/^/    /' "$T/err"
}

# U9, W7, K7 (M5): updates on box, the real zfs_rebase.
updates() {
	echo "== U9, W7, K7: shbup on box's first version, updated, cut over"
	if ! _zr=$(zfs_rebase_path); then
		no "W7 zfs_rebase on shtetl's PATH: install it" \
		    "(zfs_rebase_tool: make freebsd && make install)"
		return 0
	fi
	ok "W7 zfs_rebase at $_zr"
	_u=$P/run/containers/shbup
	_us=$P/run/containers/shbup-staging
	_lu=$P/shtetl/containers/shbup
	cat >"$ALT/zconfig/containers/shbup.conf" <<'EOF'
datasets { "srv/www" {} }
EOF
	cat >"$ALT/zconfig/containers/shbab.conf" <<'EOF'
datasets { "srv/www" {} }
EOF
	sht create -y -m "the first" shbup --on box --version "$_v"
	check U9 "created on box's first version" \
	    "$RC $(prop $_u origin)" "0 $_rf@$_v"
	if [ "$RC" != 0 ]; then
		sed 's/^/    /' "$T/err"
		echo "skip U9 and K7: no container"
		return 0
	fi
	_um=$(prop $_u mountpoint)
	case "$_um" in
	"$ALT"/?*)
		;;
	*)
		no "U9 its mount below the altroot: got [$_um]"
		return 0
		;;
	esac
	# a history: an edit the update meets, then one it does not
	echo "an admin's line" >>"$_um/usr/share/misc/bsd-family-tree"
	echo hello >"$_um/srv/www/index.html"
	sht save -m "the family tree edited" shbup
	check U9 "the second save" "$RC" 0
	echo "another line" >>"$_um/etc/motd.template"
	sht save -m "motd" shbup
	check U9 "the third save" "$RC" 0
	update_through shbup "bsd-family-tree conflict"
	check U9 "paused at the family tree's conflict, answered onto" \
	    "$SEEN" 1
	check U9 "taken up, staged" "$RC" 0
	check U9 "the staging tree on box's newest" "$(prop $_us origin)" \
	    "$_rf@$_v2-pkg"
	_usm=$(prop $_us mountpoint)
	_ft=$_usm/usr/share/misc/bsd-family-tree
	check U9 "the family tree onto's, the update's" \
	    "$(grep -c "an admin's line" "$_ft")" 0
	check U9 "the later edit replayed" \
	    "$(grep -c '^another line$' "$_usm/etc/motd.template")" 1
	check U9 "its own www cloned from the from-save" \
	    "$(cat "$_usm/srv/www/index.html" 2>/dev/null)" hello
	check U9 "the saves replayed, their commits kept" \
	    "$(zfs list -H -t snapshot -o shtetl:commit $_us |
	    grep -c '^motd$')" 1
	check U9 "the staging tree's jail running" \
	    "$(jls -j shbup-staging name 2>/dev/null)" shbup-staging
	check U9 "x stopped meanwhile" "$(jls -j shbup jid 2>/dev/null)" ""
	if [ -s /var/db/shtetl/update/shbup/update.log ]; then
		ok "W7 the tool's output in the update's log"
	else
		no "W7 the tool's output in the update's log"
	fi
	sht cutover -y shbup
	sed 's/^/    /' "$T/out"
	check K7 "cutover's exit" "$RC" 0
	[ "$RC" = 0 ] || sed 's/^/    /' "$T/err"
	check K7 "shbup on box's newest, the staging tree gone" \
	    "$(prop $_u origin) $(zfs list -H -o name $_us 2>/dev/null)" \
	    "$_rf@$_v2-pkg "
	check K7 "its jail running again" "$(jls -j shbup name 2>/dev/null)" \
	    shbup
	check K7 "the old tree archived in zlake, named for its version" \
	    "$(zfs list -H -o name $P/shtetl/archive/containers/shbup-$_v \
	    2>/dev/null)" "$P/shtetl/archive/containers/shbup-$_v"
	check K7 "zlake's shbup on box's newest" "$(prop $_lu origin)" \
	    "$P/shtetl/foundations/box@$_v2-pkg"
	# an abandon
	sht create -y -m "the first" shbab --on box --version "$_v"
	check K7 "shbab created" "$RC" 0
	update_through shbab "-"
	check K7 "shbab updated" "$RC" 0
	sht abandon -y shbab
	check K7 "abandoned: the staging tree gone, shbab running, unchanged" \
	    "$RC $(zfs list -H -o name $P/run/containers/shbab-staging \
	    2>/dev/null)$(jls -j shbab name 2>/dev/null) $(prop \
	    $P/run/containers/shbab origin)" "0 shbab $_rf@$_v"
}

cases() {
	echo "shtetl box build $(date -u +%Y-%m-%dT%H:%M:%SZ) on" \
	    "$(hostname -s), $(uname -r), $(zfs version 2>/dev/null |
	    head -1), shtetl $(git rev-parse --short HEAD 2>/dev/null)," \
	    "$BRANCH"
	mkdir -p "$T" "$ALT" || return 1
	if [ -f "$T/$P.img" ]; then
		echo "== the pool kept from the last run: $T/$P.img"
		zpool import -d "$T" -R "$ALT" $P || return 1
	else
		echo "== a new pool: $T/$P.img"
		truncate -s "$SIZE" "$T/$P.img" || return 1
		zpool create -R "$ALT" -O compression=zstd $P "$T/$P.img" ||
		    return 1
		zfs create -o mountpoint=none -o canmount=off $KEEP || return 1
	fi
	if zfs list -H -o name $KEEP/zbuild >/dev/null 2>&1; then
		echo "== the build root kept: its checkout, its files' times"
		zfs rename $KEEP/zbuild $P/zbuild || return 1
		[ "$(prop $P/zbuild mounted)" = yes ] || zfs mount $P/zbuild ||
		    return 1
	else
		echo "== the checkout: $SRC cloned, shared; origin $GITURL"
		zfs create -o mountpoint=/zbuild $P/zbuild || return 1
		# a checkout another user owns is one git refuses root
		git -c safe.directory='*' clone -q --shared "$SRC" \
		    "$ALT/zbuild" || return 1
		git -C "$ALT/zbuild" remote set-url origin "$GITURL" ||
		    return 1
	fi
	zfs create $P/shtetl
	zfs create $P/run
	zfs create -o mountpoint=/zconfig $P/zconfig

	echo "== bootstrap: lake, run, config, build"
	sht bootstrap -y --lake $P/shtetl --run $P/run --config $P/zconfig \
	    --build $P/zbuild
	check B10 "bootstrap" "$RC" 0
	[ "$RC" = 0 ] || { sed 's/^/    /' "$T/err"; return 1; }
	check B13 "shtetl-recipes cloned by default, from GitHub" \
	    "$(git -C "$ALT/zconfig" remote get-url origin 2>/dev/null)" \
	    https://github.com/eidelbyte/shtetl-recipes.git
	check B13 "its README there" \
	    "$(head -1 "$ALT/zconfig/README.md" 2>/dev/null)" "# shtetl recipes"
	if zfs list -H -o name $OBJKEEP >/dev/null 2>&1; then
		echo "== box's obj cache for $BRANCH kept: an incremental build"
		zfs rename $OBJKEEP $OBJ || return 1
	else
		echo "== no obj cache kept for $BRANCH: a whole build"
	fi

	mkdir -p "$ALT/zconfig/foundations"
	cat >"$ALT/zconfig/shtetl.conf" <<'EOF'
ver = ">=15.1";
kernconf = "GENERIC";
datasets { tmp {} var {} "usr/local" {} }
foundation {
    commands-external {
        mark = <<EOD
echo "$SHTETL_NAME $SHTETL_KIND" > "$SHTETL_ROOT/etc/shtetl-external"
EOD
    }
}
container {
    commands-external {
        resolv = <<EOD
[ ! -f /etc/resolv.conf ] || [ -e "$SHTETL_ROOT/etc/resolv.conf" ] ||
    cp /etc/resolv.conf "$SHTETL_ROOT/etc/resolv.conf"
EOD
    }
}
EOF
	echo "== P17: patches made from $BRANCH's $PF"
	_d=$T/p17
	mkdir -p "$_d/repo" "$ALT/zconfig/patches/box" || return 1
	git -C "$ALT/zbuild" fetch -q origin "$BRANCH" || return 1
	git -C "$ALT/zbuild" cat-file blob "FETCH_HEAD:$PF" >"$_d/base" ||
	    return 1
	# one: a line after the first; two: another there, conflicting
	awk 'NR == 1 { print; print "shtetl-box one"; next } { print }' \
	    "$_d/base" >"$_d/one"
	awk 'NR == 1 { print; print "shtetl-box two"; next } { print }' \
	    "$_d/base" >"$_d/two"
	# in: line 40 put back, which the tree has
	awk 'NR != 40' "$_d/base" >"$_d/less"
	mkpatch "$_d/base" "$_d/one" "$ALT/zconfig/patches/box/1-one.patch"
	mkpatch "$_d/base" "$_d/two" "$ALT/zconfig/patches/box/3-two.patch"
	mkpatch "$_d/less" "$_d/base" "$_d/repo/2-in.patch"
	git -C "$_d/repo" init -q &&
	    git -C "$_d/repo" add -A &&
	    git -C "$_d/repo" -c user.name=box -c user.email=box@localhost \
	    commit -qm in || return 1
	_in=$(git -C "$_d/repo" rev-parse HEAD)
	cat >"$ALT/zconfig/foundations/box.conf" <<EOF
src_branch = "$BRANCH";
buildopts = "WITHOUT_TESTS=yes";
commands { motd = "echo shtetl-box > /etc/motd.template"; }
patches { "file://$_d/repo" {} "patches/box" {} }
EOF
	cat >"$ALT/zconfig/foundations/nope.conf" <<'EOF'
src_branch = "releng/15.99";
EOF

	echo "== F13: a build that fails at the fetch, undone"
	sht build -y nope
	check F13 "exit" "$RC" 1
	has F13 "the refusal" "$T/err" "build of nope failed; log "
	check F13 "F's datasets gone" \
	    "$(zfs list -H -o name $P/shtetl/foundations/nope 2>/dev/null)" ""
	check F13 "the build root's mark cleared" \
	    "$(srcof $P/zbuild shtetl:build)" "-"
	check F13 "the config snapshot kept" \
	    "$(cfgsnaps "build nope" | wc -l | tr -d ' ')" 1
	_cn=$(cfgsnaps "build nope")
	check N9 "the config root atime=off, from bootstrap" \
	    "$(prop $P/zconfig atime)" off
	check F13 "the obj cache kept, unmounted" \
	    "$(prop $P/shtetl/foundation-builds/nope mounted)" no

	echo "== P17: build box: a patch already in, one applied, one paused"
	sht build -y box
	sed 's/^/    /' "$T/out"
	check P17 "the pause's exit" "$RC" 3
	has P17 "already in, from the git source" "$T/out" \
	    "patch 2-in.patch from file://$_d/repo: already in, skipped"
	has P17 "applied, from the directory" "$T/out" \
	    "patch 1-one.patch from patches/box: applied"
	_w="patch 3-two.patch from patches/box: does not apply;"
	has P17 "paused, conflict markers left" "$T/out" \
	    "$_w merged three ways, conflict markers left"
	check P17 "the record" \
	    "$([ -f "$RUNS/box/paused.conf" ] && echo yes)" yes
	check P17 "the build root's mark" "$(prop $P/zbuild shtetl:build)" box
	check P17 "a snapshot before each patch, and the first" \
	    "$(zfs list -H -t snapshot -o name -r $P/zbuild |
	    grep -c '@shtetl-patch-')" 4
	check P17 "the markers in the tree" \
	    "$(grep -c '^<<<<<<< ' "$ALT/zbuild/$PF")" 1
	sht build box --continue
	check P17 "--continue refused while a marker is left" "$RC" 1
	has P17 "naming it" "$T/err" "$PF: conflict marker left"
	# the fix: both lines
	awk 'NR == 1 { print; print "shtetl-box one"; print "shtetl-box two";
	    next } { print }' "$_d/base" >"$ALT/zbuild/$PF"

	echo "== F11, F19, P17: --continue, the build for real (hours)"
	_t0=$(date +%s)
	sht build box --continue
	_t1=$(date +%s)
	echo "# $(( (_t1 - _t0) / 60 )) minutes"
	sed 's/^/    /' "$T/out"
	check F11 "exit" "$RC" 0
	if [ "$RC" != 0 ]; then
		sed 's/^/    /' "$T/err"
		echo "    log: $RUNS/box/build.log (kept)"
		tail -40 "$RUNS/box/build.log" | sed 's/^/    /'
		return 1
	fi
	F=$P/shtetl/foundations/box
	_v=$(zfs list -H -t snapshot -o name -s createtxg $F | tail -1)
	_v=${_v#*@}
	echo "# version $_v"
	case "$_v" in
	[0-9]*-releng-[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]-*|\
	[0-9]*-stable-*|[0-9]*-current-*)
		ok "F12 the version's name" ;;
	*)	no "F12 the version's name: $_v" ;;
	esac
	check F11 "every dataset snapshotted" \
	    "$(zfs list -H -t snapshot -o name -r $F | grep -c "@$_v\$")" 5
	check F11 "shtetl:kind" "$(prop "$F/usr/local@$_v" shtetl:kind)" \
	    version
	_c=$(prop "$F@$_v" shtetl:config)
	check F11 "shtetl:config names the config snapshot" \
	    "$(zfs list -H -o name "$_c" 2>/dev/null)" "$_c"
	check F11 "no shtetl:config-guid" \
	    "$(prop "$F@$_v" shtetl:config-guid)" -
	check N9 "nothing written to zconfig since F13: its snapshot" \
	    "$_c" "$_cn"
	case "$(prop "$F@$_v" shtetl:commit)" in
	"build $BRANCH at "*) ok "F17 shtetl:commit, the build's own" ;;
	*) no "F17 shtetl:commit: $(prop "$F@$_v" shtetl:commit)" ;;
	esac
	check F11 "the placeholder" "$(prop $F/usr canmount)" off
	check F11 "F unmounted" "$(prop $F mounted)" no
	check F11 "its mountpoint back" "$(srcof $F mountpoint)" \
	    "inherited from $P/shtetl"
	check F11 "the obj cache unmounted" \
	    "$(prop $P/shtetl/foundation-builds/box mounted)" no
	check F11 "the build root's mark cleared" \
	    "$(srcof $P/zbuild shtetl:build)" "-"
	_w="file://$_d/repo@$_in: 2-in.patch already;"
	check P17 "shtetl:patches" "$(prop "$F/var@$_v" shtetl:patches)" \
	    "$_w patches/box: 3-two.patch fixed"
	check P17 "the build root rolled back, its checkout clean" \
	    "$(git -C "$ALT/zbuild" status --porcelain | wc -l | tr -d ' ')" 0
	check P17 "every patch snapshot gone" \
	    "$(zfs list -H -t snapshot -o name -r $P/zbuild |
	    grep -c '@shtetl-patch-')" 0
	check P17 "the fix written out" \
	    "$(grep -c '^+shtetl-box two' "$RUNS/box/fixed/2/3-two.patch")" 1
	check P17 "the record gone" \
	    "$([ -f "$RUNS/box/paused.conf" ] && echo yes)" ""

	echo "== what was built, mounted for a look"
	zfs set mountpoint=/look $F
	lookat $F $F/tmp $F/var $F/usr/local
	_r=$ALT/look
	check F19 "the world went into F (installworld)" \
	    "$([ -x "$_r/bin/sh" ] && echo yes)" yes
	check F19 "the kernel (installkernel)" \
	    "$([ -f "$_r/boot/kernel/kernel" ] && echo yes)" yes
	# a module, and its object, for F27 to find kept
	_ko=$(ls "$_r/boot/kernel" | grep '\.ko$' | head -1)
	_ino=$(ls -i "$_r/boot/kernel/$_ko" | awk '{ print $1 }')
	check F19 "/etc (distribution)" \
	    "$([ -f "$_r/etc/rc" ] && echo yes)" yes
	check F11 "the command ran in F" "$(cat "$_r/etc/motd.template")" \
	    shtetl-box
	check P17 "the patched tree built: both lines installed" \
	    "$(grep -c '^shtetl-box ' "$_r/usr/share/misc/bsd-family-tree")" 2
	check F16 "the kernel's ident, BOX" \
	    "$(grep -aq 'BOX' "$_r/boot/kernel/kernel" && echo yes)" yes
	check F11 "usr/local is its own dataset" \
	    "$(df "$_r/usr/local" | awk 'NR == 2 { print $1 }')" "$F/usr/local"
	for _m in $F/usr/local $F/var $F/tmp $F; do
		zfs unmount "$_m"
	done
	zfs inherit mountpoint $F
	check F11 "the run's log kept" \
	    "$([ -s "$RUNS/box/build.log" ] && echo yes)" yes

	echo "== F27: box again, an update over it (the obj cache warm)"
	# the conflicting patch gone from the recipe's directory; init and
	# rtld changed, a string each, so installworld backs up the old
	rm -f "$ALT/zconfig/patches/box/3-two.patch"
	for _f in sbin/init/init.c libexec/rtld-elf/rtld.c; do
		git -C "$ALT/zbuild" cat-file blob "FETCH_HEAD:$_f" \
		    >"$T/p17/b" || return 1
		{ cat "$T/p17/b"; echo 'static const char shtetl_box[]' \
		    '__used = "shtetl box";'; } >"$T/p17/a"
		mkpatch "$T/p17/b" "$T/p17/a" \
		    "$ALT/zconfig/patches/box/4-${_f##*/}.patch" "$_f"
	done
	_t0=$(date +%s)
	sht build -y box
	_t1=$(date +%s)
	echo "# $(( (_t1 - _t0) / 60 )) minutes"
	sed 's/^/    /' "$T/out"
	check F27 "exit" "$RC" 0
	if [ "$RC" != 0 ]; then
		sed 's/^/    /' "$T/err"
		tail -40 "$RUNS/box/build.log" | sed 's/^/    /'
		return 1
	fi
	has F27 "the plan an update" "$T/out" "foundation box, an update of @$_v"
	has F27 "delete-old before the install" "$T/out" \
	    "make delete-old delete-old-libs"
	has F27 "installworld's backups removed: init's" "$T/out" \
	    "sbin/init.bak"
	has F27 "and rtld's" "$T/out" "libexec/ld-elf.so.1.old"
	has F27 "the kernel synced into F" "$T/out" "kernel into $F: "
	_v2=$(zfs list -H -t snapshot -o name -s createtxg $F | tail -1)
	_v2=${_v2#*@}
	echo "# version $_v2"
	case "$_v2" in
	"$_v") no "F27 a new version: still $_v" ;;
	[0-9]*-releng-*|[0-9]*-stable-*|[0-9]*-current-*)
		ok "F27 a new version, on the same datasets" ;;
	*)	no "F27 a new version: $_v2" ;;
	esac
	_c2=$(prop "$F@$_v2" shtetl:config)
	check N9 "a patch written: a config snapshot of the update's" \
	    "$([ "$_c2" != "$_c" ] && prop "$_c2" shtetl:commit)" "update box"
	check F27 "two versions on each dataset" \
	    "$(zfs list -H -t snapshot -o name -r $F/usr/local | wc -l |
	    tr -d ' ')" 2
	check F27 "the first kept" \
	    "$(zfs list -H -o name "$F@$_v" 2>/dev/null)" "$F@$_v"
	zfs set mountpoint=/look $F
	lookat $F $F/tmp $F/var $F/usr/local
	check F27 "no kernel.old" \
	    "$([ -e "$_r/boot/kernel.old" ] && echo there)" ""
	check F27 "no loader backup" \
	    "$(ls "$_r/boot" | grep -c '\.old$')" 0
	check F27 "no ld-elf backup" \
	    "$(ls "$_r/libexec" | grep -c '^ld-elf.*\.so\.1\.old$')" 0
	check F27 "no init.bak" \
	    "$([ -e "$_r/sbin/init.bak" ] && echo there)" ""
	check F27 "an unchanged module keeps its object ($_ko)" \
	    "$(ls -i "$_r/boot/kernel/$_ko" | awk '{ print $1 }')" "$_ino"
	check F27 "the changed init installed" \
	    "$(grep -ac 'shtetl box' "$_r/sbin/init")" 1
	check F27 "the new tree installed: one patch line now" \
	    "$(grep -c '^shtetl-box ' "$_r/usr/share/misc/bsd-family-tree")" 1
	check F27 "the command ran again, over distribution's file" \
	    "$(cat "$_r/etc/motd.template")" shtetl-box
	for _m in $F/usr/local $F/var $F/tmp $F; do
		zfs unmount "$_m"
	done
	zfs inherit mountpoint $F

	echo "== F33: pkg-upgrade box"
	sht pkg-upgrade -y box
	sed 's/^/    /' "$T/out"
	check F33 "exit" "$RC" 0
	[ "$RC" = 0 ] || sed 's/^/    /' "$T/err"
	check F33 "the -pkg of the update's version" \
	    "$(zfs list -H -t snapshot -o name -s createtxg $F | tail -1)" \
	    "$F@$_v2-pkg"
	check F33 "shtetl:kind pkg, on every dataset" \
	    "$(zfs get -H -o value -r -t snapshot shtetl:kind $F |
	    grep -c '^pkg$')" 5
	check F33 "the version's config carried over" \
	    "$(prop "$F@$_v2-pkg" shtetl:config)" \
	    "$(prop "$F@$_v2" shtetl:config)"
	check F33 "F unmounted, the mark cleared" \
	    "$(prop $F mounted) $(srcof $P/zbuild shtetl:build)" "no -"

	echo "== M8: box to zrun, its first version whole, its newest after"
	_rf=$P/run/foundations/box
	sht beam -y --version "$_v" box
	sed 's/^/    /' "$T/out"
	check M8 "the first beam's exit" "$RC" 0
	[ "$RC" = 0 ] || sed 's/^/    /' "$T/err"
	check M8 "each dataset received" \
	    "$(zfs list -H -t snapshot -o name -r $_rf | grep -c "@$_v\$")" 5
	check M8 "readonly, inherited below" \
	    "$(prop $_rf readonly) $(srcof $_rf/usr/local readonly)" \
	    "on inherited from $_rf"
	check M8 "canmount noauto, the placeholder's off" \
	    "$(prop $_rf canmount) $(prop $_rf/usr canmount)" "noauto off"
	check M8 "nothing mounted" \
	    "$(zfs get -H -o value -r mounted $_rf | grep -c yes)" 0
	check M8 "shtetl:* copied, the config too" \
	    "$(prop "$_rf/var@$_v" shtetl:kind) $(prop "$_rf@$_v" shtetl:config)" \
	    "version $(prop "$F@$_v" shtetl:config)"
	sht beam -y box
	sed 's/^/    /' "$T/out"
	check M8 "the later beam's exit" "$RC" 0
	[ "$RC" = 0 ] || sed 's/^/    /' "$T/err"
	check M8 "the newest, the update's -pkg, from the first" \
	    "$(zfs list -H -t snapshot -o name -s createtxg $_rf | tail -1)" \
	    "$_rf@$_v2-pkg"
	check M8 "its kind pkg" "$(prop "$_rf/usr/local@$_v2-pkg" \
	    shtetl:kind)" pkg
	sht beam -y box
	check M8 "beamed again: refused, on zrun already" "$RC" 1

	container
	updates

	echo "== $pass ok, $fail failed"
	[ "$fail" = 0 ]
}

trap 'teardown; exit 2' INT TERM
OUT=tests/box/results/$(hostname -s)-build-$(date -u +%Y%m%dT%H%M%SZ).log
mkdir -p tests/box/results
cases 2>&1 | tee "$OUT"
teardown
_u=${SUDO_USER:-${DOAS_USER:-}}
[ -n "$_u" ] && chown -R "$_u" tests/box/results
box_send "$OUT" build
if grep -q '^FAIL' "$OUT" || ! grep -q '^== [0-9]* ok, 0 failed' "$OUT"; then
	echo "boxbuild: see $OUT"
	exit 1
fi
echo "boxbuild: all passed"
