# P1. Clone-stream receive (planning.md 13; 7.5 step 2).
# title: a container's first save lands in zlake as a clone of the zlake foundation
# pools: run lake
# tier: zfs
#
# zrun's F was received from zlake's F, so their snapshots share
# guids. X is a clone of zrun F@v1, one clone per dataset, plus a
# container-only www. Its first save goes to zlake as clone streams
# (send -i <zrun F/d@v1> X/d@s1) and a full stream for www. Does each
# land as a clone of zlake F/d@v1, and does it need -o origin?

probe() {
	build_foundation F v1
	beam F v1
	container X F v1
	m zfs create $RUNP/containers/X/www
	admin_edits X s1
	save X s1 "first save"

	say "== a: the root's clone stream, no -o origin"
	if xs "zfs send -i $RUNP/foundations/F@v1 $RUNP/containers/X@s1 | zfs recv -u $LAKEP/containers/X"; then
		a=$(prop origin $LAKEP/containers/X)
	else
		a="(receive failed)"
	fi
	check "a: the received root's origin, no -o origin" "$a" \
	    "$LAKEP/foundations/F@v1"
	zfs list -H -o name $LAKEP/containers/X >/dev/null 2>&1 &&
	    m zfs destroy -r $LAKEP/containers/X

	say "== b: the same with -o origin"
	if xs "zfs send -i $RUNP/foundations/F@v1 $RUNP/containers/X@s1 | zfs recv -u -o origin=$LAKEP/foundations/F@v1 $LAKEP/containers/X"; then
		b=$(prop origin $LAKEP/containers/X)
	else
		b="(receive failed)"
	fi
	check "b: the received root's origin, -o origin" "$b" \
	    "$LAKEP/foundations/F@v1"
	zfs list -H -o name $LAKEP/containers/X >/dev/null 2>&1 &&
	    m zfs destroy -r $LAKEP/containers/X

	say "== c: the whole save as 7.5 writes it, no -o origin anywhere"
	mirror X s1
	n=0 good=0
	for d in $(zfs list -H -t filesystem -o name -r $RUNP/containers/X); do
		rel=${d#$RUNP/containers/X}
		o=$(prop origin "$d")
		want=-
		[ "$o" != - ] && want=$LAKEP/foundations/F$rel@v1
		got=$(prop origin "$LAKEP/containers/X$rel")
		check "c: zlake X$rel origin" "$got" "$want"
		n=$((n + 1))
		[ "$got" = "$want" ] && good=$((good + 1))
	done
	x zfs list -r -o name,used,referenced,origin $LAKEP/containers/X
	used=$(propp used $LAKEP/containers/X)
	ref=$(propp referenced $LAKEP/foundations/F)

	say "== d: the next save, -i from s1"
	admin_edits X s2
	save X s2 "second save"
	if xs "zfs send -i @s1 $RUNP/containers/X@s2 | zfs recv -u $LAKEP/containers/X"; then
		d=received
	else
		d="refused"
	fi
	check "d: the second save's incremental" "$d" received

	answer "a clone stream lands as a clone of zlake F@v1 without -o origin: $([ "$a" = "$LAKEP/foundations/F@v1" ] && echo yes || echo "no ($a)"); with -o origin: $([ "$b" = "$LAKEP/foundations/F@v1" ] && echo yes || echo "no ($b)")"
	answer "the whole first save: $good of $n datasets landed with the right origin; zlake X uses $(mib "$used") against F's $(mib "$ref")"
}
