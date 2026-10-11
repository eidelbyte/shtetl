# P11. Promote at cutover, then mirror the promote in zlake
# (planning.md 13; 7.9 step 3, 7.10).
# title: the whole cutover on zrun and its mirror in zlake; does the next save still receive?
# pools: run lake
# tier: zfs
#
# The soak's staging tree is mirrored into zlake (root a clone of
# zlake F@v2, www a clone of zlake X/www@s1). Cutover promotes www on
# both sides, archives zlake's old X, renames X-staging to X. Then the
# next save goes -i from the last mirrored snapshot, dataset by
# dataset.

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

	say "== update and soak: staging on F@v2, www cloned from s1, saved, mirrored"
	container X-staging F v2
	m zfs clone $RUNP/containers/X/www@s1 $RUNP/containers/X-staging/www
	admin_edits X-staging soak1
	save X-staging soak1 "soak"
	mirror X-staging soak1
	check "zlake X-staging is a clone of zlake F@v2" \
	    "$(prop origin $LAKEP/containers/X-staging)" "$LAKEP/foundations/F@v2"
	check "zlake X-staging/www is a clone of zlake X/www@s1" \
	    "$(prop origin $LAKEP/containers/X-staging/www)" \
	    "$LAKEP/containers/X/www@s1"

	say "== cutover on zrun"
	m zfs promote $RUNP/containers/X-staging/www
	m zfs destroy -r $RUNP/containers/X
	m zfs rename $RUNP/containers/X-staging $RUNP/containers/X

	say "== the same cutover mirrored in zlake"
	day=$(date -u +%Y-%m-%d)
	m zfs promote $LAKEP/containers/X-staging/www
	m zfs rename $LAKEP/containers/X $LAKEP/archive/containers/X-$day
	m zfs rename $LAKEP/containers/X-staging $LAKEP/containers/X
	show $LAKEP/containers/X
	show $LAKEP/archive/containers/X-$day
	snaps $LAKEP/containers/X/www
	snaps $LAKEP/archive/containers/X-$day
	ao=$(prop origin $LAKEP/archive/containers/X-$day/www)
	check "the archive's www is a clone of the live www@s1" "$ao" \
	    "$LAKEP/containers/X/www@s1"

	say "== the next save, -i from soak1, dataset by dataset"
	admin_edits X s2
	save X s2 "after cutover"
	n=0 good=0
	for d in $(zfs list -H -t filesystem -o name -r $RUNP/containers/X); do
		rel=${d#$RUNP/containers/X}
		n=$((n + 1))
		if xs "zfs send -i @soak1 $d@s2 | zfs recv -u $LAKEP/containers/X$rel"; then
			good=$((good + 1))
		else
			note "X$rel: the incremental after the mirrored cutover was refused"
		fi
	done
	check "datasets that took the next incremental" "$good" "$n"
	answer "after a promote mirrored in zlake, the next save received for $good of $n datasets; the archive's www has origin $ao"
}
