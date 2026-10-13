#!/bin/sh
# M3 on real pools: shtetl build F, for real, on a throwaway pool: the
# box column of tests/MATRIX.md families F (F11 - F13, F19 among them)
# and P (P17: patches made from the branch's own tree, one already in,
# one applied, one that conflicts and pauses the build, fixed here and
# carried on into the build); then F27, box built again, an update
# over the first, on the obj cache the first left; and F33, its
# packages upgraded, the update's -pkg.
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
# there; and shtetl's run directories of the foundations it builds,
# /var/db/shtetl/build/box and /var/db/shtetl/build/nope. A run that
# was stopped is put away first; it will not start while any other
# pool has a shtetl root.
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

# A run's own files, and the run directories of the two foundations
# built here, by name.
runfiles() {
	rm -f "$T/out" "$T/err"
	rm -rf "$T/p17"
	rm -rf /var/db/shtetl/build/box /var/db/shtetl/build/nope
	rmdir $RUNS /var/db/shtetl 2>/dev/null
	return 0
}

# The run's layout gone and what is kept put back: box's obj cache,
# and the build root, its tree back to its checkout if a build
# stopped among its patches, a root no longer; the pool exported.
teardown() {
	zpool list -H -o name $P >/dev/null 2>&1 || { runfiles; return 0; }
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
	zfs destroy -r $P/shtetl 2>/dev/null
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

srcof() {
	zfs get -H -o source "$2" "$1" 2>/dev/null
}

# P17's file, as git's patches say it
PF=share/misc/bsd-family-tree

mkpatch() {	# FROM TO OUT: PF from FROM to TO, with git's index line
	_a=$(git -C "$ALT/zbuild" hash-object -w "$1")
	_b=$(git -C "$ALT/zbuild" hash-object -w "$2")
	{
		echo "diff --git a/$PF b/$PF"
		echo "index $_a..$_b 100644"
		echo "--- a/$PF"
		echo "+++ b/$PF"
		diff -u "$1" "$2" | sed 1,2d
	} >"$3"
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
	zfs create -o mountpoint=/zconfig $P/zconfig

	echo "== bootstrap: lake, config, build"
	sht bootstrap -y --lake $P/shtetl --config $P/zconfig \
	    --build $P/zbuild
	check B10 "bootstrap" "$RC" 0
	[ "$RC" = 0 ] || { sed 's/^/    /' "$T/err"; return 1; }
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
	    "$(zfs list -H -t snapshot -o name -r $P/zconfig |
	    grep -c '@build-nope-')" 1
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
	check F11 "shtetl:config-guid is its guid" \
	    "$(prop "$F@$_v" shtetl:config-guid)" "$(prop "$_c" guid)"
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
	for _d in $F $F/tmp $F/var $F/usr/local; do
		zfs mount "$_d"
	done
	_r=$ALT/look
	check F19 "the world went into F (installworld)" \
	    "$([ -x "$_r/bin/sh" ] && echo yes)" yes
	check F19 "the kernel (installkernel)" \
	    "$([ -f "$_r/boot/kernel/kernel" ] && echo yes)" yes
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
	for _d in $F/usr/local $F/var $F/tmp $F; do
		zfs unmount "$_d"
	done
	zfs inherit mountpoint $F
	check F11 "the run's log kept" \
	    "$([ -s "$RUNS/box/build.log" ] && echo yes)" yes

	echo "== F27: box again, an update over it (the obj cache warm)"
	# the conflicting patch gone from the recipe's directory
	rm -f "$ALT/zconfig/patches/box/3-two.patch"
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
	_v2=$(zfs list -H -t snapshot -o name -s createtxg $F | tail -1)
	_v2=${_v2#*@}
	echo "# version $_v2"
	case "$_v2" in
	"$_v") no "F27 a new version: still $_v" ;;
	[0-9]*-releng-*|[0-9]*-stable-*|[0-9]*-current-*)
		ok "F27 a new version, on the same datasets" ;;
	*)	no "F27 a new version: $_v2" ;;
	esac
	check F27 "two versions on each dataset" \
	    "$(zfs list -H -t snapshot -o name -r $F/usr/local | wc -l |
	    tr -d ' ')" 2
	check F27 "the first kept" \
	    "$(zfs list -H -o name "$F@$_v" 2>/dev/null)" "$F@$_v"
	zfs set mountpoint=/look $F
	for _d in $F $F/tmp $F/var $F/usr/local; do
		zfs mount "$_d"
	done
	check F27 "no kernel.old" \
	    "$([ -e "$_r/boot/kernel.old" ] && echo there)" ""
	check F27 "no loader backup" \
	    "$(ls "$_r/boot" | grep -c '\.old$')" 0
	check F27 "no ld-elf backup" \
	    "$(ls "$_r/libexec" | grep -c '^ld-elf.*\.so\.1\.old$')" 0
	check F27 "no init.bak" \
	    "$([ -e "$_r/sbin/init.bak" ] && echo there)" ""
	check F27 "the new tree installed: one patch line now" \
	    "$(grep -c '^shtetl-box ' "$_r/usr/share/misc/bsd-family-tree")" 1
	for _d in $F/usr/local $F/var $F/tmp $F; do
		zfs unmount "$_d"
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
