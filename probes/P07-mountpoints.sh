# P7. Mountpoint inheritance (planning.md 13; 4.3, 4.7, 7.9 step 5).
# title: clones under containers/ inherit their mountpoints; canmount does not inherit; zlake never collides; import order
# pools: run lake
# tier: zfs
#
# Where clones under zrun/containers mount, with the usr placeholder
# and usr/local beneath it; whether canmount=noauto on a space root
# reaches its children (4.3 assumes it); a clone across pools; where
# zlake's mirror would mount; a rename carrying mountpoints; and the
# order an import mounts the tree in.

probe() {
	build_foundation F v1
	beam F v1
	container X F v1
	m zfs create $RUNP/containers/X/www
	show $RUNP
	x zfs get -r -t filesystem -o name,property,value,source canmount $RUNP $LAKEP

	say "== a: where X's datasets mount"
	check "X" "$(prop mountpoint $RUNP/containers/X)" "$PR/containers/X"
	check "X/var" "$(prop mountpoint $RUNP/containers/X/var)" "$PR/containers/X/var"
	check "X/usr mounted (placeholder)" "$(prop mounted $RUNP/containers/X/usr)" no
	check "X/usr/local mounted" "$(prop mounted $RUNP/containers/X/usr/local)" yes
	check "X/usr/local" "$(prop mountpoint $RUNP/containers/X/usr/local)" \
	    "$PR/containers/X/usr/local"
	check "X's usr/bin (the root dataset's) visible" \
	    "$([ -f "$(cdir X)/usr/bin/vi" ] && echo yes || echo no)" yes
	check "zrun F mounted" "$(prop mounted $RUNP/foundations/F)" no

	say "== b: canmount on a space root and its children"
	rc=$(prop canmount $RUNP)
	cc=$(zfs get -H -o value,source canmount $RUNP/foundations | tr '\t' ' ')
	lc=$(zfs get -H -o value,source canmount $LAKEP/foundations/F/var | tr '\t' ' ')
	note "the zrun root has canmount=$rc; zrun/foundations has '$cc'; zlake F/var has '$lc'"
	answer "canmount=noauto on a space root reaches its children: $(echo "$cc" | grep -q '^noauto' && echo yes || echo "no ($cc); 4.3's 'inherited below' holds for mountpoint, not canmount")"

	say "== c: a clone across pools"
	if x zfs clone $LAKEP/foundations/F@v1 $RUNP/containers/Y; then
		c=allowed
	else
		c=refused
	fi
	check "clone of a zlake snapshot into zrun" "$c" refused

	say "== d: zlake's mirror"
	save X s1 "s1"
	mirror X s1
	show $LAKEP/containers/X
	n=$(zfs get -H -r -o value mounted $LAKEP | grep -c yes)
	check "zlake datasets mounted" "$n" 0
	check "zlake X's mountpoint" "$(prop mountpoint $LAKEP/containers/X)" none

	say "== e: a rename carries the mountpoints (7.9 step 5)"
	container X-staging F v1
	m zfs create $RUNP/containers/X-staging/www
	m zfs destroy -r $RUNP/containers/X
	m zfs rename $RUNP/containers/X-staging $RUNP/containers/X
	show $RUNP/containers/X
	check "renamed X/var" "$(prop mountpoint $RUNP/containers/X/var)" "$PR/containers/X/var"
	check "renamed X/var mounted" "$(prop mounted $RUNP/containers/X/var)" yes
	check "renamed X/usr/local mounted" "$(prop mounted $RUNP/containers/X/usr/local)" yes

	say "== f: export and import; the order the import mounts in"
	md=$(pool_md $RUNP)
	m zpool export $RUNP
	m zpool import -d "/dev/$md" $RUNP
	x mount -p
	mount -p | awk -v r="$PR/" 'index($2, r) == 1 { print $2 }' >"$SCR/order"
	cat "$SCR/order"
	bad=$(awk '{ for (i = 1; i < NR; i++) if (index(seen[i], $0 "/") == 1) print seen[i] " before " $0; seen[NR] = $0 }' "$SCR/order")
	check "a child mounted before its parent" "${bad:-none}" none
	check "X/usr/local mounted after import" "$(prop mounted $RUNP/containers/X/usr/local)" yes
	check "zrun F mounted after import" "$(prop mounted $RUNP/foundations/F)" no
	answer "clones under containers/ mount by inheritance, the placeholder hides nothing, renames carry mountpoints, and an import mounts parents first: see the checks"
}
