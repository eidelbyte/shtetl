#!/bin/sh
# The command line end to end (tests/MATRIX.md, family CL, and the
# cells of other families a case names). Each case runs ./shtetl from
# the top of the tree and compares its standard output, standard
# error and exit status with tests/cli/NAME.out, NAME.err and NAME.rc;
# a missing .out or .err means that stream must be empty.
#
# UPDATE=1 writes what each case printed as its expectation instead
# of comparing, for review with git diff before it is committed.
set -u
cd "$(dirname "$0")/.." || exit 1
exp=tests/cli
tmp=$(mktemp -d "${TMPDIR:-/tmp}/shtetl-cli.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT
if [ ! -x ./shtetl ]; then
	echo "cli: no ./shtetl: run make first"
	exit 1
fi
fails=0
n=0

# t NAME ARGS...: one case.
t() {
	name=$1
	shift
	n=$((n + 1))
	./shtetl "$@" >"$tmp/out" 2>"$tmp/err"
	echo $? >"$tmp/rc"
	if [ "${UPDATE:-0}" = 1 ]; then
		cp "$tmp/rc" "$exp/$name.rc"
		for s in out err; do
			if [ -s "$tmp/$s" ]; then
				cp "$tmp/$s" "$exp/$name.$s"
			else
				rm -f "$exp/$name.$s"
			fi
		done
		return
	fi
	bad=
	for s in rc out err; do
		if [ -f "$exp/$name.$s" ]; then
			cmp -s "$exp/$name.$s" "$tmp/$s" || bad="$bad $s"
		elif [ -s "$tmp/$s" ]; then
			bad="$bad $s"
		fi
	done
	if [ -n "$bad" ]; then
		echo "FAIL cli $name:$bad differ"
		for s in $bad; do
			diff -u "$exp/$name.$s" "$tmp/$s" 2>/dev/null ||
			    sed 's/^/    /' "$tmp/$s"
		done
		fails=$((fails + 1))
	fi
}

# CL1: no arguments is a usage error; -h is the usage asked for.
t cl1-none
t cl1-help -h
# CL2: an unknown command or option, or -C with no directory.
t cl2-command frobnicate
t cl2-option -x config show
t cl2-optarg -C
# CL3, D17: no -C, so the config root is looked for, and a portable
# shtetl has no ZFS to look in. On the freebsd flavor this would ask
# the machine's own pools; tests/box covers it there.
if [ "$(cat build/.flavor 2>/dev/null)" = portable ]; then
	t cl3-noroot config show
	t d17-spaces spaces
fi
# CL4, and CW1 - CW6 on a real tree: the merged recipe.
t cl4-nginx -C tests/conf/site config show containers/nginx
t cl4-generic -C tests/conf/site config show foundations/generic.conf
# CL5, CW7: the site's settings, with and without shtetl.conf.
t cl5-site -C tests/conf/site config show
t cl5-builtin -C tests/conf/bare config show
# CL6: refusals: on stderr, nothing on stdout, exit 1.
t cl6-bad -C tests/conf/site config show containers/bad
t cl6-drop -C tests/conf/site config show containers/drop
t cl6-notdir -C tests/conf/nonexistent config show
t cl6-norecipe -C tests/conf/site config show containers/nope
# B1: bootstrap's own usage errors.
t b1-none bootstrap
t b1-option bootstrap --lake tank/shtetl --frob
t b1-operand bootstrap --lake
t b1-recipes bootstrap --config tank/zconfig --recipes
t b4-twice bootstrap --lake tank/a --lake tank/b
# F18: build's usage errors
t f18-none build
t f18-two build generic gaming
t f18-option build --frob generic
t f18-msg build generic -m
# P18: a paused build's verbs
t p18-two build --continue --skip generic
t p18-abortmsg build --abort -m why generic
t p18-nozfs build --continue generic

# F32: pkg-upgrade
t f32-none pkg-upgrade
t f32-two pkg-upgrade generic gaming
t f32-option pkg-upgrade --frob generic
t f32-msg pkg-upgrade generic -m
t f32-nozfs pkg-upgrade generic

# M7: beam
t m7-none beam
t m7-two beam generic gaming
t m7-option beam --frob generic
t m7-version beam generic --version
t m7-nozfs beam generic
if [ "$(cat build/.flavor 2>/dev/null)" = portable ]; then
	t b1-nozfs bootstrap --lake tank/shtetl -y
	t f18-nozfs -C tests/conf/site build generic -y
fi
# CL7: more than one recipe.
t cl7-two -C tests/conf/site config show containers/nginx systems/x

if [ "${UPDATE:-0}" = 1 ]; then
	echo "cli: $n expectations written to $exp"
	exit 0
fi
if [ $fails -ne 0 ]; then
	echo "cli: $fails of $n cases failed"
	exit 1
fi
echo "ok   cli ($n cases)"
