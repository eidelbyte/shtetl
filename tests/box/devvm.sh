#!/bin/sh
# tests/box/devvm.sh -- shtetl-dev, a VM on this FreeBSD host for
# driving the box tests over ssh: FreeBSD's own ZFS VM image of this
# host's release under bhyve, made as probes/vm.sh makes its scratch
# VM, but bridged to this host's network, with sshd and one user, whose
# ssh key is given and who has sudo without a password. Inside it root
# is its own, its pools, memory disks and jails; this host's are out of
# its reach.
#
#   sudo sh tests/box/devvm.sh make KEY     made, KEY the user's ssh
#                                           public key, a .pub file
#   sudo sh tests/box/devvm.sh run          booted, and booted again
#                                           after each reboot until it
#                                           powers off; its console here
#   sudo sh tests/box/devvm.sh console      its console here again
#   sudo sh tests/box/devvm.sh stop         powered off, as its power
#                                           button would
#   sudo sh tests/box/devvm.sh destroy      gone
#
# The console is left with ~. (~~. through ssh); root logs in there with
# no password. Settings, from the environment, read by make:
#   DEV_USER  the user (claude)
#   DEV_ADDR  its address with a prefix, 192.168.1.50/24; unset, DHCP
#   DEV_GW    its gateway, with DEV_ADDR (this host's default router)
# and by run: NIC, this host's interface it is bridged to (the default
# route's). VMDIR (/var/tmp/shtetl-dev), VM_MEM (16G), VM_CPUS (4),
# VM_SIZE (160G) and VM_REL (this host's release) as probes/vm.sh's.
set -u
# daemon(8) runs the boot loop from /: this script by its whole path
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0") || exit 2
NAME=shtetl-dev
VMDIR=${VMDIR:-/var/tmp/shtetl-dev}
DISK=$VMDIR/disk.raw
MEM=${VM_MEM:-16G}
CPUS=${VM_CPUS:-4}
SIZE=${VM_SIZE:-160G}
REL=${VM_REL:-$(freebsd-version -u | sed 's/-p[0-9]*$//')}
ARCH=$(uname -m)
USR=${DEV_USER:-claude}
FW=/usr/local/share/uefi-firmware/BHYVE_UEFI.fd
VPOOL=s2dev		# the image's pool, zroot, as this host imports it
VALT=$VMDIR/root	# where this host mounts it
CONS=/dev/nmdm-$NAME.1	# its console: bhyve on A, cu on B
LOOP=$VMDIR/boot.pid	# the boot loop's
NET=$VMDIR/net		# the tap and bridge run made, for the loop's end
MAC=58:9c:fc:00:5e:5e	# bhyve's prefix: one lease, boot after boot
MD=

say() { echo "devvm.sh: $*"; }
fail() { say "$*"; exit 1; }

running() {
	[ -e "/dev/vmm/$NAME" ] || pgrep -qF "$LOOP" 2>/dev/null
}

# The image's pool imported by its own disk only, under a name of this
# host's (-t: the disk keeps zroot), its root and /home mounted under
# VALT; vm_close puts it back.
vm_open() {
	running && fail "$NAME is running: sudo sh $0 stop"
	MD=$(mdconfig -a -t vnode -f "$DISK") || fail "mdconfig $DISK"
	_part=$(gpart show -p "$MD" |
	    awk '$4 == "freebsd-zfs" { print $3; exit }')
	if [ -z "$_part" ] || ! zpool import -f -N -t -R "$VALT" \
	    -d "/dev/$_part" zroot "$VPOOL"; then
		mdconfig -d -u "$MD"
		fail "$DISK: its pool zroot not imported"
	fi
	if ! zfs mount "$VPOOL/ROOT/default" || ! zfs mount "$VPOOL/home"; then
		vm_close
		fail "$DISK: its root and /home not mounted"
	fi
}

vm_close() {
	zpool export "$VPOOL" || say "$VPOOL not exported"
	mdconfig -d -u "$MD" || say "$MD not detached"
}

