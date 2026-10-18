# title: a house's own data moves to each new version by clone and promote
# pools: run lake
# needs: -
#
# Plan 7.1 and 8.2: a dataset a house has and its foundation does not
# (www) is, on a new version, a clone of the live version's at the
# final save, and is promoted when the old version is pruned. The lake
# follows by one rule in either order, before or after the run's
# promote: a new dataset's first stream is sent from the run's
# snapshot that has the guid the lake already holds, and received
# with -o origin= that lake snapshot.
#
#   P5.1  run: v2/www cloned from v1/www@s3 has it as origin
#   P5.2  lake, before the run's promote: v2/www@s4 received with
#         origin the lake's v1/www@s3, its guid the run's
#   P5.3  run: v2/www promoted holds s1 - s3 with their guids
#   P5.4  run: v1 destroyed whole; v2/www holds what s4 held
#   P5.5  lake: v2/www promoted and v1 destroyed; v2/www holds
#         s1 - s4 with the run's guids
#   P5.6  run: v3/www cloned from v2/www@s5, s6 taken, promoted, and v2
#         destroyed before the lake has any of it
#   P5.7  lake, after the run's promote: v3/www@s6 sent from the run's
#         v3/www@s5, received with origin the lake's v2/www@s5, its
#         guid the run's
#   P5.8  lake: v3/www promoted and v2 destroyed; v3/www holds
#         s1 - s6 with the run's guids

save() {	# DS NAME: a file for the save, then its snapshot
	echo "$2" >"$(dir "$1")/$2"
	m zfs snapshot "$1@$2"
}

snaps() {	# DS: its snapshots oldest first, name:guid each
	zfs list -H -t snapshot -o name,guid -s createtxg -d 1 "$1" |
	    sed 's/^[^@]*@//' | tr '\t\n' ': '
}

files() { ls "$(dir "$1")" | tr '\n' ' '; }

probe() {
	m zfs create -o mountpoint=/v1 $RUN/v1
	m zfs create $RUN/v1/www
	for s in s1 s2 s3; do
		save $RUN/v1/www $s
	done
	m zfs create $LAKE/v1
	ms "zfs send $RUN/v1/www@s1 | zfs receive -u $LAKE/v1/www"
	ms "zfs send -I @s1 $RUN/v1/www@s3 | zfs receive -u $LAKE/v1/www"

	m zfs create -o mountpoint=/v2 $RUN/v2
	m zfs clone $RUN/v1/www@s3 $RUN/v2/www
	check P5.1 "v2/www's origin" "$(prop origin $RUN/v2/www)" \
	    "$RUN/v1/www@s3"
	save $RUN/v2/www s4

	m zfs create $LAKE/v2
	ms "zfs send -i $RUN/v1/www@s3 $RUN/v2/www@s4 |
	    zfs receive -u -o origin=$LAKE/v1/www@s3 $LAKE/v2/www"
	check P5.2 "the lake's v2/www origin" "$(prop origin $LAKE/v2/www)" \
	    "$LAKE/v1/www@s3"
	check P5.2 "the lake's v2/www@s4" "$(prop guid $LAKE/v2/www@s4)" \
	    "$(prop guid $RUN/v2/www@s4)"

	before=$(snaps $RUN/v1/www)
	m zfs promote $RUN/v2/www
	check P5.3 "v2/www's saves after the promote" \
	    "$(snaps $RUN/v2/www)" "${before}s4:$(prop guid $RUN/v2/www@s4) "
	m zfs destroy -r $RUN/v1
	check P5.4 "v2/www's files" "$(files $RUN/v2/www)" "s1 s2 s3 s4 "

	m zfs promote $LAKE/v2/www
	m zfs destroy -r $LAKE/v1
	check P5.5 "the lake's v2/www" "$(snaps $LAKE/v2/www)" \
	    "$(snaps $RUN/v2/www)"

	save $RUN/v2/www s5
	ms "zfs send -i @s4 $RUN/v2/www@s5 | zfs receive -u $LAKE/v2/www"
	m zfs create -o mountpoint=/v3 $RUN/v3
	m zfs clone $RUN/v2/www@s5 $RUN/v3/www
	save $RUN/v3/www s6
	m zfs promote $RUN/v3/www
	m zfs destroy -r $RUN/v2
	check P5.6 "v3/www on the run" "$(files $RUN/v3/www)" \
	    "s1 s2 s3 s4 s5 s6 "

	m zfs create $LAKE/v3
	ms "zfs send -i $RUN/v3/www@s5 $RUN/v3/www@s6 |
	    zfs receive -u -o origin=$LAKE/v2/www@s5 $LAKE/v3/www"
	check P5.7 "the lake's v3/www origin" "$(prop origin $LAKE/v3/www)" \
	    "$LAKE/v2/www@s5"
	check P5.7 "the lake's v3/www@s6" "$(prop guid $LAKE/v3/www@s6)" \
	    "$(prop guid $RUN/v3/www@s6)"

	m zfs promote $LAKE/v3/www
	m zfs destroy -r $LAKE/v2
	check P5.8 "the lake's v3/www" "$(snaps $LAKE/v3/www)" \
	    "$(snaps $RUN/v3/www)"
}
