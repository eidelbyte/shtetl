# title: a container's live version is a link its jail follows once
# pools: run
# needs: -
#
# Plan 7.4: the jail's path is /containers/H, a symlink to the live
# version, switched by rename(2). jail(2) looks the path up once, at
# creation, and holds the root (sys/kern/kern_jail.c), so moving the
# link changes a running jail nothing until it is next started.
#
#   P2.1  a jail made through the link runs on v1
#   P2.2  jls shows v1's own path, not the link's
#   P2.3  with the link moved to v2, the running jail is still on v1
#   P2.4  a file written inside after the move lands in v1, not v2
#   P2.5  jail -r, then jail -c through the link, lands on v2
#   P2.6  mount.devfs mounts under the version's own path, and jail -r,
#         with the link where it was, unmounts it
#   P2.7  answer: what jail -r leaves when the link moved under a
#         running jail with devfs. jail(8) checks path/dev through the
#         link (usr.sbin/jail/command.c check_path), so this is why the
#         plan's switch stops the house before it moves the link.

J=s2p2

version() {	# NAME: a dataset with enough to run sh, its name in /which
	m zfs create $RUN/containers/$1
	_d=$(dir $RUN/containers/$1)
	m mkdir -p "$_d/bin" "$_d/dev"
	m cp /rescue/sh "$_d/bin/sh"
	echo "$1" >"$_d/which"
}

inside() { jexec $J /bin/sh -c 'read w </which; echo "$w"'; }

relink() {	# VERSION: the link moved by one rename
	m ln -s "$1" "$C/H.new"
	m mv -h "$C/H.new" "$C/H"
}

devfs_at() {
	mount -p | awk -v c="$C/" '$3 == "devfs" && index($2, c) == 1 {
	    print $2 }'
}

there() { [ -e "$1" ] && echo yes || echo no; }

probe() {
	m zfs create -o mountpoint=/containers $RUN/containers
	C=$(realpath "$(dir $RUN/containers)")
	version v1
	version v2
	m ln -s v1 "$C/H"

	m jail -c name=$J path="$C/H" persist
	check P2.1 "inside, through the link" "$(inside)" v1
	check P2.2 "jls path" "$(jls -j $J path)" "$C/v1"

	relink v2
	check P2.3 "the link" "$(readlink "$C/H")" v2
	check P2.3 "inside, after the move" "$(inside)" v1
	m jexec $J /bin/sh -c 'echo inside >/written'
	check P2.4 "the file written inside, in v1" "$(there "$C/v1/written")" yes
	check P2.4 "the file written inside, in v2" "$(there "$C/v2/written")" no

	m jail -r $J
	m jail -c name=$J path="$C/H" persist
	check P2.5 "inside, started again" "$(inside)" v2
	m jail -r $J

	m jail -c name=$J path="$C/H" mount.devfs persist
	check P2.6 "devfs mounted at" "$(devfs_at)" "$C/v2/dev"
	m jail -r $J
	check P2.6 "devfs after jail -r" "$(devfs_at)" ""

	m jail -c name=$J path="$C/H" mount.devfs persist
	relink v1
	x jail -r $J
	rc=$?
	answer P2.7 "jail -r after the link moved" "exit $rc"
	answer P2.7 "devfs left at" "'$(devfs_at)'"
	answer P2.7 "the jail left" "'$(jls -j $J name 2>/dev/null)'"
	if jls -j $J >/dev/null 2>&1; then
		m jail -R $J
	fi
	for d in $(devfs_at); do
		m umount "$d"
	done
}
