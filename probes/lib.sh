# probes/lib.sh -- sourced by run.sh; the helpers every probe uses.
# POSIX sh as FreeBSD's /bin/sh runs it. ASCII only.
#
# A probe runs in a subshell of its own, over pools made fresh for
# it and destroyed after it. Everything it prints goes to its log;
# note, check, answer, skip and die also reach the terminal (fd 3)
# and the run's summary.

# ---- names -----------------------------------------------------------

# The probe pools. Distinct from the zrt* pools of zfs_rebase_tool's
# box harnesses, so the two can be run on one box in either order.
RUNP=shprun
LAKEP=shplake
LAKE2P=shplake2
LAKE2ROOT=$LAKE2P/shtetl	# a lake root that is not a pool root (P13)

IMGDIR=${TMPDIR:-/tmp}
MNTROOT=${PROBE_MNT:-${TMPDIR:-/tmp}/shtetl-probe}
PR=$MNTROOT/run		# zrun: $PR/foundations, $PR/containers
BUILD=$MNTROOT/build	# where a zlake foundation is mounted to build
TIMEBIN=${TIMEBIN:-/usr/bin/time}

# ---- the transcript --------------------------------------------------

say() { echo "$*"; }

note() {
	echo "$PROBE $*"
	echo "     $*" >&3
	echo "$PROBE $*" >>"$SUMMARY"
}

answer() { note "answer: $*"; }

check() {	# WHAT GOT WANT
	if [ "$2" = "$3" ]; then
		note "check ok: $1 ($2)"
	else
		note "check DIFFERS: $1: got '$2', want '$3'"
	fi
}

die() { note "ERROR: $*"; exit 3; }
skip() { note "skipped: $*"; exit 4; }

# Arguments as a shell would read them back: one with a space quoted.
quote() {
	for _a; do
		case $_a in
		*[[:space:]]*) printf " '%s'" "$_a" ;;
		*) printf ' %s' "$_a" ;;
		esac
	done
}

# A command, echoed, with its output and a failing status.
x() {
	echo "#$(quote "$@")"
	"$@"
	_rc=$?
	[ "$_rc" -eq 0 ] || echo "-> exit $_rc"
	return "$_rc"
}

# A pipeline or anything with redirections, as one string.
xs() {
	echo "# $1"
	eval "$1"
	_rc=$?
	[ "$_rc" -eq 0 ] || echo "-> exit $_rc"
	return "$_rc"
}

# Setup steps: a failure there is the probe broken, not an answer.
m() { x "$@" || die "setup step failed: $*"; }
ms() { xs "$1" || die "setup step failed: $1"; }

# zfs_rebase, timed. RC is the exit status, T the seconds, ZRBYTES and
# ZRPOOLS what -v said about the bytes compared and the pools pruned.
zr() {
	echo "# zfs_rebase$(quote "$@")"
	$TIMEBIN -p -o "$SCR/time" "$ZR" "$@" >"$SCR/zr.out" 2>"$SCR/zr.err"
	RC=$?
	T=$(awk '$1 == "real" { print $2 }' "$SCR/time")
	cat "$SCR/zr.out" "$SCR/zr.err"
	echo "-> exit $RC in ${T}s"
	ZRBYTES=$(sed -n 's/^zfs_rebase: \([0-9]*\) bytes compared$/\1/p' \
	    "$SCR/zr.err" | tail -1)
	ZRPOOLS=$(sed -n 's/^zfs_rebase: \([0-9]*\) pools unchanged$/\1/p' \
	    "$SCR/zr.err" | tail -1)
	: "${ZRBYTES:=?}" "${ZRPOOLS:=?}"
	return 0
}

# Conflicted names in the resolution beside a -o manifest.
conflicts() {
	if [ -f "$1.resolution" ]; then
		sed -n 's/^#names //p' "$1.resolution"
	else
		echo 0
	fi
}

# Any other command, timed: T is the seconds.
xt() {
	echo "#$(quote "$@")"
	$TIMEBIN -p -o "$SCR/time" "$@"
	_rc=$?
	T=$(awk '$1 == "real" { print $2 }' "$SCR/time")
	echo "-> exit $_rc in ${T}s"
	return "$_rc"
}

mib() { awk -v b="$1" 'BEGIN { printf "%.1f MiB", b / 1048576 }'; }

