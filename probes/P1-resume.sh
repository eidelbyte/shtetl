# title: a beam cut part way resumes from its token
# pools: run lake
# needs: -
#
# Plan 8.1: every receive is resumable, so a beam cut anywhere is a
# beam not yet converged. Each stream is cut by head -c, so the cut is
# the same on every run.
#
#   P1.1  a full send cut part way leaves a token on the lake
#   P1.2  resumed from it, the lake's @1 is the run's
#   P1.3  an incremental cut part way leaves a token; the lake's newest
#         is still @1
#   P1.4  while the token stands, a fresh incremental is refused
#   P1.5  resumed, the lake's @2 is the run's and no token is left
#   P1.6  a clone stream (a house version from a foundation snapshot the
#         lake has) cut part way leaves a token on the new dataset
#   P1.7  resumed, its origin is the lake's f@2 and its @1 the run's
#   P1.8  no token is left anywhere on the lake (a partial incremental
#         shows its token on the dataset it was going into)
#   P1.9  a token whose source snapshot is gone does not resume
#   P1.10 receive -A takes that cut away, and the next send lands

CUT=8388608	# bytes of a stream let through

files() {	# DIR N TAG: N files of 1 MiB
	_i=0
	while [ $_i -lt "$2" ]; do
		dd if=/dev/random of="$1/$3-$_i" bs=1m count=1 2>/dev/null
		_i=$((_i + 1))
	done
}

token() {	# DS: yes if a resume token stands on DS
	[ "$(prop receive_resume_token "$1")" != - ] && echo yes || echo no
}

probe() {
	m zfs create -o mountpoint=/f $RUN/f
	files "$(dir $RUN/f)" 32 one
	m zfs snapshot $RUN/f@1

	xs "zfs send $RUN/f@1 | head -c $CUT | zfs receive -s -u $LAKE/f"
	check P1.1 "a cut full send leaves a token" "$(token $LAKE/f)" yes
	xs "zfs send -t $(prop receive_resume_token $LAKE/f) |
	    zfs receive -s -u $LAKE/f"
	check P1.2 "the resumed @1 is the run's" \
	    "$(prop guid $LAKE/f@1)" "$(prop guid $RUN/f@1)"

	files "$(dir $RUN/f)" 32 two
	m zfs snapshot $RUN/f@2
	xs "zfs send -i @1 $RUN/f@2 | head -c $CUT |
	    zfs receive -s -u $LAKE/f"
	check P1.3 "a cut incremental leaves a token" "$(token $LAKE/f)" yes
	check P1.3 "the lake's newest is still @1" \
	    "$(zfs list -H -t snapshot -o name -s createtxg $LAKE/f | tail -1)" \
	    "$LAKE/f@1"
	if xs "zfs send -i @1 $RUN/f@2 | zfs receive -s -u $LAKE/f"; then
		r=received
	else
		r=refused
	fi
	check P1.4 "a fresh incremental while the token stands" "$r" refused
	xs "zfs send -t $(prop receive_resume_token $LAKE/f) |
	    zfs receive -s -u $LAKE/f"
	check P1.5 "the resumed @2 is the run's" \
	    "$(prop guid $LAKE/f@2)" "$(prop guid $RUN/f@2)"
	check P1.5 "no token left" "$(token $LAKE/f)" no

	m zfs clone -o mountpoint=/h $RUN/f@2 $RUN/h
	files "$(dir $RUN/h)" 16 house
	m zfs snapshot $RUN/h@1
	xs "zfs send -i $RUN/f@2 $RUN/h@1 | head -c $CUT |
	    zfs receive -s -u $LAKE/h"
	check P1.6 "a cut clone stream leaves a token" "$(token $LAKE/h)" yes
	xs "zfs send -t $(prop receive_resume_token $LAKE/h) |
	    zfs receive -s -u $LAKE/h"
	check P1.7 "the resumed clone's origin" "$(prop origin $LAKE/h)" \
	    "$LAKE/f@2"
	check P1.7 "the resumed h@1 is the run's" \
	    "$(prop guid $LAKE/h@1)" "$(prop guid $RUN/h@1)"

	x zfs list -t all -o name,receive_resume_token -r $LAKE
	check P1.8 "tokens left" "$(zfs list -H -o receive_resume_token \
	    -r $LAKE | grep -vc '^-$')" 0

	files "$(dir $RUN/f)" 32 three
	m zfs snapshot $RUN/f@3
	xs "zfs send -i @2 $RUN/f@3 | head -c $CUT |
	    zfs receive -s -u $LAKE/f"
	m zfs destroy $RUN/f@3
	if xs "zfs send -t $(prop receive_resume_token $LAKE/f) |
	    zfs receive -s -u $LAKE/f"; then
		r=resumed
	else
		r=refused
	fi
	check P1.9 "a token whose source is gone" "$r" refused

	m zfs receive -A $LAKE/f
	check P1.10 "receive -A takes the cut away" "$(token $LAKE/f)" no
	files "$(dir $RUN/f)" 4 four
	m zfs snapshot $RUN/f@4
	ms "zfs send -i @2 $RUN/f@4 | zfs receive -s -u $LAKE/f"
	check P1.10 "the next send lands" \
	    "$(prop guid $LAKE/f@4)" "$(prop guid $RUN/f@4)"
}
