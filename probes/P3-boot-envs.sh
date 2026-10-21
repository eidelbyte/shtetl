# title: a system's versions are boot environments, switched by bootfs
# pools:
# needs: vm
#
# Plan 7.4. Two versions under <pool>/s2sys, each a clone of the boot
# environment running now with var and usr/local children of its own,
# the root mountpoint=/ canmount=noauto, the children canmount=noauto
# so that zfs mount -a never takes the other version's. rc.d/zfsbe,
# which runs before rc.d/zfs, mounts the booted one's children.
#
# Run it, reboot, run it again, four times: each run finds its stage
# from ZFS (which environment is booted, whether s2sys is there, its
# s2probe:* properties) and says when to reboot. The reboot is a real
# one: bootfs is read by the loader, and reboot -r keeps the root.
#
#   P3.1  booted from the version bootfs named
#   P3.2  its / /var and /usr/local are its own (a marker in each)
#   P3.3  none of the other version's datasets is mounted
#   P3.4  answer: what is mounted at /var and below it
#   P3.5  bectl lists both versions
#   P3.6  by hand: the loader's boot environment menu lists both (say
#         so with the results)
#   P3.7  back on the environment it started from, with no marker, and
#         s2sys gone

MNT=${TMPDIR:-/tmp}/s2p3

# DS SRC V: DS filled from the directory SRC, V in its marker
fill() {
	m mkdir -p "$MNT"
	m mount -t zfs "$1" "$MNT"
	[ -z "$2" ] || ms "tar -C '$2' -cf - . | tar -C '$MNT' -xpf -"
	echo "$3" >"$MNT/s2which"
	m umount "$MNT"
}

version() {	# V
	m zfs clone -o mountpoint=/ -o canmount=noauto "$HOME_BE@s2p3" "$S/$1"
	fill "$S/$1" "" "$1"
	m zfs create -o canmount=noauto "$S/$1/var"
	fill "$S/$1/var" "/.zfs/snapshot/s2p3/var" "$1"
	m zfs create -o canmount=off "$S/$1/usr"
	m zfs create -o canmount=noauto "$S/$1/usr/local"
	fill "$S/$1/usr/local" "/.zfs/snapshot/s2p3/usr/local" "$1"
}

marker() { cat "$1/s2which" 2>/dev/null || echo none; }

on_version() {	# V OTHER: the checks of a boot from V
	check P3.1 "booted from" "$(booted)" "$S/$1"
	check P3.2 "/'s marker" "$(marker /)" "$1"
	check P3.2 "/var's marker" "$(marker /var)" "$1"
	check P3.2 "/usr/local's marker" "$(marker /usr/local)" "$1"
	check P3.3 "the other version's datasets mounted" \
	    "$(zfs list -H -o mounted -r "$S/$2" | grep -c yes)" 0
	answer P3.4 "mounted at /var and below" "$(mount -p |
	    awk '$2 ~ /^\/var(\/|$)/ { print $1 " " $2 }' | tr '\n' ';')"
	check P3.5 "bectl list" "$(bectl list -H | awk '{ print $1 }' |
	    sort | tr '\n' ' ')" "v1 v2 "
}

probe() {
	outside_be
	POOL=$(booted | cut -d/ -f1)
	S=$POOL/s2sys
	if ! zfs list "$S" >/dev/null 2>&1; then
		HOME_BE=$(booted)
		m zfs create -o mountpoint=none -o canmount=off \
		    -o s2probe:home="$HOME_BE" "$S"
		m zfs snapshot "$HOME_BE@s2p3"
		version v1
		version v2
		m zpool set bootfs="$S/v1" "$POOL"
		note "stage 1 of 4 set up: reboot (not reboot -r: the" \
		    "loader reads bootfs), then run P3 again"
		return
	fi
	HOME_BE=$(prop s2probe:home "$S")
	case $(booted) in
	"$S/v1")
		on_version v1 v2
		m zfs set s2probe:v1=checked "$S"
		m zpool set bootfs="$S/v2" "$POOL"
		note "stage 2 of 4: reboot (look at the loader's boot" \
		    "environment menu), then run P3 again"
		;;
	"$S/v2")
		on_version v2 v1
		m zfs set s2probe:v2=checked "$S"
		m zpool set bootfs="$HOME_BE" "$POOL"
		note "stage 3 of 4: reboot, then run P3 again"
		;;
	"$HOME_BE")
		[ "$(prop s2probe:v2 "$S")" = checked ] ||
		    die "$HOME_BE booted, but bootfs is" \
		    "$(zpool get -H -o value bootfs "$POOL"): reboot for" \
		    "real (reboot -r keeps the root), then run P3 again"
		check P3.7 "booted from" "$(booted)" "$HOME_BE"
		check P3.7 "/'s marker" "$(marker /)" none
		m zfs destroy -r "$S"
		m zfs destroy "$HOME_BE@s2p3"
		check P3.7 "s2sys" "$(zfs list -H -o name "$S" 2>/dev/null)" ""
		note "stage 4 of 4: done; say whether the loader's menu" \
		    "listed v1 and v2 (P3.6)"
		;;
	*)
		die "booted $(booted), neither version nor $HOME_BE;" \
		    "bootfs is $(zpool get -H -o value bootfs "$POOL")"
		;;
	esac
}