# ---- pools -----------------------------------------------------------

pool_of() {
	case $1 in
	run) echo $RUNP ;;
	lake) echo $LAKEP ;;
	lake2) echo $LAKE2P ;;
	*) return 1 ;;
	esac
}

pool_md() {
	mdconfig -l -v 2>/dev/null |
	    awk -v f="$IMGDIR/$1.img" '$4 == f { print $1 }'
}

# A pool on a memory disk backed by a sparse file, with the space
# root properties of planning.md 4.3 (mountpoint none, canmount
# noauto, atime off, zstd, aclmode passthrough).
pool_make() {	# POOL SIZE
	_img=$IMGDIR/$1.img
	truncate -s "$2" "$_img" || return 1
	_md=$(mdconfig -a -t vnode -f "$_img") || return 1
	zpool create -o autotrim=on -O mountpoint=none -O canmount=noauto \
	    -O atime=off -O compression=zstd -O aclmode=passthrough \
	    "$1" "/dev/$_md"
}

pool_gone() {	# POOL
	if zpool list -H -o name "$1" >/dev/null 2>&1; then
		zpool destroy -f "$1"
	fi
	for _md in $(pool_md "$1"); do
		mdconfig -d -u "$_md"
	done
	rm -f "$IMGDIR/$1.img"
	rm -rf "/var/db/zfs_rebase/$1"
}

pools_down() {
	for _p in $RUNP $LAKEP $LAKE2P; do
		pool_gone "$_p"
	done
	# Mount points only: find deletes nothing but empty directories.
	[ -d "$MNTROOT" ] && find "$MNTROOT" -depth -type d -empty -delete
	return 0
}

# The layout of planning.md 4.6 and 4.7 on whichever pools were made:
# shtetl:space on each root, foundations and containers below it.
spaces() {	# KINDS
	for _k in $1; do
		case $_k in
		run)
			m zfs set shtetl:space=run $RUNP
			m zfs create -o mountpoint="$PR/foundations" \
			    $RUNP/foundations
			m zfs create -o mountpoint="$PR/containers" \
			    -o canmount=on $RUNP/containers
			;;
		lake)
			lake_root $LAKEP
			;;
		lake2)
			m zfs create $LAKE2ROOT
			lake_root $LAKE2ROOT
			;;
		esac
	done
}

lake_root() {	# ROOT
	m zfs set shtetl:space=lake "$1"
	m zfs create "$1/foundations"
	m zfs create "$1/containers"
	m zfs create -p "$1/archive/containers"
}

# ---- zfs -------------------------------------------------------------

prop() { zfs get -H -o value "$1" "$2"; }
propp() { zfs get -Hp -o value "$1" "$2"; }

show() {	# DS [PROPS]
	x zfs get -r -t filesystem -o name,property,value,source \
	    "${2:-mountpoint,canmount,mounted,readonly,origin}" "$1"
}

snaps() { x zfs list -H -t snapshot -o name,guid,userrefs,clones -r "$1"; }

# Mount a dataset and every child of it that can be mounted.
mount_tree() {	# DS
	for _d in $(zfs list -H -t filesystem -o name -r "$1"); do
		[ "$(prop canmount "$_d")" = off ] && continue
		[ "$(prop mounted "$_d")" = yes ] && continue
		m zfs mount "$_d"
	done
}

umount_tree() {	# DS
	for _d in $(zfs list -H -t filesystem -o name -r "$1" | sort -r); do
		[ "$(prop mounted "$_d")" = yes ] && m zfs unmount "$_d"
	done
	return 0
}

