# title: a system stops into outer space, switches there, comes back
# pools:
# needs: vm
#
# Plan 7.4, the outer space protocol ruled 2026-10-15: a swap-backed
# memory disk made a pool, the root running now sent into it,
# vfs.root.mountfrom pointed at it, reboot -r. An rc script sent with
# the root, finding / on the memory pool, does a switch's work there:
# it records what it saw as s2probe:* properties on <pool>/s2os,
# snapshots a dataset of the disk pool, sets bootfs to the version
# <pool>/s2os/v1, and reboots for real.
#
# Three runs, each finding its stage from ZFS:
#   1. booted from where it started, nothing seen in outer space yet:
#      whatever an earlier stage 1 left is taken away, all is set up
#      again; then `sudo reboot -r`
#   2. booted from v1: the checks, bootfs back; then reboot
#   3. booted from where it started, outer space seen: the checks,
#      everything taken away
#
#   P4.1  the root sent into the memory pool (answers: the disk's
#         size, the send's seconds)
#   P4.2  vfs.root.mountfrom names the memory pool's root
#   P4.3  in outer space, / was the memory pool's root
#   P4.4  in outer space, a dataset of the disk pool took a snapshot
#   P4.5  in outer space, bootfs was set
#   P4.6  the machine came back up on v1
#   P4.7  answer: hw.usermem in outer space
#   P4.8  back where it started, with the probe's things all gone

RCS=/usr/local/etc/rc.d/s2outer
ZP=s2zram

rc_script() {	# S V
	cat <<EOF
#!/bin/sh
# PROVIDE: s2outer
# REQUIRE: zfs
# KEYWORD: nojail
#
# Installed by probes/P4-outer-space.sh. Does its work only when / is
# on $ZP.

. /etc/rc.subr

name=s2outer
rcvar=s2outer_enable
start_cmd=s2outer_start
stop_cmd=:

s2outer_start()
{
	_root=\$(mount -p | awk '\$2 == "/" { print \$1; exit }')
	case \$_root in
	$ZP/*) ;;
	*) return 0 ;;
	esac
	zfs set s2probe:outer-root="\$_root" $1
	zfs set s2probe:outer-mem="\$(sysctl -n hw.usermem)" $1
	zfs snapshot $2@outer && zfs set s2probe:outer-snap=ok $1
	zpool set bootfs=$2 ${1%%/*} && zfs set s2probe:outer-bootfs=ok $1
	reboot
}

load_rc_config \$name
: \${s2outer_enable:=YES}
run_rc_command "\$1"
EOF
}

# Everything stage 1 makes, taken away, whatever is there of it.
reset() {
	if zpool list -H -o name $ZP >/dev/null 2>&1; then
		m zpool destroy -f $ZP
	fi
	mdconfig -l -v | awk -F '\t' -v l=$ZP '$5 == l { print $1 }' |
	    while read -r md; do
		m mdconfig -d -u "$md"
	done
	if zfs list "$S" >/dev/null 2>&1; then
		m zfs destroy -r "$S"
	fi
	if zfs list "$HOME_BE@s2p4" >/dev/null 2>&1; then
		m zfs destroy "$HOME_BE@s2p4"
	fi
	m rm -f $RCS
}

stage1() {
	HOME_BE=$(booted)
	reset
	m zfs create -o mountpoint=none -o canmount=off \
	    -o s2probe:home="$HOME_BE" "$S"
	rc_script "$S" "$S/v1" >$RCS || die "writing $RCS"
	m chmod 555 $RCS
	m zfs snapshot "$HOME_BE@s2p4"
	m zfs clone -o mountpoint=/ -o canmount=noauto "$HOME_BE@s2p4" "$S/v1"

	ref=$(zfs get -Hp -o value referenced "$HOME_BE@s2p4")
	g=$(((ref * 2 + 1073741823) / 1073741824))
	answer P4.1 "the memory disk" "${g}g for a root of $ref bytes"
	md=$(mdconfig -a -t swap -s "${g}g" -L $ZP) || die "mdconfig"
	m zpool create -o cachefile=none -O mountpoint=none $ZP "/dev/$md"
	m zfs create $ZP/root
	t0=$(date +%s)
	ms "zfs send $HOME_BE@s2p4 | zfs receive -u $ZP/root/default"
	answer P4.1 "the send" "$(($(date +%s) - t0)) seconds"
	check P4.1 "the root on the memory pool" \
	    "$(prop guid $ZP/root/default@s2p4)" "$(prop guid "$HOME_BE@s2p4")"
	m kenv vfs.root.mountfrom="zfs:$ZP/root/default"
	check P4.2 "vfs.root.mountfrom" "$(kenv vfs.root.mountfrom)" \
	    "zfs:$ZP/root/default"
	note "stage 1 of 3 set up: sudo reboot -r; outer space reboots" \
	    "for real by itself; then run P4 again"
}

probe() {
	outside_be
	POOL=$(booted | cut -d/ -f1)
	S=$POOL/s2os
	if [ "$(booted)" = "$S/v1" ]; then
		HOME_BE=$(prop s2probe:home "$S")
		check P4.3 "/ in outer space" "$(prop s2probe:outer-root "$S")" \
		    "$ZP/root/default"
		check P4.4 "a snapshot of the disk pool from outer space" \
		    "$(prop s2probe:outer-snap "$S")" ok
		check P4.4 "the snapshot" \
		    "$(zfs list -H -o name "$S/v1@outer" 2>/dev/null)" "$S/v1@outer"
		check P4.5 "bootfs from outer space" \
		    "$(prop s2probe:outer-bootfs "$S")" ok
		check P4.6 "booted from" "$(booted)" "$S/v1"
		answer P4.7 "hw.usermem in outer space" \
		    "$(prop s2probe:outer-mem "$S")"
		m zpool set bootfs="$HOME_BE" "$POOL"
		note "stage 2 of 3: reboot, then run P4 again"
	elif zfs list "$S" >/dev/null 2>&1 &&
	    [ "$(prop s2probe:outer-root "$S")" != - ]; then
		HOME_BE=$(prop s2probe:home "$S")
		check P4.8 "booted from" "$(booted)" "$HOME_BE"
		reset
		check P4.8 "s2os" "$(zfs list -H -o name "$S" 2>/dev/null)" ""
		check P4.8 "the rc script" "$([ -e $RCS ] && echo there)" ""
		note "stage 3 of 3: done"
	else
		stage1
	fi
}
