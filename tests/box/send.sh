# tests/box/send.sh, sourced by run.sh and build.sh from the checkout's
# top: box_send LOG WHAT commits the run's LOG and pushes it, so it
# comes back without a hand step, passed or failed. Git runs as the
# person who ran the script through sudo or doas, with their identity
# and credentials, through su, which root takes without a password;
# the commit holds LOG alone ("box: <host> WHAT: <n> ok, <m> failed").
# A push that cannot go, as when the credentials were an agent that
# sudo dropped, leaves the commit and says how to push it. SEND=no in
# the environment sends nothing.

box_send() {
	_log=$1
	_what=$2
	_u=${SUDO_USER:-${DOAS_USER:-}}
	if [ "${SEND:-yes}" = no ]; then
		echo "box: SEND=no: $_log not sent"
		return 0
	fi
	if [ -z "$_u" ]; then
		echo "box: not run through sudo or doas: $_log not sent"
		return 1
	fi
	if ! _git=$(command -v git); then
		echo "box: no git: $_log not sent"
		return 1
	fi
	_sum=$(sed -n 's/^== \([0-9]* ok, [0-9]* failed\)$/\1/p' "$_log" |
	    tail -1)
	_msg="box: $(hostname -s) $_what: ${_sum:-no summary}"
	# each command one line: the person's shell may be csh
	_g="$_git -C '$PWD'"
	_add="$_g add -- '$_log' && $_g commit -q -m '$_msg' -- '$_log'"
	if ! su "$_u" -c "$_add"; then
		echo "box: $_log not committed"
		return 1
	fi
	if su "$_u" -c "$_g pull -q --rebase --autostash && $_g push -q"; then
		echo "box: $_log sent," \
		    "$(su "$_u" -c "$_g log -1 --format=%h")"
		return 0
	fi
	echo "box: $_log committed, not pushed: git pull --rebase && git push"
	return 1
}
