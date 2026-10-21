#!/bin/sh
# probes/vm.sh -- the scratch VM that the VM probes, P3 and P4, run in:
# FreeBSD's own ZFS VM image of the host's release, under bhyve on this
# FreeBSD host, with this checkout cloned into its /home and its
# console on this terminal. It needs no network: the results come back
# through its disk.
#
#   sudo sh probes/vm.sh make       made from the release's image
#   sudo sh probes/vm.sh run        booted, its console here, booted
#                                   again after each reboot, until it
#                                   powers off
#   sudo sh probes/vm.sh results    the VM off: its probes' results
#                                   copied here, committed and pushed
#   sudo sh probes/vm.sh destroy    gone
#
# VMDIR (/var/tmp/shtetl-vm), VM_MEM (8G), VM_CPUS (2), VM_SIZE (20G)
# and VM_REL (this host's release) change the defaults; SEND=no keeps
# the results local. What to type in the VM is in README.md.
set -u
PD=$(cd "$(dirname "$0")" && pwd) || exit 2
TOP=$(dirname "$PD")
. "$PD/lib.sh"

NAME=shtetl-vm
VMDIR=${VMDIR:-/var/tmp/shtetl-vm}
DISK=$VMDIR/disk.raw
MEM=${VM_MEM:-8G}
CPUS=${VM_CPUS:-2}
SIZE=${VM_SIZE:-20G}
REL=${VM_REL:-$(freebsd-version -u | sed 's/-p[0-9]*$//')}
ARCH=$(uname -m)
FW=/usr/local/share/uefi-firmware/BHYVE_UEFI.fd
VPOOL=s2vm		# the image's pool, zroot, as this host imports it
VALT=$VMDIR/root	# where this host mounts it
MD=

say() { echo "vm.sh: $*"; }
fail() { say "$*"; exit 1; }

running() { [ -e "/dev/vmm/$NAME" ]; }

# The image's pool imported by its own disk only, under a name of this
# host's (-t: the disk keeps zroot), its root and /home mounted under
# VALT. vm_close puts it back.
vm_open() {
	running && fail "$NAME is running: poweroff in it first"
	[ -e "$DISK" ] || fail "no VM: sudo sh probes/vm.sh make"
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

vm_make() {
	_img=FreeBSD-$REL-$ARCH-zfs.raw
	_url=https://download.freebsd.org/releases/VM-IMAGES/$REL/$ARCH/Latest
	[ -e "$DISK" ] && fail "$DISK is there: sudo sh probes/vm.sh destroy"
	[ -e "$FW" ] || fail "no UEFI firmware: pkg install bhyve-firmware"
	mkdir -p "$VMDIR" || exit 2
	# -m: an image a run before fetched is not fetched again
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
	# its name, which the results are filed under; its console the
	# serial port, this terminal; this checkout where a boot
	# environment's switch leaves it be, and git, for the commit line
	sysrc -f "$VALT/etc/rc.conf" hostname=shtetlvm >/dev/null
	echo 'console="comconsole"' >>"$VALT/boot/loader.conf"
	if ! git -c safe.directory='*' clone -q "$TOP" "$VALT/home/shtetl"; then
		vm_close
		fail "$TOP not cloned into the VM"
	fi
	pkg -r "$VALT" install -qy git-lite >/dev/null ||
	    say "git-lite not installed: the VM's summaries lack the commit"
	say "the VM has $(git -C "$TOP" log -1 --format='%h %s')"
	vm_close
	say "made: sudo sh probes/vm.sh run"
}

vm_run() {
	running && fail "$NAME is running already"
	[ -e "$DISK" ] || fail "no VM: sudo sh probes/vm.sh make"
	kldstat -q -m vmm || kldload vmm || fail "vmm not loaded"
	say "its console follows: root, no password; poweroff ends it" \
	    "(or, elsewhere, sudo bhyvectl --vm=$NAME --force-poweroff)"
	while :; do
		bhyve -c "$CPUS" -m "$MEM" -H -A -P -s 0,hostbridge \
		    -s 3,virtio-blk,"$DISK" -s 31,lpc -l com1,stdio \
		    -l bootrom,"$FW" "$NAME"
		_rc=$?
		bhyvectl --destroy --vm="$NAME" >/dev/null 2>&1
		# bhyve's status: 0 a reboot, 1 off, 2 halted, 3, 4 failed
		[ "$_rc" -eq 0 ] || break
		say "rebooted: booting it again"
	done
	say "the VM ended, bhyve's status $_rc"
	[ "$_rc" -le 2 ]
}

vm_results() {
	vm_open
	_n=0
	_ok=0
	_bad=0
	for _d in "$VALT/home/shtetl/probes/results"/*; do
		_b=$(basename "$_d")
		[ -d "$_d" ] && [ ! -e "$PD/results/$_b" ] || continue
		cp -Rp "$_d" "$PD/results/$_b" || continue
		set -- $(sed -n 's/^== \([0-9]*\) ok, \([0-9]*\) failed$/\1 \2/p' \
		    "$_d/summary.txt") 0 0
		_n=$((_n + 1))
		_ok=$((_ok + $1))
		_bad=$((_bad + $2))
		say "$_b: $1 ok, $2 failed"
	done
	vm_close
	[ "$_n" -gt 0 ] || fail "no results in the VM that are not here"
	send "$PD/results" "VM: $_n runs, $_ok ok, $_bad failed"
}

vm_destroy() {
	running && fail "$NAME is running: poweroff in it first"
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
make) vm_make ;;
run) vm_run ;;
results) vm_results ;;
destroy) vm_destroy ;;
*) sed -n '8,14p' "$0" | sed 's/^# //'; exit 2 ;;
esac
