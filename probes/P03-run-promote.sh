# P3. P2 on zrun, with clone and promote instead of a move
# (planning.md 13; 7.6 step 6, 7.9 step 3).
# title: zrun: a promoted www clone keeps sending -i www@s1 www@s2 to zlake
# pools: run lake
# tier: zfs
#
# The update clones X/www@s1 into X-staging/www; cutover promotes it,
# destroys the old X and renames X-staging to X. Is s1 still on the
# new www with its guid, and does its next incremental reach the
# zlake www that the first save made?

probe() {
	build_foundation F v1
	beam F v1
	container X F v1
	m zfs create $RUNP/containers/X/www
	admin_edits X s1
	save X s1 "first save"
	mirror X s1
	g1=$(prop guid $RUNP/containers/X/www@s1)

	update_foundation F v2
	beam F v2 v1

	say "== the update: a stand-in staging tree on F@v2, www cloned from s1"
	container X-staging F v2
	m zfs clone $RUNP/containers/X/www@s1 $RUNP/containers/X-staging/www
	printf '<p>soak</p>\n' >"$(cdir X-staging)/www/soak.html"

	say "== cutover: promote, destroy the old X, rename"
	m zfs promote $RUNP/containers/X-staging/www
	snaps $RUNP/containers/X-staging/www
	m zfs destroy -r $RUNP/containers/X
	m zfs rename $RUNP/containers/X-staging $RUNP/containers/X
	show $RUNP/containers/X
	g2=$(prop guid $RUNP/containers/X/www@s1 2>/dev/null)
	check "s1 is on the promoted www with its guid" "$g2" "$g1"

	say "== the next save, sent -i from s1 into zlake's www"
	m zfs snapshot -o shtetl:kind=save $RUNP/containers/X/www@s2
	if xs "zfs send -i @s1 $RUNP/containers/X/www@s2 | zfs recv -u $LAKEP/containers/X/www"; then
		r=received
	else
		r=refused
	fi
	check "send -i www@s1 www@s2 from the promoted www" "$r" received
	snaps $LAKEP/containers/X/www
	answer "promote keeps s1 (guid $([ "$g2" = "$g1" ] && echo kept || echo changed)) and the next incremental is $r"
}
