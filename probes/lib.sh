# probes/lib.sh -- sourced by run.sh: what every probe uses.
# POSIX sh as FreeBSD's /bin/sh runs it. ASCII only.
#
# A probe is probes/P<n>-<name>.sh: a header naming its title, the
# pools it wants and what else it needs, and a function probe(). Each
# probe runs in a subshell of its own with its output in its log, on
# pools made for it and destroyed after it. check, answer and die also
# go to the summary and the terminal (fd 3).

RUN=s2run			# zrun's stand-in
LAKE=s2lake			# zlake's stand-in
ALT=${TMPDIR:-/tmp}/shtetl-probes	# the pools' altroot
MDLABEL=shtetl-probe		# the label on every memory disk made

# ---- the record ------------------------------------------------------

note() {
	echo "$PROBE $*"
	echo "$PROBE $*" >>"$SUMMARY"
	echo "     $*" >&3
}

check() {	# CELL WHAT GOT WANT
	if [ "$3" = "$4" ]; then
		note "ok $1 $2"
	else
		note "failed $1 $2: got '$3', want '$4'"
	fi
}

answer() {	# CELL WHAT VALUE: a fact recorded, neither ok nor failed
	note "answer $1 $2: $3"
}

die() {		# a setup step failed: the probe is broken, not answered
	note "failed setup: $*"
	exit 3
}

# ---- commands --------------------------------------------------------

x() {		# CMD...: echoed and run; a failing status shown
	echo "# $*"
	"$@"
	_rc=$?
	[ "$_rc" -eq 0 ] || echo "-> exit $_rc"
	return "$_rc"
}

xs() {		# "COMMANDS": as x, for a pipeline
	echo "# $1"
	eval "$1"
	_rc=$?
	[ "$_rc" -eq 0 ] || echo "-> exit $_rc"
	return "$_rc"
}

m() { x "$@" || die "$*"; }
ms() { xs "$1" || die "$1"; }

prop() { zfs get -H -o value "$1" "$2"; }

# The directory a dataset is mounted at, altroot included.
dir() { prop mountpoint "$1"; }

# ---- a VM's boot ------------------------------------------------------

# The dataset mounted at /: the boot environment running now.
booted() { mount -p | awk '$2 == "/" { print $1; exit }'; }

# A VM probe's results must outlive the boot environment it ran in, so
# the checkout may not be inside it.
outside_be() {
	_fs=$(df "$TOP" | awk 'NR == 2 { print $1 }')
	[ "$_fs" != "$(booted)" ] ||
	    die "the checkout is in the boot environment; clone it under /home"
}

# ---- pools -----------------------------------------------------------

pool_make() {	# NAME [SIZE]
	_md=$(mdconfig -a -t swap -s "${2:-1g}" -L "$MDLABEL") || return 1
	zpool create -R "$ALT" -O mountpoint=none -O atime=off \
	    -O compression=zstd "$1" "/dev/$_md"
}

# Everything a probe or a crash left: the probes' jails and the mounts
# that are not ZFS under the altroot hold the pools busy, so they go
# first; then the pools, zfs_rebase's run directories for them, the
# memory disks by their label, and the empty mount points.
pools_down() {
	for _j in $(jls name | grep '^s2p'); do
		jail -r "$_j"
	done
	mount -p | awk -v a="$ALT/" 'index($2, a) == 1 && $3 != "zfs" {
	    print $2 }' | sort -r | while read -r _mp; do
		umount -f "$_mp"
	done
	for _p in $RUN $LAKE; do
		if zpool list -H -o name "$_p" >/dev/null 2>&1; then
			zpool destroy -f "$_p"
		fi
		rm -rf "/var/db/zfs_rebase/$_p"
	done
	mdconfig -l -v | awk -F '\t' -v l="$MDLABEL" '$5 == l { print $1 }' |
	    while read -r _md; do
		mdconfig -d -u "$_md"
	done
	[ -d "$ALT" ] && find "$ALT" -depth -type d -empty -delete
	return 0
}

# ---- sending the results back ----------------------------------------

# The run's directory committed and pushed by the person who ran the
# script through sudo, with their identity and credentials (su, which
# root takes without a password). SEND=no keeps it local.
send() {	# DIR SUMMARYLINE
	_u=${SUDO_USER:-}
	if [ "${SEND:-yes}" = no ]; then
		echo "probes: SEND=no: $1 not sent"
		return 0
	fi
	if [ -z "$_u" ]; then
		echo "probes: not run through sudo: $1 not sent"
		return 1
	fi
	chown -R "$_u" "$1"
	_g="git -C '$TOP'"
	_msg="probes: $(hostname -s): $2"
	if ! su "$_u" -c "$_g add -- '$1' && $_g commit -q -m '$_msg' -- '$1'"; then
		echo "probes: $1 not committed"
		return 1
	fi
	if su "$_u" -c "$_g pull -q --rebase --autostash && $_g push -q"; then
		echo "probes: sent, $(su "$_u" -c "$_g log -1 --format=%h")"
		return 0
	fi
	echo "probes: committed, not pushed: git pull --rebase && git push"
	return 1
}
