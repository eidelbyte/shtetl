#!/bin/sh
# The repositories and configuration roots tests/check_src.c runs on,
# made in the scratch directory $1 (family G of tests/MATRIX.md). The
# caller sets HOME and git's identity.

set -e
S=$1
cd "$S"

# up, the src remote. releng/15.1 at A; main at B, which moves a.c's
# lines from 11 on by two and changes lines 5 and 25.
git init -q up
i=1
while [ $i -le 30 ]; do
	echo "line $i"
	i=$((i + 1))
done > up/a.c
echo 0 > up/seq
echo '*.o' > up/.gitignore
mkdir -p up/sys/conf
printf '%s\n' '#!/bin/sh' 'TYPE="FreeBSD"' 'REVISION="15.1"' \
    'BRANCH=${BRANCH_OVERRIDE:-RELEASE}' > up/sys/conf/newvers.sh
git -C up add -A
git -C up commit -q -m A
git -C up branch releng/15.1
# stable/14: a tree with no newvers.sh
git -C up checkout -q -b stable/14
git -C up rm -q sys/conf/newvers.sh
git -C up commit -q -m 'no newvers.sh'
git -C up checkout -q main

# The patches, made against A in a clone of it, so their bases are
# blobs up has; seq's chain each on the last, a base up never has.
git clone -q up w
mkdir p
mkpatch() {
	git -C w diff > "$1"
	git -C w checkout -q -- .
}
edit() {
	sed "s/^line $1\$/$2/" w/a.c > w/x
	mv w/x w/a.c
}
edit 20 'LINE 20'
mkpatch p/x-line20.patch
edit 3 'LINE 3'
mkpatch p/y-line3.patch
edit 5 'LINE 5'
mkpatch p/z-line5.patch
edit 5 'line five'
mkpatch p/c5.patch
echo 1 >> w/seq
git -C w diff > p/s1.patch
git -C w commit -q -am s1
echo 2 >> w/seq
git -C w diff > p/s2.patch
git -C w commit -q -am s2
echo 10 >> w/seq
git -C w diff > p/s10.patch
printf '%s\n' 'diff --git a/a.c b/a.c' 'index 1234567..89abcde 100644' \
    '--- a/a.c' '+++ b/a.c' '@@ -1,2 +1,2 @@' '-nope' '+yes' ' line 2' \
    > p/bogus.patch
echo 'not a patch' > p/garbage.patch

awk 'NR == 11 { print "added a"; print "added b" } { print }' up/a.c |
    sed 's/^line 5$/LINE 5/; s/^line 25$/LINE 25/' > up/x
mv up/x up/a.c
git -C up commit -q -am B

# pa, a patch source. At the tag v2, 15/s1.patch, and a bad patch at
# its top; at its head, the top's patches among others.
git init -q pa
mkdir pa/15
cp p/s1.patch pa/15/s1.patch
cp p/garbage.patch pa/bad.patch
git -C pa add -A
git -C pa commit -q -m v2
git -C pa tag v2
git -C pa rm -q -r 15 bad.patch
cp p/s1.patch pa/1.patch
cp p/s2.patch pa/2.patch
cp p/s10.patch pa/10.patch
cp p/garbage.patch 'pa/.#3.patch'
echo 'not a patch' > pa/README
cp p/x-line20.patch p/y-line3.patch p/z-line5.patch pa/
git -C pa add -A
git -C pa commit -q -m head

# siterepo, a recipe repository: f4 and its patches in its own place;
# otherrepo, one zconfig is missing.
git init -q siterepo
mkdir -p siterepo/foundations siterepo/patches/local
printf '%s\n' 'src_branch = "releng/15.1";' \
    'patches { "patches/local" {} }' > siterepo/foundations/f4.conf
cp p/s1.patch siterepo/patches/local/
# r6 (tests/check_roll.c): a kernel config of its own, in its place
mkdir -p siterepo/kern
echo 'include GENERIC' > siterepo/kern/MINE
printf '%s\n' 'src_branch = "releng/15.1";' 'kernconf = "kern/MINE";' \
    > siterepo/foundations/r6.conf
git -C siterepo add -A
git -C siterepo commit -q -m f4
git init -q otherrepo
echo x > otherrepo/x
git -C otherrepo add -A
git -C otherrepo commit -q -m x

