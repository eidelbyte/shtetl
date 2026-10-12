#!/bin/sh
# M2 on real pools: shtetl spaces, config show and bootstrap as the
# freebsd build runs them, against two pools on memory disks: the box
# column of tests/MATRIX.md families D and B.
#
# usage: make freebsd && sudo sh tests/box/run.sh
#        sudo sh tests/box/run.sh --clean     what a crash left, removed
#
# It touches only what it makes: the pools shbtank and shbfast, imported
# with an altroot under TMPDIR, so that /containers and every other
# mountpoint lands there and not on this system's own root; the memory
# disks behind their images; and that directory. It will not start
# while either pool exists, nor while any other pool has a shtetl
# root, which discovery would find and the cases would count.
set -u
cd "$(dirname "$0")/../.." || exit 2
T=${TMPDIR:-/tmp}/shtetl-box
ALT=$T/alt
P1=shbtank
P2=shbfast
SH=$PWD/shtetl

pool_md() {
	mdconfig -l -v 2>/dev/null |
	    awk -v f="$T/$1.img" '$4 == f { print $1 }'
}

pool_gone() {
	if zpool list -H -o name "$1" >/dev/null 2>&1; then
		zpool destroy -f "$1"
	fi
	for _md in $(pool_md "$1"); do
		mdconfig -d -u "$_md"
	done
	rm -f "$T/$1.img"
}

clean() {
	pool_gone $P1
	pool_gone $P2
	# what the pools left mounted there: empty directories only
	[ -d "$ALT" ] && find "$ALT" -depth -type d -empty -delete
	rm -rf "$T/repo"
	rm -f "$T/out" "$T/err"
	rmdir "$T" 2>/dev/null
	return 0
}

if [ "${1:-}" = --clean ]; then
	clean
	echo "box: cleaned"
	exit 0
fi
if [ "$(id -u)" != 0 ]; then
	echo "box: run as root: sudo sh tests/box/run.sh"
	exit 2
fi
if [ "$(uname -s)" != FreeBSD ]; then
	echo "box: FreeBSD only"
	exit 2
fi
if [ "$(cat build/.flavor 2>/dev/null)" != freebsd ] || [ ! -x "$SH" ]; then
	echo "box: make freebsd first"
	exit 2
fi
for _p in $P1 $P2; do
	if zpool list -H -o name "$_p" >/dev/null 2>&1; then
		echo "box: pool $_p exists: sudo sh tests/box/run.sh --clean"
		exit 2
	fi
