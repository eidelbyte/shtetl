# title: an actor over a C port, and its runtime measured (spike S1)
# pools: run lake
# needs: elixir
#
# Plan 10 and 14. spike/ is copied onto the run pool and built there,
# HOME there too so that mix writes nothing of root's anywhere else:
# the port with make, then the release with mix. A toy foundation
# f@r0, its house h0 and the lake's f@r0 are set up here; the release
# is started with the pools in its environment; then five rolls, one
# left alone and one with the actor killed after each of its steps.
#
#   S1.1  the port and the release build
#   S1.2  the release answers its first rpc (answers: the seconds from
#         its start, and what one rpc from the shell costs)
#   S1.3  answers: the release's size, the VM's resident memory idle,
#         the port's round trip
#   S1.4  roll 1, left alone: f@r1, h1 and the lake's f@r1 with the
#         run's guid; the actor never started again
#   S1.5  roll 2, the actor killed after its write: the same, and the
#         actor started again
#   S1.6  roll 3, killed after its snapshot: the same
#   S1.7  roll 4, killed after its clone: the same
#   S1.8  roll 5, killed after its beam: the same
#   S1.9  the port's owner killed: one zport process after it, and
#         roll 6 goes through

rel() { "$R" "$@"; }
rpc() { "$R" rpc "$1"; }

actor() { rpc 'IO.inspect(Process.whereis(Roll.Foundation))'; }

# The actor's state once it is idle, or what it last said in a minute.
settle() {
	_i=0
	while [ $_i -lt 60 ]; do
		_s=$(rpc 'IO.inspect(Roll.Foundation.state())' 2>/dev/null)
		[ "$_s" = :idle ] && break
		_i=$((_i + 1))
		sleep 1
	done
	echo "$_s"
}

rolled() {	# CELL K CRASH RESTARTED
	_before=$(actor)
	x rpc "Roll.Foundation.roll($3)"
	check "$1" "roll $2 settles" "$(settle)" :idle
	check "$1" "f@r$2" "$(zfs list -H -o name $RUN/f@r$2 2>/dev/null)" \
	    "$RUN/f@r$2"
	check "$1" "h$2" "$(zfs list -H -o name $RUN/h$2 2>/dev/null)" "$RUN/h$2"
	check "$1" "the lake's f@r$2" "$(prop guid $LAKE/f@r$2 2>/dev/null)" \
	    "$(prop guid $RUN/f@r$2)"
	if [ "$(actor)" = "$_before" ]; then _r=no; else _r=yes; fi
	check "$1" "the actor started again" "$_r" "$4"
}

probe() {
	m zfs create -o mountpoint=/build $RUN/build
	B=$(dir $RUN/build)
	m cp -R "$TOP/spike/zport" "$TOP/spike/roll" "$B/"
	export HOME="$B/home" MIX_ENV=prod
	m mkdir -p "$HOME"
	x make -C "$B/zport"
	check S1.1 "the port builds" "$?" 0
	(cd "$B/roll" && x mix release --overwrite)
	check S1.1 "the release builds" "$?" 0
	R=$B/roll/_build/prod/rel/roll/bin/roll
	[ -x "$R" ] || die "no release to run"

	m zfs create -o mountpoint=/f $RUN/f
	F=$(dir $RUN/f)
	m dd if=/dev/random of="$F/r0" bs=1m count=1
	m zfs snapshot $RUN/f@r0
	m zfs clone $RUN/f@r0 $RUN/h0
	ms "zfs send $RUN/f@r0 | zfs receive -u $LAKE/f"

	export S1_RUN=$RUN S1_LAKE=$LAKE S1_DIR=$F
	export RELEASE_DISTRIBUTION=name RELEASE_NODE=roll@127.0.0.1
	# the system's epmd, so that nothing left running is on the pool
	ER=$(erl -noshell -eval 'io:format("~s", [code:root_dir()]), halt().')
	m "$ER/bin/epmd" -daemon
	cat >"$B/start.sh" <<EOF
"$R" daemon || exit 1
i=0
until "$R" rpc 'IO.puts(:ready)' >/dev/null 2>&1; do
	i=\$((i + 1))
	[ \$i -lt 100 ] || exit 1
	sleep 0.1
done
EOF
	xs "/usr/bin/time -p -o '$B/start.time' sh '$B/start.sh'"
	check S1.2 "the release answers" "$?" 0
	answer S1.2 "seconds from its start to its first rpc" \
	    "$(awk '$1 == "real" { print $2 }' "$B/start.time")"
	xs "/usr/bin/time -p -o '$B/rpc.time' '$R' rpc 'IO.puts(:ready)'"
	answer S1.2 "seconds one rpc from the shell takes" \
	    "$(awk '$1 == "real" { print $2 }' "$B/rpc.time")"

	answer S1.3 "the release on disk" \
	    "$(du -sk "$B/roll/_build/prod/rel/roll" | cut -f1) KiB"
	answer S1.3 "resident memory idle" \
	    "$(ps -o rss= -p "$(rel pid)" | tr -d ' ') KiB"
	answer S1.3 "the port's round trip" \
	    "$(rpc 'IO.puts(Roll.port_micros(1000))') microseconds"

	rolled S1.4 1 nil no
	rolled S1.5 2 :write yes
	rolled S1.6 3 :snapshot yes
	rolled S1.7 4 :clone yes
	rolled S1.8 5 :beam yes

	x rpc 'Process.exit(Process.whereis(Roll.Zfs), :kill)'
	check S1.9 "the actor after the port's owner died" "$(settle)" :idle
	check S1.9 "zport processes" "$(pgrep -f '/priv/zport$' | wc -l |
	    tr -d ' ')" 1
	rolled S1.9 6 nil no

	x rel stop
}