# The user, KEY its key, sudo without a password; sshd by keys only;
# the network; the packages the box tests want.
vm_setup() {	# KEY
	_rc=$VALT/etc/rc.conf
	sysrc -f "$_rc" hostname=$NAME sshd_enable=YES >/dev/null
	if [ -n "${DEV_ADDR:-}" ]; then
		_gw=${DEV_GW:-$(route -n get default |
		    awk '$1 == "gateway:" { print $2 }')}
		sysrc -f "$_rc" ifconfig_vtnet0="inet $DEV_ADDR" \
		    defaultrouter="$_gw" >/dev/null
		cp /etc/resolv.conf "$VALT/etc/resolv.conf"
	else
		sysrc -f "$_rc" ifconfig_vtnet0=DHCP >/dev/null
	fi
	echo 'console="comconsole"' >>"$VALT/boot/loader.conf"
	printf '%s\n' 'PasswordAuthentication no' \
	    'KbdInteractiveAuthentication no' 'PermitRootLogin no' \
	    >>"$VALT/etc/ssh/sshd_config"
	pw -R "$VALT" useradd -n "$USR" -m -G wheel -s /bin/sh \
	    -c "shtetl's box tests" || return 1
	_ids=$(pw -R "$VALT" usershow -n "$USR" | awk -F: '{ print $3 ":" $4 }')
	mkdir -p "$VALT/home/$USR/.ssh" &&
	    cp "$1" "$VALT/home/$USR/.ssh/authorized_keys" &&
	    chmod 700 "$VALT/home/$USR/.ssh" &&
	    chmod 600 "$VALT/home/$USR/.ssh/authorized_keys" &&
	    chown -R "$_ids" "$VALT/home/$USR/.ssh" || return 1
	# the host's ABI is the image's: its packages are the image's
	ASSUME_ALWAYS_YES=yes pkg -r "$VALT" install -q sudo git tmux \
	    libucl >/dev/null || return 1
	mkdir -p "$VALT/usr/local/etc/sudoers.d" &&
	    echo "$USR ALL=(ALL) NOPASSWD: ALL" \
	    >"$VALT/usr/local/etc/sudoers.d/$USR" &&
	    chmod 440 "$VALT/usr/local/etc/sudoers.d/$USR"
}

vm_make() {	# KEY
	_img=FreeBSD-$REL-$ARCH-zfs.raw
	_url=https://download.freebsd.org/releases/VM-IMAGES/$REL/$ARCH/Latest
	[ -n "${1:-}" ] && head -1 "$1" | grep -Eq '^(ssh|ecdsa)-' ||
	    fail "KEY: an ssh public key, a .pub file"
	[ -e "$DISK" ] && fail "$DISK is there: sudo sh $0 destroy"
	[ -e "$FW" ] || fail "no UEFI firmware: pkg install bhyve-firmware"
	mkdir -p "$VMDIR" || exit 2
	fetch -m -o "$VMDIR/$_img.xz" "$_url/$_img.xz" ||
	    fail "$_url/$_img.xz not fetched"
	fetch -o "$VMDIR/CHECKSUM" "$_url/CHECKSUM.SHA256" ||
	    fail "its checksum not fetched"
	_want=$(awk -v f="($_img.xz)" '$2 == f { print $4 }' "$VMDIR/CHECKSUM")
	[ -n "$_want" ] && [ "$(sha256 -q "$VMDIR/$_img.xz")" = "$_want" ] ||
	    fail "$_img.xz: not the image its checksum names"
	say "unpacking $_img"
	unxz -c "$VMDIR/$_img.xz" >"$DISK" || fail "$_img.xz not unpacked"
	rm -f "$VMDIR/$_img.xz" "$VMDIR/CHECKSUM"
	# the image grows its partition and pool to this at its first boot
	truncate -s "$SIZE" "$DISK" || fail "$DISK not grown"
	vm_open
	if ! vm_setup "$1"; then
		vm_close
		fail "not set up: sudo sh $0 destroy, and make again"
	fi
	vm_close
	say "made: sudo sh $0 run"
}

