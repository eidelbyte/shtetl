# P16. The mount requirement end to end (planning.md 13; 7.3, 7.6
# steps 4, 5 and 7, 9).
# title: zrun F (noauto, readonly, inherited mountpoint) mounted for an update and unmounted after; the usr placeholder refused as a side and cloned
# pools: run lake
# tier: zfs
# needs: zfs_rebase
#
# F has the usr placeholder (canmount=off) holding usr/local, and var.
# Its zrun copy is received readonly with canmount noauto; nothing
# mounts it until an update does. Then X is updated dataset by
# dataset: the root and var and usr/local rebased, usr given to
# zfs_rebase (expected: refused, exit 2) and then cloned instead.

probe() {
	build_foundation F v1
	beam F v1
	F=$RUNP/foundations/F
	X=$RUNP/containers/X
	S=$RUNP/containers/X-staging
	show $F
	check "zrun F is not mounted after the receive" "$(prop mounted $F)" no
	check "zrun F's mountpoint is inherited" \
	    "$(zfs get -H -o source mountpoint $F)" \
	    "inherited from $RUNP/foundations"
	container X F v1
	admin_edits X s1
	save X s1 "s1"
	update_foundation F v2
	beam F v2 v1

	say "== mount the target for reading (7.6 step 4)"
	fmount F
	show $F
	x mount -p
	ro=$(mount -p | awk -v m="$PR/foundations/F" '$2 == m { print $4 }')
	note "zrun F mounted with options: ${ro:-(not mounted)}"
	check "zrun F mounted read-only" \
	    "$(echo "$ro" | tr , '\n' | grep -cx ro)" 1
	check "the placeholder stays unmounted" "$(prop mounted $F/usr)" no

	say "== the root, clone form, placed"
	zr -v -o "$OUT/P16-root.manifest" --from $X@s1 --onto $F@v2 --result $S
	check "root exit" "$RC" 0
	[ "$RC" = 0 ] || die "the root did not finish"
	place $S

	say "== usr, the placeholder, given to zfs_rebase"
	zr -v -o "$OUT/P16-usr.manifest" --from $X/usr@s1 --onto $F/usr@v2 \
	    --result $S/usr
	u=$RC
	check "usr as a side: refused" "$u" 2
	zfs list -H -o name $S/usr >/dev/null 2>&1 &&
	    die "the refused rebase left $S/usr behind"
	m zfs clone -o canmount=off $F/usr@v2 $S/usr

	say "== usr/local and var, clone form, placed"
	for c in usr/local var; do
		zr -v -o "$OUT/P16-$(echo $c | tr / -).manifest" \
		    --from $X/$c@s1 --onto $F/$c@v2 --result $S/$c
		check "$c exit" "$RC" 0
		[ "$RC" = 0 ] && place $S/$c
	done
	show $S
	check "usr/local mounted at" "$(prop mountpoint $S/usr/local)" \
	    "$PR/containers/X-staging/usr/local"
	check "the staging root's usr/bin is visible" \
	    "$([ -f "$(cdir X-staging)/usr/bin/vi" ] && echo yes || echo no)" yes

	say "== unmount the target (7.6 step 7)"
	fumount F
	n=$(zfs get -H -r -o value mounted $F | grep -c yes)
	check "zrun F datasets still mounted" "$n" 0
	answer "the zrun foundation mounts read-only on demand ($ro) and unmounts after: $([ "$n" = 0 ] && echo yes || echo "no, $n still mounted")"
	answer "the canmount=off placeholder as a side: exit $u (2 expected), and cloning it instead works"
}
