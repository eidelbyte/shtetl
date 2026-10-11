# P8. written@<from> as the cutover warning (planning.md 13; 7.9
# step 1).
# title: written@from on a stopped container: mounts, reads with atime on, one real write
# pools: run lake
# tier: zfs
#
# X is stopped and its from-snapshot taken. Cutover warns when any
# dataset of X has written@<from> above zero. What moves it: a remount
# (a reboot), reads with atime on, a real write?

written_sum() {
	zpool sync $RUNP
	zfs get -Hp -r -t filesystem -o value written@from $RUNP/containers/X |
	    awk '{ s += $1 } END { print s + 0 }'
}

probe() {
	build_foundation F v1
	beam F v1
	container X F v1
	m zfs create $RUNP/containers/X/www
	admin_edits X s1
	save X from "update to F@v2"
	X=$RUNP/containers/X
	xd=$(cdir X)

	x zfs get -Hp -r -t filesystem -o name,value written@from $X
	a=$(written_sum)
	check "a: written@from right after the snapshot" "$a" 0

	say "== b: every dataset unmounted and mounted again"
	umount_tree $X
	mount_tree $X
	b=$(written_sum)
	check "b: after a remount" "$b" 0

	say "== c: every file read, atime off (the pool default)"
	find "$xd" -type f -exec cat {} + >/dev/null
	c=$(written_sum)
	check "c: after reads, atime off" "$c" 0

	say "== d: the same with atime on"
	m zfs set atime=on $X
	x zfs get -r -o name,value,source atime,relatime $X
	find "$xd" -type f -exec cat {} + >/dev/null
	d=$(written_sum)
	x zfs get -Hp -r -t filesystem -o name,value written@from $X
	note "d: after reads with atime on, written@from sums to $d bytes"
	m zfs inherit atime $X

	say "== e: one line appended to a log"
	printf 'one more line\n' >>"$xd/var/log/messages"
	e=$(written_sum)
	x zfs get -Hp -r -t filesystem -o name,value written@from $X
	note "e: after one real write, written@from sums to $e bytes"

	answer "written@from stays 0 through a remount ($b) and reads with atime off ($c); reads with atime on make it $d; one real write makes it $e"
}