# The boot loop, which run starts under daemon(8): bhyve, and bhyve
# again after each reboot, until the VM powers off; then the tap and
# the bridge run made, gone.
vm_boot() {
	read -r _tap _br <"$NET"
	while :; do
		bhyve -c "$CPUS" -m "$MEM" -H -A -P -s 0,hostbridge \
		    -s 3,virtio-blk,"$DISK" -s 4,virtio-net,"$_tap",mac=$MAC \
		    -s 31,lpc -l com1,"${CONS}A" -l bootrom,"$FW" "$NAME"
		_rc=$?
		bhyvectl --destroy --vm="$NAME" >/dev/null 2>&1
		# bhyve's status: 0 a reboot, 1 off, 2 halted, 3, 4 failed
		[ "$_rc" -eq 0 ] || break
		say "$(date): rebooted: booting it again"
	done
	ifconfig "$_tap" destroy
	ifconfig "$_br" destroy
	rm -f "$NET"
	say "$(date): the VM ended, bhyve's status $_rc"
}

vm_run() {
	running && fail "$NAME is running already: sudo sh $0 console"
	[ -e "$DISK" ] || fail "no VM: sudo sh $0 make KEY"
	_nic=${NIC:-$(route -n get default |
	    awk '$1 == "interface:" { print $2 }')}
	[ -n "$_nic" ] || fail "no NIC: set NIC to the interface to bridge to"
	for _m in vmm nmdm if_bridge; do
		kldstat -q -m "$_m" || kldload "$_m" || fail "$_m not loaded"
	done
	sysctl -q net.link.tap.up_on_open=1 >/dev/null
	_tap=$(ifconfig tap create) || fail "no tap made"
	if ! _br=$(ifconfig bridge create) ||
	    ! ifconfig "$_br" addm "$_nic" addm "$_tap" up; then
		ifconfig "$_tap" destroy
		[ -n "${_br:-}" ] && ifconfig "$_br" destroy
		fail "$_nic: not bridged (a wireless one cannot be)"
	fi
	echo "$_tap $_br" >"$NET"
	daemon -o "$VMDIR/boot.log" -p "$LOOP" sh "$SELF" boot ||
	    fail "its boot loop not begun"
	vm_console
}

vm_console() {
	running || fail "$NAME is not running: sudo sh $0 run"
	say "its console: root, no password; ~. leaves it (~~. through" \
	    "ssh) and the VM runs on"
	cu -l "${CONS}B" -s 115200
}

# ACPI's power button: the VM shuts itself down, and the loop ends.
vm_stop() {
	running || fail "$NAME is not running"
	pkill -TERM -f "bhyve: $NAME" || fail "no bhyve of $NAME"
	say "powering off; the loop ends when it has"
}

vm_destroy() {
	running && fail "$NAME is running: sudo sh $0 stop"
	if zpool list -H -o name "$VPOOL" >/dev/null 2>&1; then
		zpool export "$VPOOL" || fail "$VPOOL not exported"
	fi
	mdconfig -l -v | awk -F '\t' -v f="$DISK" '$4 == f { print $1 }' |
	    while read -r _md; do
		mdconfig -d -u "$_md"
	done
	rm -rf "$VMDIR" && say "$VMDIR gone"
}

[ "$(uname)" = FreeBSD ] || fail "FreeBSD only"
[ "$(id -u)" = 0 ] || fail "needs root (sudo)"
case ${1:-} in
make) vm_make "${2:-}" ;;
run) vm_run ;;
boot) vm_boot ;;
console) vm_console ;;
stop) vm_stop ;;
destroy) vm_destroy ;;
*) sed -n '10,18p' "$0" | sed 's/^# //'; exit 2 ;;
esac
