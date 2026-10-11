# P5. A rebuild in place, then a rebase of a container whose admin
# edited rc.conf (planning.md 13; 7.2 "Known cost").
# title: synthetic installworld rewrite: rc.conf edited in X, untouched by v1 -> v2; conflict? time? send size?
# pools: run
# size: 4g
# tier: zfs
# needs: zfs_rebase
#
# F is a payload tree (PAYLOAD, host directories) plus a small etc.
# v2 rewrites every file with the same bytes, each a new object, as
# installworld does (tar -U from the v1 snapshot), and changes a few
# for real. G is the same update without the rewrite: a clone of F@v1
# with only the real changes, what install -C everywhere would give.
# X, a clone of F@v1 whose admin edited rc.conf, is rebased onto each.
# The real installworld is P12 (--world).

probe() {
	F=$RUNP/foundations/F
	G=$RUNP/foundations/G
	X=$RUNP/containers/X
	m zfs create $F
	fill_foundation "$PR/foundations/F" v1
	payload "$PR/foundations/F"
	m zfs snapshot $F@v1
	x zfs list -o name,used,referenced $F

	m zfs clone $F@v1 $X
	admin_edits X s1
	save X s1 "rc.conf edited"

	say "== v2 in place: every file written again from v1, then the real changes"
	ms "tar -cf - -C $PR/foundations/F/.zfs/snapshot/v1 . | tar -xpUf - -C $PR/foundations/F"
	next_foundation "$PR/foundations/F" v2
	m zfs snapshot $F@v2

	say "== G: the real changes alone"
	m zfs clone $F@v1 $G
	next_foundation "$PR/foundations/G" v2
	m zfs snapshot $G@v2

	objlist "$PR/foundations/F/.zfs/snapshot/v1" "$SCR/v1.lst"
	objlist "$PR/foundations/F/.zfs/snapshot/v2" "$SCR/v2.lst"
	objlist "$PR/foundations/G/.zfs/snapshot/v2" "$SCR/g2.lst"
	objdiff "$SCR/v1.lst" "$SCR/v2.lst" "F@v1 -> F@v2 (rewritten)" >"$SCR/d1"
	objdiff "$SCR/v1.lst" "$SCR/g2.lst" "F@v1 -> G@v2 (changes only)" >"$SCR/d2"
	cat "$SCR/d1" "$SCR/d2"
	note "$(tail -1 "$SCR/d1")"
	note "$(tail -1 "$SCR/d2")"
	if cmp -s "$PR/foundations/F/.zfs/snapshot/v1/etc/rc.conf" \
	    "$PR/foundations/F/.zfs/snapshot/v2/etc/rc.conf"; then
		same=yes
	else
		same=no
	fi
	check "rc.conf's bytes the same in F@v1 and F@v2" "$same" yes
	sa=$(sendsize $F v1 v2)
	sb=$(sendsize $G v1 v2 $F)
	note "send -i F@v1 F@v2: $(mib "$sa"); send -i F@v1 G@v2: $(mib "$sb")"

	say "== A: X@s1 onto the rewritten F@v2"
	zr -v -o "$OUT/P5-a.manifest" --from $X@s1 --onto $F@v2 \
	    --result $RUNP/containers/X-a
	ra=$RC ta=$T ba=$ZRBYTES pa=$ZRPOOLS ca=$(conflicts "$OUT/P5-a.manifest")
	check "A: exit (no conflict expected)" "$ra" 0

	say "== B: X@s1 onto G@v2"
	zr -v -o "$OUT/P5-b.manifest" --from $X@s1 --onto $G@v2 \
	    --result $RUNP/containers/X-b
	rb=$RC tb=$T bb=$ZRBYTES pb=$ZRPOOLS cb=$(conflicts "$OUT/P5-b.manifest")
	check "B: exit (no conflict expected)" "$rb" 0

	if [ "$ra" = 0 ]; then
		place $RUNP/containers/X-a
		check "A: the result's rc.conf has the admin's line" \
		    "$(grep -c '^admin_s1=' "$(cdir X-a)/etc/rc.conf")" 1
		check "A: the result's version is v2's" \
		    "$(cat "$(cdir X-a)/etc/version")" "FreeBSD v2"
	fi

	answer "rewritten v2: exit $ra, $ca conflicts, ${ta}s, $ba bytes compared, $pa pools unchanged; send $(mib "$sa")"
	answer "changes-only v2: exit $rb, $cb conflicts, ${tb}s, $bb bytes compared, $pb pools unchanged; send $(mib "$sb")"
}
