# title: a house rolled while live, caught up at the switch
# pools: run
# needs: zfs_rebase
#
# Plan 7.2 and 7.3. A roll replays onto the new version the live
# version's saves it does not have yet, one zfs_rebase a save: the
# first the clone form onto the new foundation version, each later one
# the dataset form with --allow-unrelated, --base the previous save,
# and the pre-apply snapshot named for that previous save. Run while
# the house is live it stops at the newest save; the switch's catch-up
# is the same replay again after the final save, so no step's
# snapshot is ever taken twice.
#
# f@1 -> f@2 changes /etc/motd, /bin/tool, adds /share/new. The live
# version h1 saves s1 (rc.conf line, etc/app.conf), s2 (rm share/a,
# etc/b), s3 (/etc/motd, against f@2's) and, after the roll, s4 (rc.conf
# line, etc/app.conf changed and chmod 600).
#
#   P6.1  step 1, the clone form onto f@2, goes through
#   P6.2  step 3 stops at the conflict over /etc/motd
#   P6.3  answered from, --continue goes through
#   P6.4  the live version written all through steps 2 and 3 (answer:
#         the writes counted), every step's outcome as without
#   P6.5  the catch-up of s4 goes through; h2 holds s1 s2 s3 and no
#         snapshot of a name twice
#   P6.6  the same saves as one rebase, h1@s4 onto f@2 (motd answered
#         from), goes through
#   P6.7  the stepwise result equals the one rebase: type, mode,
#         owner, size, link, sha256, flags
#   P6.8  answer: whether the times agree too
#   P6.9  the result holds what each side meant

zr() {		# ARGS: zfs_rebase, its exit status in RC
	echo "# zfs_rebase $*"
	"$ZR" "$@"
	RC=$?
	echo "-> exit $RC"
}

from_all() {	# RESOLUTION: every name unanswered answered from
	ms "sed -e 's/ conflict \([0-9]*\) -\$/ conflict \1 from/' \
	    -e 's/^#unanswered [0-9]*\$/#unanswered 0/' '$1' >'$1.new' &&
	    mv '$1.new' '$1'"
}

place() {	# DS DIR: a clone form result put where it belongs
	m zfs set readonly=off "$1"
	m zfs set mountpoint="$2" "$1"
	[ "$(prop mounted "$1")" = yes ] || m zfs mount "$1"
}

save() {	# NAME
	m zfs snapshot $RUN/h1@"$1"
}

spec() {	# DIR KEYWORDS: mtree's spec of DIR, its header comments out
	mtree -c -n -k "$2" -p "$1" | grep -v '^#'
}

same() {	# DIR1 DIR2 KEYWORDS: yes if the two specs are one text
	spec "$1" "$3" >"$W/spec1"
	spec "$2" "$3" >"$W/spec2"
	if diff "$W/spec1" "$W/spec2"; then echo yes; else echo no; fi
}

