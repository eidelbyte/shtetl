# title: a container's active version is the one mounted at its path
# pools: run
# needs: -
#
# Plan 7.1 and 7.3 (ruled 2026-10-18: only one version of a house is
# mounted, the active one, and a roll swaps the mounted dataset). Two
# versions of a container H, each a root with a var child, every
# dataset canmount=noauto. The active one has its root's mountpoint at
# /containers/H; the other keeps the one its name gives it and is
# mounted there only while a roll builds it.
#
#   P2.1  with every dataset canmount=noauto, the pool exported and
#         imported again, as at a boot, mounts neither version
#   P2.2  v1 made active: root and var mounted at /containers/H and
#         /containers/H/var (answer: whether setting the mountpoint
#         mounted it)
#   P2.3  a jail on /containers/H, with devfs, runs on v1
#   P2.4  as during a roll: v2 mounted at its own path while v1 is at H
#   P2.5  the swap, the jail stopped: v1 unmounted with its mountpoint
#         put back, v2's set to /containers/H; v2's root and var there
#         (answer: whether libzfs moved the mounted v2 by itself)
#   P2.6  the jail started again runs on v2, and v1 is mounted nowhere
#   P2.7  answer: what ZFS does when v1 is mounted at H over v2

J=s2p2

version() {	# V: a root with enough to run sh and a var, its name in each
	m zfs create -o canmount=noauto $RUN/containers/$1
	m zfs create -o canmount=noauto $RUN/containers/$1/var
	mount_tree $RUN/containers/$1
	_d=$(dir $RUN/containers/$1)
	m mkdir -p "$_d/bin" "$_d/dev"
	m cp /rescue/sh "$_d/bin/sh"
	echo "$1" >"$_d/which"
	echo "$1" >"$_d/var/which"
	umount_tree $RUN/containers/$1
}

mount_tree() {	# DS: it and its children, parent first
	for _d in $(zfs list -H -o name -r "$1"); do
		[ "$(prop mounted "$_d")" = yes ] || m zfs mount "$_d"
	done
}

umount_tree() {	# DS: its children first
	for _d in $(zfs list -H -o name -r "$1" | sort -r); do
		[ "$(prop mounted "$_d")" = no ] || m zfs unmount "$_d"
	done
}

where() {	# DS: where it is mounted, or "no"
	[ "$(prop mounted "$1")" = yes ] && dir "$1" || echo no
}

inside() { jexec $J /bin/sh -c 'read a </which; read b </var/which;
    echo "$a $b"'; }

devfs_at() {
	mount -p | awk -v c="$RC/" '$3 == "devfs" && index($2, c) == 1 {
	    print $2 }'
}

# Mountpoints are set relative to the pool's altroot (/containers/H)
# and read back with it ($C/H).
probe() {
	m zfs create -o mountpoint=/containers $RUN/containers
	C=$(dir $RUN/containers)
	RC=$(realpath "$C")
	version v1
	version v2

	m zpool export $RUN
	m zpool import -R "$ALT" -d /dev $RUN
	check P2.1 "versions mounted after an import" "$(zfs list -H \
	    -o mounted -r $RUN/containers/v1 $RUN/containers/v2 |
	    grep -c yes)" 0

	m zfs set mountpoint=/containers/H $RUN/containers/v1
	answer P2.2 "mounted by setting the mountpoint" \
	    "$(prop mounted $RUN/containers/v1)"
	mount_tree $RUN/containers/v1
	check P2.2 "v1 at" "$(where $RUN/containers/v1)" "$C/H"
	check P2.2 "v1/var at" "$(where $RUN/containers/v1/var)" "$C/H/var"

	m jail -c name=$J path="$C/H" mount.devfs persist
	check P2.3 "inside" "$(inside)" "v1 v1"
	check P2.3 "devfs at" "$(devfs_at)" "$RC/H/dev"

	mount_tree $RUN/containers/v2
	check P2.4 "v2 at, during a roll" "$(where $RUN/containers/v2)" \
	    "$C/v2"
	check P2.4 "v1 still at" "$(where $RUN/containers/v1)" "$C/H"

	m jail -r $J
	check P2.5 "devfs after jail -r" "$(devfs_at)" ""
	umount_tree $RUN/containers/v1
	m zfs inherit mountpoint $RUN/containers/v1
	m zfs set mountpoint=/containers/H $RUN/containers/v2
	answer P2.5 "v2 and v2/var after the set, before any mount" \
	    "$(where $RUN/containers/v2) $(where $RUN/containers/v2/var)"
	mount_tree $RUN/containers/v2
	check P2.5 "v2 at" "$(where $RUN/containers/v2)" "$C/H"
	check P2.5 "v2/var at" "$(where $RUN/containers/v2/var)" "$C/H/var"
	check P2.5 "v1's mountpoint" "$(dir $RUN/containers/v1)" "$C/v1"

	m jail -c name=$J path="$C/H" mount.devfs persist
	check P2.6 "inside, started again" "$(inside)" "v2 v2"
	check P2.6 "v1 mounted at" "$(where $RUN/containers/v1)" no
	m jail -r $J

	m zfs set mountpoint=/containers/H $RUN/containers/v1
	x zfs mount $RUN/containers/v1
	rc=$?
	answer P2.7 "zfs mount of v1 at H over v2: exit" "$rc"
	answer P2.7 "mounted at H now" "$(mount -p |
	    awk -v h="$C/H" '$2 == h { print $1 }' | tr '\n' ' ')"
	umount_tree $RUN/containers/v1
	m zfs inherit mountpoint $RUN/containers/v1
}
