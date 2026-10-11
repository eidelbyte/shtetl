# P2. A child's lineage under a parent that changes lineage, in zlake
# (planning.md 13; 7.10).
# title: zlake: www moved under a new X (a clone of F@v2) still takes send -i www@s1 www@s2
# pools: run lake
# tier: zfs
#
# zlake/containers/X is a clone of F@v1 with a container-only child
# www carrying @s1. Receive the new X as a clone of F@v2 at a new
# name, move www under it, destroy the old X, rename the new X into
# place. Does send -i www@s1 www@s2 still receive?

probe() {
	build_foundation F v1
	beam F v1
	container X F v1
	m zfs create $RUNP/containers/X/www
	admin_edits X s1
	save X s1 "first save"
	mirror X s1

	update_foundation F v2
	beam F v2 v1

	say "== the new X: X-new on zrun stands in for the rebased tree"
	container X-new F v2
	save X-new u1 "update to F@v2"
	mirror X-new u1
	check "the new X in zlake is a clone of F@v2" \
	    "$(prop origin $LAKEP/containers/X-new)" "$LAKEP/foundations/F@v2"

	say "== www moved under it, the old X destroyed, the new X renamed"
	m zfs rename $LAKEP/containers/X/www $LAKEP/containers/X-new/www
	m zfs destroy -r $LAKEP/containers/X
	m zfs rename $LAKEP/containers/X-new $LAKEP/containers/X
	show $LAKEP/containers/X
	snaps $LAKEP/containers/X/www

	say "== zrun's www goes on: s2, sent -i from s1"
	printf '<p>s2</p>\n' >"$(cdir X)/www/index-s2.html"
	m zfs snapshot -o shtetl:kind=save $RUNP/containers/X/www@s2
	if xs "zfs send -i @s1 $RUNP/containers/X/www@s2 | zfs recv -u $LAKEP/containers/X/www"; then
		r=received
	else
		r=refused
	fi
	check "send -i www@s1 www@s2 into the moved www" "$r" received
	snaps $LAKEP/containers/X/www
	answer "a child moved under a parent of another lineage still takes its incrementals: $r"
}
