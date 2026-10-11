# P12. installworld with install -C (planning.md 13; 7.2 "Known
# cost"). Also P5 on a real world.
# title: installworld over itself, plain and with install -C: what still changes, the send, the rebase
# pools: run
# size: 40g
# tier: world
# needs: zfs_rebase
#
# F gets a world (installworld and distribution from SRC) as v1. X,
# a container on it, gets a user, a hosts line and the rest. Then the
# same world goes in again twice: over F as it is (v2, plain), and
# over G, a clone of F@v1, with INSTALL_CMD="install -C" (v2 with -C).
# What changed when nothing changed, what each costs to send, and
# what each costs to rebase X onto.

probe() {
	world_src
	F=$RUNP/foundations/F
	G=$RUNP/foundations/G
	world_foundation F
	world_install v1 "$PR/foundations/F"
	m zfs snapshot -r -o shtetl:kind=version $F@v1
	container X F v1
	world_admin X
	save X s1 "a user, hosts, rc.conf"
	clone_tree $F v1 $G

	world_install v2-plain "$PR/foundations/F"
	tp=$T
	m zfs snapshot -r -o shtetl:kind=version $F@v2
	world_install v2-C "$PR/foundations/G" INSTALL_CMD="install -C"
	tc=$T
	m zfs snapshot -r -o shtetl:kind=version $G@v2

	treelist $F v1 "$SCR/v1.lst"
	treelist $F v2 "$SCR/p.lst"
	treelist $G v2 "$SCR/c.lst"
	objdiff "$SCR/v1.lst" "$SCR/p.lst" "plain" >"$SCR/dp"
	objdiff "$SCR/v1.lst" "$SCR/c.lst" "install -C" 1000000 >"$OUT/P12-still-changed.txt"
	cat "$SCR/dp"
	sed -n '1,200p' "$OUT/P12-still-changed.txt"
	note "$(tail -1 "$SCR/dp")"
	note "$(tail -1 "$OUT/P12-still-changed.txt") (every name in P12-still-changed.txt)"
	sp=$(sendsize $F v1 v2)
	sc=$(sendsize $G v1 v2 $F)

	say "== X onto each"
	rebase_tree $RUNP/containers/X s1 $F v2 $RUNP/containers/X-p plain
	rp=$TREE_T cp=$TREE_C bp=$TREE_BAD
	rebase_tree $RUNP/containers/X s1 $G v2 $RUNP/containers/X-c C
	rc=$TREE_T cc=$TREE_C bc=$TREE_BAD

	answer "plain: install ${tp}s, send $(mib "$sp"), rebase ${rp}s, $cp conflicted names, $bp failed"
	answer "install -C: install ${tc}s, send $(mib "$sc"), rebase ${rc}s, $cc conflicted names, $bc failed"
	answer "what -C still changes: $(tail -1 "$OUT/P12-still-changed.txt" | sed 's/^install -C: //')"
	answer "P5 on a real world: X's edited hosts and passwd files over an unchanged reinstall gave $cp conflicted names"
}