# c1, a zconfig: site cloned, then its remote moved on. A garbage
# patch in patches/local at the top, where f4's are not.
mkdir -p c1/foundations c1/patches/local c1/patches/two \
    c1/patches/conflict c1/patches/bogus
git clone -q siterepo c1/site
echo new > siterepo/new
git -C siterepo add -A
git -C siterepo commit -q -m new
cp p/garbage.patch c1/patches/local/
cp p/s2.patch c1/patches/two/
cp p/c5.patch c1/patches/conflict/
cp p/s1.patch c1/patches/conflict/d.patch
cp p/bogus.patch c1/patches/bogus/
printf '%s\n' 'remotes {' "    src = \"file://$S/up\";" \
    "    recipes { site = \"file://$S/siterepo\";" \
    "        other = \"file://$S/otherrepo\"; }" '}' \
    'properties { lake { compression = "zstd-9"; } }' \
    'build { make_jobs = 3; }' > c1/shtetl.conf
f() {
	n=$1
	shift
	printf '%s\n' 'src_branch = "releng/15.1";' "$@" > c1/foundations/$n.conf
}
f f1
f f2 "patches { \"file://$S/pa\" {} }"
f f3 "patches { \"file://$S/pa\" { ref = \"v2\"; dir = \"15\"; } }"
f f6 "patches { \"file://$S/pa\" { ref = \"v2\"; dir = \"15\"; }" \
    '    "patches/two" {} }'
f f7 'patches { "patches/conflict" {} }'
f f8 'patches { "patches/bogus" {} }'
f f9 "patches { \"file://$S/nope\" {} }"
f f10 "patches { \"file://$S/pa\" { dir = \"nodir\"; } }"
printf '%s\n' 'src_branch = "releng/9.9";' > c1/foundations/f11.conf

# The rolls of tests/check_roll.c: r1 with datasets, a placeholder
# among them; r2 a conflict; r3 and r4 a ver refused before and after
# the checkout; r5 a tree with no REVISION.
f r1 'datasets { "usr/local" {} var { atime = true; } }' \
    'kernconf = <<EOD' 'device vmm' 'EOD' 'buildopts = "WITHOUT_LIB32=yes";'
f r2 'patches { "patches/conflict" {} }'
f r3 'ver = ">=16";'
printf '%s\n' 'src_branch = "main";' 'ver = ">=16";' > c1/foundations/r4.conf
printf '%s\n' 'src_branch = "stable/14";' > c1/foundations/r5.conf
# r7: packages, commands, commands-external that write to the fakes'
# log; r8 commands alone; r9 packages and a resolv.conf of its own.
mkdir -p c1/common
echo 'commands { one = "echo 0"; }' > c1/common/c7.conf
f r7 'import = "common/c7.conf";' 'pkg = [ "a", "b" ];' \
    'datasets { "usr/local" {} }' \
    'commands { one = "echo 1"; two = "echo 2"; }' \
    'commands-external { ext = <<EOD' \
    'echo "external $SHTETL_NAME $SHTETL_KIND $SHTETL_ROOT" >> "$FAKE_LOG"' \
    'EOD' '}'
f r8 'commands { only = "true"; }'
f r9 'pkg = [ "a" ];' 'commands-external { resolv = <<EOD' \
    'echo mine > "$SHTETL_ROOT/etc/resolv.conf"' 'EOD' '}'
# the host's resolv.conf, as the tests lend it
echo 'nameserver 127.0.0.1' > resolv.conf

# c2: shtetl.conf's foundation block gives a directory, at the top.
mkdir -p c2/foundations c2/patches/top
cp p/s1.patch c2/patches/top/
printf '%s\n' 'remotes {' "    src = \"file://$S/up\";" '}' \
    'foundation { patches { "patches/top" {} } }' > c2/shtetl.conf
printf '%s\n' 'src_branch = "releng/15.1";' > c2/foundations/f5.conf

# c3: remotes that are not there.
mkdir -p c3/foundations
printf '%s\n' 'remotes {' "    src = \"file://$S/nope\";" \
    "    recipes { gone = \"file://$S/nope\"; }" '}' > c3/shtetl.conf
printf '%s\n' 'src_branch = "releng/15.1";' > c3/foundations/f1.conf
