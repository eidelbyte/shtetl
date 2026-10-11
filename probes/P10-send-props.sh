# P10. Do snapshot user properties travel with zfs send -i?
# (planning.md 13; 6.2).
# title: shtetl:commit on a save snapshot, sent -i with and without -p; what else -p brings
# pools: run lake
# tier: zfs
#
# Two receivers of X's saves: X1 without -p and X2 with -p, each a
# clone stream first and an incremental after; www, container-only,
# sent full both ways. Which snapshots arrive carrying shtetl:commit,
# and which properties of the dataset come along with -p?

has_commit() {	# SNAP
	v=$(prop shtetl:commit "$1" 2>/dev/null)
	[ -n "$v" ] && [ "$v" != - ] && echo yes || echo no
}

probe() {
	build_foundation F v1
	beam F v1
	container X F v1
	m zfs create $RUNP/containers/X/www
	admin_edits X s1
	save X s1 "first save"
	R=$RUNP/containers/X
	L=$LAKEP/containers

	say "== the first save: clone stream, then www full"
	ms "zfs send -i $RUNP/foundations/F@v1 $R@s1 | zfs recv -u $L/X1"
	ms "zfs send -p -i $RUNP/foundations/F@v1 $R@s1 | zfs recv -u $L/X2"
	ms "zfs send $R/www@s1 | zfs recv -u $L/X1/www"
	ms "zfs send -p $R/www@s1 | zfs recv -u $L/X2/www"

	say "== the next save: -i from s1"
	admin_edits X s2
	save X s2 "second save"
	ms "zfs send -i @s1 $R@s2 | zfs recv -u $L/X1"
	ms "zfs send -p -i @s1 $R@s2 | zfs recv -u $L/X2"
	ms "zfs send -i @s1 $R/www@s2 | zfs recv -u $L/X1/www"
	ms "zfs send -p -i @s1 $R/www@s2 | zfs recv -u $L/X2/www"

	x zfs get -r -t snapshot -o name,property,value,source shtetl:commit,shtetl:kind $L/X1 $L/X2
	say "== what -p brought with the datasets"
	x zfs get -r -s local,received -o name,property,value,source all $L/X1 $L/X2

	c1=$(has_commit $L/X1@s1) c2=$(has_commit $L/X2@s1)
	i1=$(has_commit $L/X1@s2) i2=$(has_commit $L/X2@s2)
	w1=$(has_commit $L/X1/www@s1) w2=$(has_commit $L/X2/www@s1)
	note "shtetl:commit arrived: clone stream $c1 / with -p $c2; incremental $i1 / $i2; full $w1 / $w2"

	say "== the fallback: set it on the received snapshot"
	if x zfs set "shtetl:commit=$(prop shtetl:commit $R@s1)" $L/X1@s1; then
		fb=works
	else
		fb=refused
	fi
	answer "without -p: clone $c1, incremental $i1, full $w1; with -p: clone $c2, incremental $i2, full $w2; zfs set on the received snapshot $fb"
}
