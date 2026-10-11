# P6. delete-old after an install over an older release (planning.md
# 13; 7.2).
# title: an update over an older release in 7.2's order: what remains that a fresh install lacks?
# pools: run
# size: 40g
# tier: world
#
# F starts as an older release's base.txz (OLD_REL or OLD_BASE). SRC's
# world goes over it as a foundation update does (planning.md 7.2):
# delete-old and delete-old-libs first, then installworld and
# distribution with install -C, then installworld's backups deleted.
# H gets the same world fresh. Every path F has and H does not is
# written down; none should be a certificate entry or a backup.

probe() {
	world_src
	world_old
	F=$RUNP/foundations/F
	H=$RUNP/foundations/H
	world_foundation F
	xt tar -xpf "$OLDTXZ" -C "$PR/foundations/F" ||
	    die "could not extract $OLDTXZ"
	m zfs snapshot -r $F@old
	world_update new "$PR/foundations/F"
	m zfs snapshot -r $F@new
	world_make new-check-old check-old "$PR/foundations/F"

	world_foundation H
	world_install fresh "$PR/foundations/H"
	m zfs snapshot -r $H@fresh

	treelist $F new "$SCR/u.lst"
	treelist $H fresh "$SCR/f.lst"
	paths_only "$SCR/u.lst" "$SCR/f.lst" >"$OUT/P6-remains.txt"
	paths_only "$SCR/f.lst" "$SCR/u.lst" >"$OUT/P6-missing.txt"
	check "certificate entries left in /etc/ssl" \
	    "$(grep -c '^/etc/ssl/' "$OUT/P6-remains.txt")" 0
	check "installworld's backups left" \
	    "$(grep -c -E '[.]old$|/init[.]bak$' "$OUT/P6-remains.txt")" 0
	nr=$(wc -l <"$OUT/P6-remains.txt" | tr -d ' ')
	nm=$(wc -l <"$OUT/P6-missing.txt" | tr -d ' ')
	say "== what remains, by directory"
	awk -F/ '{ d = "/" $2 "/" $3; if (NF <= 3) d = "/" $2; n[d]++ }
	    END { for (d in n) printf "%6d %s\n", n[d], d }' \
	    "$OUT/P6-remains.txt" | sort -rn | tee "$SCR/bydir"
	sed -n '1,100p' "$OUT/P6-remains.txt"
	answer "$nr paths remain that a fresh install does not have (P6-remains.txt; most in $(head -3 "$SCR/bydir" | awk '{ printf "%s%s (%s)", (NR > 1 ? ", " : ""), $2, $1 }')); $nm paths a fresh install has are missing (P6-missing.txt)"
}