# The sum over a tree of zfs send -nvP -i: SNAP1 and SNAP2 are the
# short names, or FROMTREE gives the origin tree of a clone stream.
sendsize() {	# TREE SNAP1 SNAP2 [FROMTREE]
	_sum=0
	for _d in $(zfs list -H -t filesystem -o name -r "$1"); do
		_rel=${_d#$1}
		_from=${4:-$1}$_rel@$2
		_b=$(zfs send -nvP -i "$_from" "$_d@$3" 2>&1 |
		    awk '$1 == "size" { print $2 }')
		_sum=$((_sum + ${_b:-0}))
	done
	echo "$_sum"
}

# ---- shtetl's moves --------------------------------------------------

# LR is the lake root a helper works in; P13 points it at the second.
lr() { echo "${LR:-$LAKEP}"; }

# A foundation in zlake: F and the children a recipe pre-splits,
# usr a canmount=off placeholder holding usr/local as zroot does (7.1
# step 2).
lake_foundation() {	# F [CHILDREN]
	_f=$(lr)/foundations/$1
	m zfs create "$_f"
	for _c in ${2-usr usr/local var}; do
		if [ "$_c" = usr ]; then
			m zfs create -o canmount=off "$_f/$_c"
		else
			m zfs create "$_f/$_c"
		fi
	done
}

# zlake datasets have mountpoint none; a build mounts F at a build
# path for the install and takes it away again (7.1 steps 3 and 7).
lake_mount() {
	m zfs set mountpoint="$BUILD/$1" "$(lr)/foundations/$1"
	mount_tree "$(lr)/foundations/$1"
}

lake_umount() { m zfs inherit mountpoint "$(lr)/foundations/$1"; }

lake_snap() {	# F V
	m zfs snapshot -r -o shtetl:kind=version -o "shtetl:commit=version $2" \
	    "$(lr)/foundations/$1@$2"
}

# Build F's first version, or the next one over the last (7.1, 7.2).
build_foundation() {	# F V [CHILDREN]
	lake_foundation "$1" "${3-usr usr/local var}"
	lake_mount "$1"
	fill_foundation "$BUILD/$1" "$2"
	lake_umount "$1"
	lake_snap "$1" "$2"
}

update_foundation() {	# F V
	lake_mount "$1"
	next_foundation "$BUILD/$1" "$2"
	lake_umount "$1"
	lake_snap "$1" "$2"
}

# The version to zrun (7.3): per dataset, parent first, full the
# first time and -i from PREV after; readonly, canmount noauto (off
# for a placeholder), the mountpoint inherited from zrun/foundations.
beam() {	# F V [PREV]
	for _d in $(zfs list -H -t filesystem -o name -r "$(lr)/foundations/$1"); do
		_rel=${_d#$(lr)/foundations/$1}
		if [ -n "${3:-}" ]; then
			ms "zfs send -i @$3 $_d@$2 | zfs recv -u $RUNP/foundations/$1$_rel"
			continue
		fi
		_cm=noauto
		[ "$(prop canmount "$_d")" = off ] && _cm=off
		ms "zfs send $_d@$2 | zfs recv -u -o readonly=on -o canmount=$_cm $RUNP/foundations/$1$_rel"
	done
}

fmount() { mount_tree "$RUNP/foundations/$1"; }
fumount() { umount_tree "$RUNP/foundations/$1"; }

# One clone per dataset, parent first, a placeholder kept
# canmount=off (7.4 step 3).
clone_tree() {	# SRCTREE SNAP DSTTREE
	for _d in $(zfs list -H -t filesystem -o name -r "$1"); do
		_rel=${_d#$1}
		_o=
		[ "$(prop canmount "$_d")" = off ] && _o="-o canmount=off"
		m zfs clone $_o "$_d@$2" "$3$_rel"
	done
}

container() { clone_tree "$RUNP/foundations/$2" "$3" "$RUNP/containers/$1"; }

cdir() { echo "$PR/containers/$1"; }

save() {	# X SNAP MSG
	m zfs snapshot -r -o shtetl:kind=save -o "shtetl:commit=$3" \
	    "$RUNP/containers/$1@$2"
}

# X's save to zlake (7.5 step 2): a clone stream from its origin for
# each dataset X inherited, a full stream for a container-only one;
# -i from PREV when given.
mirror() {	# X SNAP [PREV]
	for _d in $(zfs list -H -t filesystem -o name -r "$RUNP/containers/$1"); do
		_rel=${_d#$RUNP/containers/$1}
		_to=$(lr)/containers/$1$_rel
		if [ -n "${3:-}" ]; then
			ms "zfs send -i @$3 $_d@$2 | zfs recv -u $_to"
			continue
		fi
		_o=$(prop origin "$_d")
		if [ "$_o" = - ]; then
			ms "zfs send $_d@$2 | zfs recv -u $_to"
		else
			ms "zfs send -i $_o $_d@$2 | zfs recv -u $_to"
		fi
	done
}

# A clone form result into service (7.6 step 5): readonly off, the
# mountpoint inherited, mounted.
place() {	# DS
	m zfs set readonly=off "$1"
	m zfs inherit mountpoint "$1"
	[ "$(prop canmount "$1")" = on ] || m zfs set canmount=on "$1"
	[ "$(prop mounted "$1")" = yes ] || m zfs mount "$1"
}

# ---- trees -----------------------------------------------------------

# A small foundation: enough files in each dataset for a rebase to
# have work. Paths under var and usr/local land in those datasets.
fill_foundation() {	# DIR VERSION
	mkdir -p "$1/etc" "$1/bin" "$1/lib" "$1/usr/bin" \
	    "$1/usr/share/misc" "$1/var/log" "$1/var/db" "$1/usr/local/etc"
	printf 'hostname="probe"\nsendmail_enable="NONE"\n' >"$1/etc/rc.conf"
	printf '127.0.0.1\tlocalhost\n::1\tlocalhost\n' >"$1/etc/hosts"
	printf 'root:*:0:0::0:0:Charlie &:/root:/bin/sh\n' \
	    >"$1/etc/master.passwd"
	printf 'FreeBSD %s\n' "$2" >"$1/etc/version"
	dd if=/dev/random of="$1/bin/sh" bs=64k count=2 2>/dev/null
	dd if=/dev/random of="$1/lib/libc.so.7" bs=64k count=8 2>/dev/null
	dd if=/dev/random of="$1/usr/bin/vi" bs=64k count=4 2>/dev/null
	printf 'misc\n' >"$1/usr/share/misc/README"
	printf 'log start\n' >"$1/var/log/messages"
	printf 'pkg db\n' >"$1/var/db/README"
	printf 'packagesite: probe\n' >"$1/usr/local/etc/pkg.conf"
}

# What a new version changes (7.2): a program, the version file, a
# new file in each dataset. rc.conf is left alone.
next_foundation() {	# DIR VERSION
	printf 'FreeBSD %s\n' "$2" >"$1/etc/version"
	dd if=/dev/random of="$1/bin/sh" bs=64k count=2 2>/dev/null
	printf 'new in %s\n' "$2" >"$1/usr/share/misc/new-$2"
	[ -d "$1/var/db" ] && printf 'new in %s\n' "$2" >"$1/var/db/new-$2"
	[ -d "$1/usr/local/etc" ] &&
	    printf 'packagesite: probe %s\n' "$2" >"$1/usr/local/etc/pkg.conf"
	return 0
}

# What an admin does in a container between two saves.
admin_edits() {	# X TAG
	_x=$(cdir "$1")
	printf 'admin_%s="YES"\n' "$2" >>"$_x/etc/rc.conf"
	[ -d "$_x/var/log" ] && printf 'admin %s\n' "$2" >"$_x/var/log/admin-$2.log"
	[ -d "$_x/usr/local/etc" ] && printf 'app %s\n' "$2" >"$_x/usr/local/etc/app-$2.conf"
	[ -d "$_x/www" ] && printf '<p>%s</p>\n' "$2" >"$_x/www/index-$2.html"
	return 0
}

# A realistic tree for the probes that time a rebase: host
# directories copied in (PAYLOAD, default usr/include usr/share/man).
# PAYLOAD_DIRS is what was found of them.
payload() {	# DIR
	PAYLOAD_DIRS=
	for _p in ${PAYLOAD:-usr/include usr/share/man}; do
		[ -d "/$_p" ] && PAYLOAD_DIRS="$PAYLOAD_DIRS $_p"
	done
	[ -n "$PAYLOAD_DIRS" ] ||
	    die "none of PAYLOAD (${PAYLOAD:-usr/include usr/share/man}) is on this host"
	ms "tar -cf - -C / $PAYLOAD_DIRS | tar -xpf - -C $1"
}

# One line per object of a tree: inode, ctime, mode, size, path.
# Read it from snapshot directories on both sides, so a child
# dataset's mount point is the same empty directory in both.
objlist() {	# DIR OUT
	(cd "$1" && find . -xdev -print0 | xargs -0 stat -f '%i %c %p %z %N') >"$2"
}

# What became of each object between two listings, the last line the
# counts. kept is what zfs_rebase can prune without reading: same
# object, same ctime. LIMIT caps the names printed of each kind.
objdiff() {	# OLD NEW LABEL [LIMIT]
	awk -v label="$3" -v lim="${4:-30}" '
	function path(s) { sub(/^[^ ]+ [^ ]+ [^ ]+ [^ ]+ /, "", s); return s }
	NR == FNR { p = path($0); oi[p] = $1; oc[p] = $2; next }
	{
		p = path($0); seen[p] = 1
		if (!(p in oi)) { add++; if (add <= lim) print "  added    " p; next }
		if ($1 != oi[p]) { rep++; if (rep <= lim) print "  replaced " p; next }
		if ($2 != oc[p]) { tch++; if (tch <= lim) print "  touched  " p; next }
		kept++
	}
	END {
		for (p in oi)
			if (!(p in seen)) { rem++; if (rem <= lim) print "  removed  " p }
		printf "%s: %d kept, %d touched (same object, new ctime), %d replaced (new object), %d added, %d removed\n", label, kept, tch, rep, add, rem
	}' "$1" "$2"
}

# objlist over every mountable dataset of a tree at SNAP, each path
# written from the tree's top (/etc/hosts, /var/log/messages).
treelist() {	# TREE SNAP OUT
	: >"$3"
	for _tl in $(zfs list -H -t filesystem -o name -r "$1"); do
		[ "$(prop canmount "$_tl")" = off ] && continue
		_tr=${_tl#$1}
		objlist "$(prop mountpoint "$_tl")/.zfs/snapshot/$2" "$SCR/tl"
		sed -e "s| \./| $_tr/|" -e "s| \.\$| ${_tr:-/}|" "$SCR/tl" >>"$3"
	done
}

# Every path of listing A that listing B does not have.
paths_only() {	# A B
	awk 'function path(s) { sub(/^[^ ]+ [^ ]+ [^ ]+ [^ ]+ /, "", s); return s }
	NR == FNR { seen[path($0)] = 1; next }
	!(path($0) in seen) { print path($0) }' "$2" "$1" | sort -u
}

# ---- the installworld probes (--world) -------------------------------

# SRC must hold a source tree with a finished buildworld: these probes
# only installworld from it, into DESTDIRs on the probe pool.
world_src() {
	[ -f "$SRC/Makefile.inc1" ] ||
	    skip "no FreeBSD source tree at $SRC (set SRC=)"
	_sl=$(sysctl -n kern.securelevel)
	[ "$_sl" -le 0 ] ||
	    skip "securelevel $_sl: installworld sets schg flags zfs_rebase could not clear"
	[ "$(df "$IMGDIR" | awk 'NR == 2 { print $1 }')" != tmpfs ] ||
	    skip "$IMGDIR is tmpfs and the world pools write GBs: set TMPDIR to a directory on ZFS or UFS"
	_v=$(sed -n 's/^REVISION="\(.*\)"/\1/p; s/^BRANCH="\(.*\)"/\1/p' \
	    "$SRC/sys/conf/newvers.sh" | paste -sd - -)
	_h=$(git -C "$SRC" rev-parse --short=12 HEAD 2>/dev/null)
	note "source: $SRC, $_v${_h:+ $_h}"
}

# An older release to update from: OLD_BASE, a base.txz on disk, or
# OLD_REL, a release whose base.txz is fetched once and kept.
world_old() {
	if [ -n "${OLD_BASE:-}" ]; then
		OLDTXZ=$OLD_BASE
	elif [ -n "${OLD_REL:-}" ]; then
		_c=${PROBE_CACHE:-/var/cache/shtetl-probes}
		OLDTXZ=$_c/$OLD_REL-base.txz
		if [ ! -f "$OLDTXZ" ]; then
			m mkdir -p "$_c"
			_url=https://download.freebsd.org/releases/$(uname -m)/$(uname -p)/$OLD_REL/base.txz
			x fetch -o "$OLDTXZ.part" "$_url" ||
			    skip "could not fetch $_url"
			m mv "$OLDTXZ.part" "$OLDTXZ"
		fi
	else
		skip "needs an older release: OLD_REL=15.0-RELEASE (fetched) or OLD_BASE=/path/to/base.txz"
	fi
	[ -f "$OLDTXZ" ] || skip "no $OLDTXZ"
	note "older release: $OLDTXZ"
}

# A foundation on zrun, pre-split and writable, to install into.
world_foundation() {	# NAME
	m zfs create "$RUNP/foundations/$1"
	m zfs create -o canmount=off "$RUNP/foundations/$1/usr"
	m zfs create "$RUNP/foundations/$1/usr/local"
	m zfs create "$RUNP/foundations/$1/var"
}

# make in SRC, its output in a log of its own beside the probe's.
world_make() {	# TAG TARGET DESTDIR [ARGS]
	_wl=$OUT/$PROBE-$1.log
	_wt=$2
	_wd=$3
	shift 3
	echo "# make -C $SRC ${WORLD_J:+-j$WORLD_J }$_wt DESTDIR=$_wd $* >$(basename "$_wl")"
	$TIMEBIN -p -o "$SCR/time" make -C "$SRC" ${WORLD_J:+-j$WORLD_J} \
	    "$_wt" DESTDIR="$_wd" "$@" >"$_wl" 2>&1
	_rc=$?
	T=$(awk '$1 == "real" { print $2 }' "$SCR/time")
	echo "-> exit $_rc in ${T}s"
	if [ "$_rc" != 0 ]; then
		tail -40 "$_wl"
		gzip -9f "$_wl"
		die "make $_wt failed (log $(basename "$_wl").gz); installworld needs a finished buildworld in $SRC"
	fi
	gzip -9f "$_wl"
}

# installworld and distribution: 7.1 step 5 short of the kernel.
world_install() {	# TAG DESTDIR [ARGS]
	_wi=$1
	_wdd=$2
	shift 2
	world_make "$_wi-installworld" installworld "$_wdd" "$@"
	_wtw=$T
	world_make "$_wi-distribution" distribution "$_wdd" "$@"
	T=$(awk -v a="$_wtw" -v b="$T" 'BEGIN { print a + b }')
	note "$_wi: installworld and distribution in ${T}s"
}

world_delete_old() {	# TAG DESTDIR
	world_make "$1-delete-old" delete-old "$2" BATCH_DELETE_OLD_FILES=yes
	world_make "$1-delete-old-libs" delete-old-libs "$2" \
	    BATCH_DELETE_OLD_FILES=yes
}

# What an admin does to a whole-world container: a user (pw rewrites
# the passwd files and their .db files), hosts, rc.conf, a config
# under usr/local, a log line.
world_admin() {	# X
	_wx=$(cdir "$1")
	mkdir -p "$_wx/etc" "$_wx/var/log" "$_wx/usr/local/etc"
	m pw -R "$_wx" useradd probe -m -c "probe user" -s /bin/sh
	printf '192.0.2.10\tprobe-peer\n' >>"$_wx/etc/hosts"
	printf 'sshd_enable="YES"\n' >>"$_wx/etc/rc.conf"
	printf 'app\n' >"$_wx/usr/local/etc/app.conf"
	printf 'admin\n' >>"$_wx/var/log/messages"
}

# One rebase per dataset of a tree, parent first (7.6 step 5): the
# placeholder cloned, every other dataset a clone form rebase. A
# rebase stopped at its conflicts gate is left there; the pool goes
# after the probe. TREE_T is the seconds, TREE_C the conflicted
# names, TREE_BAD the rebases that neither finished nor stopped at
# conflicts.
rebase_tree() {	# FROMTREE FROMSNAP ONTOTREE ONTOSNAP RESULTTREE TAG
	TREE_T=0 TREE_C=0 TREE_BAD=0
	for _rt in $(zfs list -H -t filesystem -o name -r "$1"); do
		_rr=${_rt#$1}
		if [ "$(prop canmount "$_rt")" = off ]; then
			m zfs clone -o canmount=off "$3$_rr@$4" "$5$_rr"
			continue
		fi
		_rm=$OUT/$PROBE-$6$(echo "${_rr:-/root}" | tr / -).manifest
		zr -v -o "$_rm" --from "$_rt@$2" --onto "$3$_rr@$4" \
		    --result "$5$_rr"
		_rn=$(conflicts "$_rm")
		note "$6 ${_rr:-/}: exit $RC, ${T}s, $ZRBYTES bytes compared, $ZRPOOLS pools unchanged, ${_rn:-0} conflicted names"
		TREE_T=$(awk -v a="$TREE_T" -v b="$T" 'BEGIN { print a + b }')
		TREE_C=$((TREE_C + ${_rn:-0}))
		case $RC in
		0) ;;
		1) x cat "$_rm.resolution" ;;
		*) TREE_BAD=$((TREE_BAD + 1)) ;;
		esac
	done
}
