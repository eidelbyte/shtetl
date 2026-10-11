# P14. History replay (planning.md 13; 7.6.2, Q-J and Q-N).
# title: replay s1..s3 onto F@v2: step 1 clone form, steps 2-3 dataset form with --allow-unrelated --base
# pools: run
# size: 4g
# tier: zfs
# needs: zfs_rebase
#
# X has three saves: s1 adds line A to rc.conf and a file, s2 adds
# line B and edits ten payload files, s3 removes one file and renames
# another. Step 1 is a clone form rebase onto F@v2; step k a dataset
# form one with --base X@s(k-1), whose pre-apply snapshot is the
# replayed s(k-1). First a dry run shows what the derived base (F@v1)
# would make of step 2. Each step's time and -v counts are set
# against the dataset's size, and against update.depth = 1 (one
# rebase of s3).

probe() {
	F=$RUNP/foundations/F
	X=$RUNP/containers/X
	S=$RUNP/containers/X-staging
	m zfs create $F
	fill_foundation "$PR/foundations/F" v1
	payload "$PR/foundations/F"
	m zfs snapshot $F@v1
	m zfs clone $F@v1 $X
	xd=$(cdir X)
	for p in $PAYLOAD_DIRS; do
		find "$xd/$p" -type f
	done | sort >"$SCR/files"
	[ "$(wc -l <"$SCR/files")" -ge 30 ] ||
	    die "the payload has fewer than 30 files; set PAYLOAD"

	printf 'line_a="YES"\n' >>"$xd/etc/rc.conf"
	printf 'added in s1\n' >"$xd/etc/added-s1"
	save X s1 "line A"
	printf 'line_b="YES"\n' >>"$xd/etc/rc.conf"
	for f in $(sed -n '1,10p' "$SCR/files"); do
		printf 'shtetl-probe-s2\n' >>"$f"
	done
	save X s2 "line B, ten files"
	gone=$(sed -n '11p' "$SCR/files")
	moved=$(sed -n '12p' "$SCR/files")
	m rm "$gone"
	m mv "$moved" "$moved.renamed"
	save X s3 "a file removed, one renamed"

	next_foundation "$PR/foundations/F" v2
	for f in $(sed -n '21,25p' "$SCR/files"); do
		printf 'shtetl-probe-v2\n' >>"$PR/foundations/F${f#$xd}"
	done
	m zfs snapshot $F@v2
	size=$(propp referenced $X@s3)
	nfiles=$(find "$xd" -xdev | wc -l)
	note "the dataset: $(mib "$size"), $nfiles names"

	say "== step 1: clone form, base derived (F@v1)"
	zr -v -o "$OUT/P14-1.manifest" --from $X@s1 --onto $F@v2 --result $S
	r1=$RC t1=$T b1=$ZRBYTES p1=$ZRPOOLS
	check "step 1 exit" "$r1" 0
	[ "$r1" = 0 ] || die "step 1 did not finish; the replay stops here"
	place $S

	say "== Q-J: step 2 as a dry run with the base derived instead"
	m zfs snapshot $S@dry
	zr -n -o "$OUT/P14-derived.manifest" --from $X@s2 --onto $S@dry
	m zfs destroy $S@dry
	dc=$(sed -n 's/^#conflicts //p' "$OUT/P14-derived.manifest")
	db=$(sed -n 's/^#base \([^ ]*\).*/\1/p' "$OUT/P14-derived.manifest")
	note "the derived base is $db; step 2 against it has ${dc:-?} conflict groups"

	say "== step 2: dataset form, --base X@s1"
	zr -v -o "$OUT/P14-2.manifest" --allow-unrelated --base $X@s1 \
	    --from $X@s2 --onto $S --result s1
	r2=$RC t2=$T b2=$ZRBYTES c2=$(conflicts "$OUT/P14-2.manifest")
	check "step 2 exit" "$r2" 0

	say "== step 3: dataset form, --base X@s2"
	zr -v -o "$OUT/P14-3.manifest" --allow-unrelated --base $X@s2 \
	    --from $X@s3 --onto $S --result s2
	r3=$RC t3=$T b3=$ZRBYTES c3=$(conflicts "$OUT/P14-3.manifest")
	check "step 3 exit" "$r3" 0
	m zfs snapshot $S@s3
	snaps $S

	say "== the replayed snapshots take the originals' properties"
	for s in s1 s2; do
		m zfs set "shtetl:commit=$(prop shtetl:commit $X@$s)" \
		    "shtetl:replayed-from=$(prop guid $X@$s)" shtetl:kind=save $S@$s
	done
	x zfs get -r -t snapshot -o name,property,value shtetl:commit,shtetl:replayed-from $S

	say "== the result"
	sd=$(cdir X-staging)
	check "rc.conf has line A once" "$(grep -c '^line_a=' "$sd/etc/rc.conf")" 1
	check "rc.conf has line B once" "$(grep -c '^line_b=' "$sd/etc/rc.conf")" 1
	check "the version is v2's" "$(cat "$sd/etc/version")" "FreeBSD v2"
	check "the removed file is gone" \
	    "$([ -e "$sd${gone#$xd}" ] && echo there || echo gone)" gone
	check "the renamed file is renamed" \
	    "$([ -e "$sd${moved#$xd}.renamed" ] && echo yes || echo no)" yes
	e=0
	for f in $(sed -n '1,10p' "$SCR/files"); do
		grep -q shtetl-probe-s2 "$sd${f#$xd}" && e=$((e + 1))
	done
	check "the ten s2 edits are there" "$e" 10

	say "== update.depth = 1: s3 in one rebase, for the cost of history"
	zr -v -o "$OUT/P14-depth1.manifest" --from $X@s3 --onto $F@v2 \
	    --result $RUNP/containers/X-depth1
	r4=$RC t4=$T b4=$ZRBYTES p4=$ZRPOOLS
	check "depth 1 exit" "$r4" 0

	answer "the derived base would make ${dc:-?} conflict groups of step 2; --base X@s1 makes $c2 (exit $r2)"
	answer "step 1 (prune on): ${t1}s, $b1 bytes compared, $p1 pools unchanged"
	answer "step 2 (prune off): ${t2}s, $b2 bytes compared; step 3: ${t3}s, $b3 bytes compared; the dataset is $(mib "$size") ($size bytes)"
	answer "depth 1 (one rebase of s3): ${t4}s, $b4 bytes compared, $p4 pools unchanged"
}