probe() {
	m zfs create -o mountpoint=/w $RUN/w
	W=$(dir $RUN/w)
	m zfs create -o mountpoint=/f $RUN/f
	F=$(dir $RUN/f)
	m mkdir -p "$F/etc" "$F/bin" "$F/share"
	printf 'hostname="probe"\n' >"$F/etc/rc.conf"
	printf 'Welcome to FreeBSD 1\n' >"$F/etc/motd"
	dd if=/dev/random of="$F/bin/tool" bs=64k count=1 2>/dev/null
	printf 'a\n' >"$F/share/a"
	m zfs snapshot $RUN/f@1

	m zfs clone -o mountpoint=/h1 $RUN/f@1 $RUN/h1
	H=$(dir $RUN/h1)
	printf 'sshd_enable="YES"\n' >>"$H/etc/rc.conf"
	printf 'app 1\n' >"$H/etc/app.conf"
	save s1
	m rm "$H/share/a"
	printf 'b\n' >"$H/etc/b"
	save s2
	printf 'Welcome to the house\n' >"$H/etc/motd"
	save s3

	printf 'Welcome to FreeBSD 2\n' >"$F/etc/motd"
	dd if=/dev/random of="$F/bin/tool" bs=64k count=1 2>/dev/null
	printf 'new\n' >"$F/share/new"
	m zfs snapshot $RUN/f@2

	zr -m -o "$W/r1" --from $RUN/h1@s1 --onto $RUN/f@2 --result $RUN/h2
	check P6.1 "step 1, clone form" "$RC" 0
	place $RUN/h2 /h2

	: >"$W/writing"
	(
		i=0
		while [ -e "$W/writing" ]; do
			echo "$i" >>"$H/busy"
			i=$((i + 1))
		done
		echo "$i" >"$W/writes"
	) &
	zr -m -o "$W/r2" -u -b $RUN/h1@s1 --from $RUN/h1@s2 \
	    --onto $RUN/h2 --result s1
	rc2=$RC
	zr -m -o "$W/r3" -u -b $RUN/h1@s2 --from $RUN/h1@s3 \
	    --onto $RUN/h2 --result s2
	rc3=$RC
	rm "$W/writing"
	wait
	answer P6.4 "writes to the live version during steps 2 and 3" \
	    "$(cat "$W/writes")"
	check P6.4 "step 2, with the live version written" "$rc2" 0
	check P6.2 "step 3 stops at its conflict" "$rc3" 1
	check P6.2 "conflicted names" \
	    "$(sed -n 's/^#names //p' "$W/r3.resolution")" 1
	check P6.2 "the conflict is etc/motd" "$(grep -c \
	    '^        motd conflict [0-9]* -$' "$W/r3.resolution")" 1
	from_all "$W/r3.resolution"
	zr --continue $RUN/h2@s2
	check P6.3 "answered from, --continue" "$RC" 0

	printf 'late="YES"\n' >>"$H/etc/rc.conf"
	printf 'app 2\n' >"$H/etc/app.conf"
	m chmod 600 "$H/etc/app.conf"
	save s4
	zr -m -o "$W/r4" -u -b $RUN/h1@s3 --from $RUN/h1@s4 \
	    --onto $RUN/h2 --result s3
	check P6.5 "the catch-up of s4" "$RC" 0
	check P6.5 "h2's snapshots" "$(zfs list -H -t snapshot -o name \
	    -s createtxg -d 1 $RUN/h2 | sed 's/.*@//' | tr '\n' ' ')" \
	    "s1 s2 s3 "

	zr -m -o "$W/r5" --from $RUN/h1@s4 --onto $RUN/f@2 --result $RUN/h3
	if [ "$RC" = 1 ]; then
		from_all "$W/r5.resolution"
		zr --continue $RUN/h3
	fi
	check P6.6 "s4 as one rebase" "$RC" 0
	place $RUN/h3 /h3

	K=type,mode,uid,gid,size,link,sha256digest,flags
	check P6.7 "stepwise equals one rebase" \
	    "$(same "$(dir $RUN/h2)" "$(dir $RUN/h3)" $K)" yes
	answer P6.8 "the times agree too" \
	    "$(same "$(dir $RUN/h2)" "$(dir $RUN/h3)" $K,time)"

	R=$(dir $RUN/h2)
	check P6.9 "/etc/motd is the house's" "$(cat "$R/etc/motd")" \
	    "Welcome to the house"
	check P6.9 "/bin/tool is f@2's" "$(sha256 -q "$R/bin/tool")" \
	    "$(sha256 -q "$F/bin/tool")"
	check P6.9 "/share/new came" "$(cat "$R/share/new")" new
	check P6.9 "/share/a is gone" "$([ -e "$R/share/a" ] && echo there)" ""
	check P6.9 "rc.conf has both lines" \
	    "$(grep -c -e sshd_enable -e late "$R/etc/rc.conf")" 2
	check P6.9 "app.conf is s4's, mode 600" \
	    "$(cat "$R/etc/app.conf") $(stat -f %Lp "$R/etc/app.conf")" \
	    "app 2 600"
}
