# P13. Multi-rooting (planning.md 13; 4.1).
# title: two lake roots in two pools: discovery, the exactly-one-match rule, a mirror in the root holding its foundation
# pools: run lake lake2
# tier: zfs
#
# Roots carry a local shtetl:space. The second lake root is a dataset
# (shplake2/shtetl), not a pool root. A foundation is looked for
# under every lake root: one match is the answer, two an error. A
# container's mirror can only go to the root holding its foundation.

roots() {	# SPACE
	zfs get -H -s local -t filesystem -o name,value shtetl:space |
	    awk -v s="$1" '$2 == s { print $1 }'
}

lookup() {	# SPACE REL
	for r in $(roots "$1"); do
		zfs list -H -o name "$r/$2" 2>/dev/null
	done
	return 0
}

probe() {
	say "== a: discovery"
	xt zfs get -H -s local -t filesystem -o name,value shtetl:space
	note "a: the roots of every imported pool, found in ${T}s"
	check "run roots" "$(roots run | paste -sd ' ' -)" "$RUNP"
	check "lake roots" "$(roots lake | sort | paste -sd ' ' -)" \
	    "$(printf '%s\n' $LAKEP $LAKE2ROOT | sort | paste -sd ' ' -)"
	check "inherited values are not roots" \
	    "$(zfs get -H -o source shtetl:space $LAKE2ROOT/foundations)" \
	    "inherited from $LAKE2ROOT"

	say "== b: F in one lake root: one match"
	build_foundation F v1
	x lookup lake foundations/F
	check "b: matches for foundations/F" "$(lookup lake foundations/F | wc -l | tr -d ' ')" 1

	say "== c: F in both lake roots: two matches, the error case"
	LR=$LAKE2ROOT
	lake_foundation F ""
	LR=
	x lookup lake foundations/F
	check "c: matches for foundations/F" "$(lookup lake foundations/F | wc -l | tr -d ' ')" 2
	m zfs destroy -r $LAKE2ROOT/foundations/F

	say "== d: G in the second root, Y on it, Y's mirror in G's root"
	LR=$LAKE2ROOT
	build_foundation G v1
	beam G v1
	container Y G v1
	admin_edits Y s1
	save Y s1 "first save"
	mirror Y s1
	LR=
	check "d: Y's mirror is a clone of G in the second root" \
	    "$(prop origin $LAKE2ROOT/containers/Y)" "$LAKE2ROOT/foundations/G@v1"

	say "== e: Y's mirror sent to the other lake root, which has no G"
	if xs "zfs send -i $RUNP/foundations/G@v1 $RUNP/containers/Y@s1 | zfs recv -u $LAKEP/containers/Y"; then
		e=received
	else
		e=refused
	fi
	check "e: a clone stream into a root without its origin" "$e" refused
	answer "discovery by local shtetl:space finds pool and dataset roots alike; one match and two are told apart; a mirror lands only in the root holding its foundation (elsewhere: $e)"
}
