#!/bin/sh
# M3 on real pools: shtetl build F, for real, on a throwaway pool: the
# box column of tests/MATRIX.md family F (F11 - F13, F19 among them).
# A buildworld and buildkernel take hours; nothing here asks anything,
# so start it and leave it.
#
# usage: make freebsd && sudo sh tests/box/build.sh
#        sudo sh tests/box/build.sh --clean      what a crash left, removed
#
# Settings, from the environment:
#   BRANCH   the branch to build (releng/15.1)
#   GITURL   where the checkout fetches it (https://git.freebsd.org/src.git)
#   IMGDIR   where the pool's file goes (/var/tmp), on ZFS or UFS
#   SIZE     the pool's file, sparse (80g)
#   SRC      a git checkout of FreeBSD to clone from, shared (/usr/src)
#
# It touches only what it makes: the pool shbbuild on a file in
# IMGDIR, imported with an altroot under it, so every mount lands
# there; and shtetl's run directories of the foundations it builds,
# /var/db/shtetl/build/box and /var/db/shtetl/build/nope. It will not
# start while the pool exists, nor while any pool has a shtetl root.
set -u
cd "$(dirname "$0")/../.." || exit 2
BRANCH=${BRANCH:-releng/15.1}
GITURL=${GITURL:-https://git.freebsd.org/src.git}
IMGDIR=${IMGDIR:-/var/tmp}
SIZE=${SIZE:-80g}
SRC=${SRC:-/usr/src}
P=shbbuild
T=$IMGDIR/shtetl-boxbuild
ALT=$T/alt
SH=$PWD/shtetl
RUNS=/var/db/shtetl/build

clean() {
	if zpool list -H -o name $P >/dev/null 2>&1; then
		zpool destroy -f $P
	fi
	rm -f "$T/$P.img" "$T/out" "$T/err"
	[ -d "$ALT" ] && find "$ALT" -depth -type d -empty -delete
	rmdir "$T" 2>/dev/null
	# the run directories of the two foundations built here, by name
	rm -rf /var/db/shtetl/build/box /var/db/shtetl/build/nope
	rmdir $RUNS /var/db/shtetl 2>/dev/null
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
	echo "boxbuild: pool $P exists: sudo sh tests/box/build.sh --clean"
	exit 2
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

cases() {
	echo "shtetl box build $(date -u +%Y-%m-%dT%H:%M:%SZ) on" \
	    "$(hostname -s), $(uname -r), $(zfs version 2>/dev/null |
	    head -1), shtetl $(git rev-parse --short HEAD 2>/dev/null)," \
	    "$BRANCH"
	mkdir -p "$T" "$ALT" || return 1
	truncate -s "$SIZE" "$T/$P.img" || return 1
	zpool create -R "$ALT" -O compression=zstd $P "$T/$P.img" || return 1
	zfs create $P/shtetl
	zfs create -o mountpoint=/zconfig $P/zconfig
	zfs create -o mountpoint=/zbuild $P/zbuild

	echo "== the checkout: $SRC cloned, shared; origin $GITURL"
	# a checkout another user owns is one git refuses root by default
	git -c safe.directory='*' clone -q --shared "$SRC" "$ALT/zbuild" ||
	    return 1
	git -C "$ALT/zbuild" remote set-url origin "$GITURL" || return 1

	echo "== bootstrap: lake, config, build"
	sht bootstrap -y --lake $P/shtetl --config $P/zconfig \
	    --build $P/zbuild
	check B10 "bootstrap" "$RC" 0
	[ "$RC" = 0 ] || { sed 's/^/    /' "$T/err"; return 1; }

	mkdir -p "$ALT/zconfig/foundations"
	cat >"$ALT/zconfig/shtetl.conf" <<'EOF'
ver = ">=15.1";
kernconf = "GENERIC";
datasets { tmp {} var {} "usr/local" {} }
EOF
	cat >"$ALT/zconfig/foundations/box.conf" <<EOF
src_branch = "$BRANCH";
buildopts = "WITHOUT_TESTS=yes";
commands { motd = "echo shtetl-box > /etc/motd.template"; }
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

	echo "== F11, F19: build box, for real (hours)"
	_t0=$(date +%s)
	sht build -y box
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

	echo "== $pass ok, $fail failed"
	[ "$fail" = 0 ]
}

trap 'clean; exit 2' INT TERM
OUT=tests/box/results/$(hostname -s)-build-$(date -u +%Y%m%dT%H%M%SZ).log
mkdir -p tests/box/results
cases 2>&1 | tee "$OUT"
clean
_u=${SUDO_USER:-${DOAS_USER:-}}
[ -n "$_u" ] && chown -R "$_u" tests/box/results
if grep -q '^FAIL' "$OUT" || ! grep -q '^== [0-9]* ok, 0 failed' "$OUT"; then
	echo "boxbuild: see $OUT"
	exit 1
fi
echo "boxbuild: all passed; to send the log back:" \
    "git add tests/box/results &&" \
    "git commit -m 'box: $(hostname -s) build' && git push"