done
_roots=$(zfs get -H -s local -o name,value shtetl:space 2>/dev/null)
if [ -n "$_roots" ]; then
	echo "box: this system has shtetl roots, which discovery would find:"
	echo "$_roots"
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
		sed 's/^/    /' "$3"
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
	echo "shtetl box tests $(date -u +%Y-%m-%dT%H:%M:%SZ) on" \
	    "$(hostname -s), $(uname -r), $(zfs version 2>/dev/null |
	    head -1), shtetl $(git rev-parse --short HEAD 2>/dev/null)"
	mkdir -p "$T" "$ALT" || return 1
	for _p in $P1 $P2; do
		truncate -s 1g "$T/$_p.img" || return 1
		_md=$(mdconfig -a -t vnode -f "$T/$_p.img") || return 1
		zpool create -R "$ALT" "$_p" "/dev/$_md" || return 1
	done
	zfs create $P1/shtetl
	zfs create $P2/run
	zfs create -o mountpoint=/zconfig $P1/zconfig
	zfs create -o mountpoint=/zbuild $P1/zbuild
	GIT=
	for _g in /usr/local/bin/git /usr/bin/git; do
		[ -x "$_g" ] && GIT=$_g
	done
	RECIPES=
	if [ -n "$GIT" ]; then
		"$GIT" init -q "$T/repo" &&
		    printf 'pkg = [ "sudo" ];\n' > "$T/repo/shtetl.conf" &&
		    (cd "$T/repo" && "$GIT" add shtetl.conf &&
		    "$GIT" -c user.name=box -c user.email=box@box commit \
		    -qm recipes) && RECIPES="--recipes $T/repo"
	fi

	echo "== D1: no roots yet"
	sht spaces
	check D1 "exit" "$RC" 0
	check D1 "every space none" \
	    "$(grep -c 'none: shtetl bootstrap makes one' "$T/out")" 4

	echo "== B16, B8: shown, and not confirmed"
	sht bootstrap --lake $P1/shtetl --run $P2/run --config $P1/zconfig \
	    --build $P1/zbuild
	check B16 "exit" "$RC" 1
	has B16 "the refusal" "$T/err" "not confirmed, so nothing done"
	check B16 "nothing marked" "$(prop $P1/shtetl shtetl:space)" "-"
	check B16 "nothing made" "$(zfs list -H -o name -r $P1/shtetl |
	    wc -l | tr -d ' ')" 1
	has B8 "the lake root's table" "$T/out" "$P1/shtetl: a lake root"
	# a memory disk's ashift follows vfs.zfs.vdev.min_auto_ashift
	if grep -qF "pool $P1: ashift 12;" "$T/out"; then
		ok "B8 ashift 12, no warning"
	else
		has B8 "the ashift warning" "$T/out" \
		    "(shtetl recommends 12, which only a new pool can have)"
	fi
	has B8 "autotrim" "$T/out" "autotrim off, set to on"

	echo "== B10, B11, B13, B17: bootstrap, confirmed"
	sht bootstrap -y --lake $P1/shtetl --run $P2/run \
	    --config $P1/zconfig --build $P1/zbuild $RECIPES
	check B10 "exit" "$RC" 0
	[ "$RC" = 0 ] || sed 's/^/    /' "$T/err"
	check B10 "lake mountpoint" "$(prop $P1/shtetl mountpoint)" none
	check B10 "lake canmount" "$(prop $P1/shtetl canmount)" noauto
	check B10 "lake atime" "$(prop $P1/shtetl atime)" off
	check B10 "lake compression" "$(prop $P1/shtetl compression)" zstd
	check B10 "lake aclmode" "$(prop $P1/shtetl aclmode)" passthrough
	check B10 "lake layout" "$(zfs list -H -o name -r $P1/shtetl |
	    wc -l | tr -d ' ')" 8
	check B10 "lake mark" "$(prop $P1/shtetl shtetl:space)" lake
	check B10 "the mark is local" "$(srcof $P1/shtetl shtetl:space)" \
	    local
	check B10 "config mark" "$(prop $P1/zconfig shtetl:space)" config
	check B10 "build mark" "$(prop $P1/zbuild shtetl:space)" build
	check B11 "run mark" "$(prop $P2/run shtetl:space)" run
	check B11 "foundations' mountpoint" \
	    "$(prop $P2/run/foundations mountpoint)" "$ALT/shtetl/foundations"
	check B11 "foundations' canmount" \
	    "$(prop $P2/run/foundations canmount)" off
	check B11 "foundations not mounted" \
	    "$(prop $P2/run/foundations mounted)" no
	check B11 "containers' mountpoint" \
	    "$(prop $P2/run/containers mountpoint)" "$ALT/containers"
	check B11 "containers' canmount" \
	    "$(prop $P2/run/containers canmount)" on
	check B11 "containers mounted" "$(prop $P2/run/containers mounted)" \
	    yes
	check B11 "systems' mountpoint" "$(prop $P2/run/systems mountpoint)" \
	    none
	check B17 "autotrim on $P1" "$(zpool get -H -o value autotrim $P1)" on
	check B17 "autotrim on $P2" "$(zpool get -H -o value autotrim $P2)" on
	if [ -n "$RECIPES" ]; then
		check B13 "the recipes cloned" \
		    "$(cat "$ALT/zconfig/shtetl.conf" 2>/dev/null)" \
		    'pkg = [ "sudo" ];'
		check B13 "a snapshot before the clone" \
		    "$(zfs list -H -t snapshot -o name -r $P1/zconfig |
		    grep -c '@bootstrap-')" 1
	else
		echo "skip B13: no git on this system"
	fi

	echo "== D2, D16: shtetl spaces"
	sht spaces
	check D16 "exit" "$RC" 0
	has D16 "the lake root" "$T/out" "lake    $P1/shtetl"
	has D16 "the config root, mounted" "$T/out" \
	    "config  $P1/zconfig  mounted at $ALT/zconfig"
	has D16 "the build root, mounted" "$T/out" \
	    "build   $P1/zbuild  mounted at $ALT/zbuild"
	has D16 "the run root" "$T/out" "run     $P2/run"

	echo "== D14: the configuration found without -C"
	sht config show
	check D14 "exit" "$RC" 0
	has D14 "the settings" "$T/out" "# the site's settings"

	echo "== B3: a root named again"
	sht bootstrap -y --lake $P1/shtetl
	check B3 "exit" "$RC" 1
	has B3 "the refusal" "$T/err" "$P1/shtetl is a lake root already"

	echo "== B12, D6: a second run root"
	zfs create $P1/run2
	sht bootstrap -y --run $P1/run2
	check B12 "exit" "$RC" 0
	check B12 "containers' canmount" "$(prop $P1/run2/containers canmount)" \
	    off
	check B12 "containers not mounted" \
	    "$(prop $P1/run2/containers mounted)" no
	check B12 "containers below /containers" \
	    "$(prop $P1/run2/containers mountpoint)" "$ALT/containers"
	sht spaces
	has D6 "two run roots, no default" "$T/out" \
	    "no default: shtetl.conf names none as spaces.run.default"

	echo "== B14, B18: layout already there, read below the altroot"
	zfs create $P2/run3
	zfs create -o mountpoint=/containers -o canmount=off $P2/run3/containers
	sht bootstrap -y --run $P2/run3
	check B14 "exit" "$RC" 0
	has B14 "the layout there, taken" "$T/out" \
	    "$P2/run3/containers  mountpoint=/containers, canmount=off (there)"
	check B14 "marked" "$(prop $P2/run3 shtetl:space)" run

	echo "== B6: a child that is not shtetl's"
	zfs create $P2/lake2
	zfs create $P2/lake2/home
	sht bootstrap -y --lake $P2/lake2
	check B6 "exit" "$RC" 1
	has B6 "the refusal" "$T/err" \
	    "$P2/lake2 has $P2/lake2/home, which is not shtetl's layout"

	echo "== D5: two config roots, found"
	zfs create $P2/c2
	zfs set shtetl:space=config $P2/c2
	sht spaces
	check D5 "exit" "$RC" 1
	has D5 "the refusal" "$T/err" "two config roots"
	zfs inherit shtetl:space $P2/c2

	echo "== $pass ok, $fail failed"
	[ "$fail" = 0 ]
}

trap 'clean; exit 2' INT TERM
OUT=tests/box/results/$(hostname -s)-$(date -u +%Y%m%dT%H%M%SZ).log
mkdir -p tests/box/results
cases 2>&1 | tee "$OUT"
clean
_u=${SUDO_USER:-${DOAS_USER:-}}
[ -n "$_u" ] && chown -R "$_u" tests/box/results
if grep -q '^FAIL' "$OUT" || ! grep -q '^== [0-9]* ok, 0 failed' "$OUT"; then
	echo "box: see $OUT"
	exit 1
fi
echo "box: all passed; to send the log back: git add tests/box/results &&" \
    "git commit -m 'box: $(hostname -s) run' && git push"
