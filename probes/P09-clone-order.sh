# P9. One clone per child at creation (planning.md 13; 7.4 step 3).
# title: cloning a foundation's children: parent first? zfs clone -p? the placeholder's canmount? what a clone keeps
# pools: run lake
# tier: zfs
#
# Clone is not recursive, so a container is made one zfs clone per
# dataset. What has to come first, what clone -p makes of a missing
# parent, what happens to the usr placeholder cloned without
# -o canmount=off, and which of the origin's properties a clone keeps.

probe() {
	build_foundation F v1
	beam F v1
	F=$RUNP/foundations/F
	C=$RUNP/containers
	m zfs set shtetl:probe=on-the-origin $F

	say "== a: a child cloned before its parent exists"
	if x zfs clone $F/var@v1 $C/A/var; then a=allowed; else a=refused; fi
	check "a: child before parent" "$a" refused

	say "== b: zfs clone -p makes the missing parent"
	x zfs clone -p $F/var@v1 $C/B/var
	b=$(prop origin $C/B 2>/dev/null)
	note "b: clone -p made $C/B with origin '${b:-(none)}'; it is $([ "$b" = - ] && echo 'a plain dataset, not a clone' || echo "'$b'")"

	say "== c: the placeholder cloned without -o canmount=off"
	m zfs clone $F@v1 $C/P
	m zfs clone $F/usr@v1 $C/P/usr
	m zfs clone $F/usr/local@v1 $C/P/usr/local
	show $C/P
	x ls "$(cdir P)/usr"
	pc=$(prop canmount $C/P/usr)
	pv=$([ -f "$(cdir P)/usr/bin/vi" ] && echo visible || echo hidden)
	note "c: P/usr has canmount=$pc and the root's usr/bin is $pv"

	say "== d: the same with -o canmount=off (shtetl's way)"
	container D F v1
	show $C/D
	dv=$([ -f "$(cdir D)/usr/bin/vi" ] && echo visible || echo hidden)
	check "d: the root's usr/bin with the placeholder canmount=off" "$dv" visible

	say "== e: usr/local cloned before usr"
	m zfs clone $F@v1 $C/E
	if x zfs clone $F/usr/local@v1 $C/E/usr/local; then e=allowed; else e=refused; fi
	check "e: usr/local before usr" "$e" refused

	say "== f: what a clone keeps of its origin"
	x zfs get -o name,property,value,source readonly,canmount,mountpoint,compression,atime,shtetl:probe $F $C/D
	f=$(zfs get -H -o property,value,source readonly,canmount,shtetl:probe $C/D |
	    tr '\t' ' ' | paste -sd ';' -)
	note "f: D (a clone of readonly, noauto F carrying shtetl:probe): $f"

	answer "children need the parent first (a: $a, e: $e); clone -p makes a plain parent ('${b:-none}'); the placeholder cloned without canmount=off is canmount=$pc and the root's usr/bin is $pv"
	answer "a clone keeps none of the origin's local properties: $f"
}
