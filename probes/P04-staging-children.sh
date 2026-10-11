# P4. Rebases into a staging tree whose parent is placed and mounted
# (planning.md 13; 7.6 step 5, 7.6.2).
# title: clone and dataset form rebases on children of a mounted X-staging, and on a parent with a child mounted
# pools: run lake
# tier: zfs
# needs: zfs_rebase
#
# X (root and var) has saves s1, s2, s3. The root is replayed first:
# step 1 a clone form rebase making X-staging, placed and mounted;
# step 2 a dataset form one. Then var: step 1 creates X-staging/var
# under the mounted parent; step 2 runs while the parent stays
# mounted. Last, a dataset form step on the root while var is
# mounted beneath it: refused (in use), or not?

probe() {
	build_foundation F v1 var
	beam F v1
	container X F v1
	admin_edits X s1
	save X s1 "s1"
	admin_edits X s2
	save X s2 "s2"
	admin_edits X s3
	save X s3 "s3"
	update_foundation F v2
	beam F v2 v1
	fmount F
	X=$RUNP/containers/X
	S=$RUNP/containers/X-staging
	F=$RUNP/foundations/F

	say "== a: root step 1, clone form"
	zr -v -o "$OUT/P4-a.manifest" --from $X@s1 --onto $F@v2 --result $S
	check "a: root step 1 exit" "$RC" 0
	[ "$RC" = 0 ] || die "root step 1 did not finish; nothing after it can run"
	show $S
	place $S

	say "== b: root step 2, dataset form, before any child exists"
	zr -v -o "$OUT/P4-b.manifest" --allow-unrelated --base $X@s1 \
	    --from $X@s2 --onto $S --result s1
	check "b: root step 2 exit" "$RC" 0

	say "== c: var step 1, clone form, under the placed and mounted X-staging"
	zr -v -o "$OUT/P4-c.manifest" --from $X/var@s1 --onto $F/var@v2 \
	    --result $S/var
	c=$RC
	check "c: var step 1 exit" "$c" 0
	check "c: X-staging still mounted" "$(prop mounted $S)" yes
	[ "$c" = 0 ] || die "var step 1 did not finish"
	place $S/var
	check "c: var placed at" "$(prop mountpoint $S/var)" \
	    "$PR/containers/X-staging/var"

	say "== d: var step 2, dataset form, the parent mounted"
	zr -v -o "$OUT/P4-d.manifest" --allow-unrelated --base $X/var@s1 \
	    --from $X/var@s2 --onto $S/var --result s1
	d=$RC
	check "d: var step 2 exit" "$d" 0
	check "d: X-staging still mounted" "$(prop mounted $S)" yes
	check "d: var back at its mountpoint" "$(prop mounted $S/var)" yes

	say "== e: root step 3, dataset form, var mounted beneath it"
	zr -v -o "$OUT/P4-e.manifest" --allow-unrelated --base $X@s2 \
	    --from $X@s3 --onto $S --result s2
	e=$RC
	note "e: root step 3 with var mounted beneath exited $e (2 is 'in use', refused before anything was touched)"
	if [ "$e" != 0 ]; then
		say "== e2: the same with var unmounted first"
		[ "$e" = 2 ] || x "$ZR" --abort "$OUT/P4-e.manifest"
		m zfs unmount $S/var
		zr -v -o "$OUT/P4-e2.manifest" --allow-unrelated --base $X@s2 \
		    --from $X@s3 --onto $S --result s2
		check "e2: root step 3, var unmounted" "$RC" 0
		m zfs mount $S/var
	else
		check "e: var still mounted after the root's step" \
		    "$(prop mounted $S/var)" yes
		[ "$(prop mounted $S/var)" = yes ] || m zfs mount $S/var
	fi

	say "== f: var step 3, then the last snapshot"
	zr -v -o "$OUT/P4-f.manifest" --allow-unrelated --base $X/var@s2 \
	    --from $X/var@s3 --onto $S/var --result s2
	check "f: var step 3 exit" "$RC" 0
	m zfs snapshot -r $S@s3
	snaps $S
	show $S

	say "== the staging tree's contents"
	st=$(cdir X-staging)
	x cat "$st/etc/rc.conf" "$st/etc/version"
	x ls "$st/var/log" "$st/var/db"
	check "rc.conf carries each save's line once" \
	    "$(grep -c '^admin_s[123]=' "$st/etc/rc.conf")" 3
	check "the version is v2's" "$(cat "$st/etc/version")" "FreeBSD v2"
	check "var has the admin's three logs" \
	    "$(ls "$st/var/log" | grep -c '^admin-s[123]\.log$')" 3
	check "var has v2's new file" "$([ -f "$st/var/db/new-v2" ] && echo yes)" yes
	fumount F

	answer "a clone form rebase under a mounted parent: $([ "$c" = 0 ] && echo works || echo "exit $c"); a dataset form step on a child with its parent mounted: $([ "$d" = 0 ] && echo works || echo "exit $d")"
	answer "a dataset form step on a parent with a child mounted beneath: exit $e$([ "$e" = 0 ] && echo ' (works)' || echo '; so each dataset is replayed whole before its children are made, or children are unmounted for it')"
}
