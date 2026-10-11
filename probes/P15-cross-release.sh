# P15. A cross-release update (planning.md 13; 7.2, 7.6).
# title: a container on an older release rebased onto SRC's world, updated over it in 7.2's order
# pools: run
# size: 40g
# tier: world
# needs: zfs_rebase
#
# F starts as an older release's base.txz; X is a container on it
# with an admin's edits (a user, hosts, rc.conf). F is updated in
# place as planning.md 7.2 has it: delete-old and delete-old-libs,
# installworld and distribution with install -C, the backups deleted.
# X is then rebased onto it dataset by dataset. Which names conflict
# across a release, and how long does it take?

probe() {
	world_src
	world_old
	F=$RUNP/foundations/F
	world_foundation F
	xt tar -xpf "$OLDTXZ" -C "$PR/foundations/F" ||
	    die "could not extract $OLDTXZ"
	m zfs snapshot -r -o shtetl:kind=version $F@old
	container X F old
	world_admin X
	save X s1 "a user, hosts, rc.conf"

	world_update new "$PR/foundations/F"
	m zfs snapshot -r -o shtetl:kind=version $F@new
	sn=$(sendsize $F old new)
	note "send -i F@old F@new: $(mib "$sn")"

	rebase_tree $RUNP/containers/X s1 $F new $RUNP/containers/X-staging update
	answer "across the release: rebase ${TREE_T}s, $TREE_C conflicted names (the resolutions are P15-update-*.manifest.resolution), $TREE_BAD failed; the foundation's incremental is $(mib "$sn")"
}
