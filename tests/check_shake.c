/*
 * The shaken recipe written into a tree and read from a snapshot,
 * src/core/shake.c: family I of tests/MATRIX.md, in a scratch tree.
 * The flag is nodump, which a user may set on the mac and on ZFS;
 * schg, which only root may, is the box's (tests/box/shake.c).
 */

#include <sys/stat.h>

#include "check.h"
#include "core/shake.h"
#include "core/tree.h"

static struct cap c;
static char tree[1024];

static const char text[] = "pkg {\n    nginx = true;\n}\n";

/* The tree TREE/NAME, made empty. */
static const char *
mktree(const char *name)
{
	(void) snprintf(tree, sizeof (tree), "%s", scratch_path(name));
	CHECK(mkdir(tree, 0755) == 0);
	return (tree);
}

/* TREE/REL, as a path. */
static const char *
in(const char *rel)
{
	static char buf[2048];

	(void) snprintf(buf, sizeof (buf), "%s/%s", tree, rel);
	return (buf);
}

/* The file PATH holds WANT. */
static int
holds(const char *path, const char *want)
{
	char buf[256];
	FILE *f;
	size_t n;

	if ((f = fopen(path, "r")) == NULL)
		return (0);
	n = fread(buf, 1, sizeof (buf) - 1, f);
	(void) fclose(f);
	buf[n] = '\0';
	return (strcmp(buf, want) == 0);
}

static int
write_it(const char *rel)
{
	cap_reset(&c);
	return (sh_shaken_write(tree, rel, text, sizeof (text) - 1, UF_NODUMP,
	    &c.c_err));
}

int
main(void)
{
	struct sh_node *n;
	struct stat st;

	cap_open(&c);

	/* I1: written into a tree with nothing on the way */
	mktree("t1");
	CHECK(write_it(SH_SHAKEN_HOUSE) == 0);
	CHECK(holds(in(SH_SHAKEN_HOUSE), text));
	CHECK(lstat(in(SH_SHAKEN_HOUSE), &st) == 0 && S_ISREG(st.st_mode) &&
	    (st.st_mode & 07777) == 0444 && (st.st_flags & UF_NODUMP) != 0);
	CHECK(lstat(in("var/db/shtetl"), &st) == 0 &&
	    (st.st_mode & 07777) == 0755);

	/* I2: written again: the file replaced, its flags put back on */
	CHECK(sh_shaken_write(tree, SH_SHAKEN_HOUSE, "x = 1;\n", 7,
	    UF_NODUMP, &c.c_err) == 0);
	CHECK(holds(in(SH_SHAKEN_HOUSE), "x = 1;\n"));
	CHECK(lstat(in(SH_SHAKEN_HOUSE), &st) == 0 &&
	    (st.st_flags & UF_NODUMP) != 0);

	/* I3: a link on the way: refused, nothing written through it */
	mktree("t3");
	CHECK(mkdir(in("var"), 0755) == 0);
	CHECK(mkdir(in("elsewhere"), 0755) == 0);
	CHECK(symlink(in("elsewhere"), in("var/db")) == 0);
	CHECK(write_it(SH_SHAKEN_HOUSE) == -1);
	CHECK(cap_has(&c, "var/db: a link"));
	CHECK(lstat(in("elsewhere/shtetl"), &st) != 0);

	/* I4: a link at the file's place: replaced, its target untouched */
	mktree("t4");
	CHECK(write_it("var/db/shtetl/x") == 0);
	(void) scratch_write("target", "theirs\n");
	CHECK(symlink(scratch_path("target"), in(SH_SHAKEN_HOUSE)) == 0);
	CHECK(write_it(SH_SHAKEN_HOUSE) == 0);
	CHECK(lstat(in(SH_SHAKEN_HOUSE), &st) == 0 && S_ISREG(st.st_mode));
	CHECK(holds(in(SH_SHAKEN_HOUSE), text));
	CHECK(holds(scratch_path("target"), "theirs\n"));

	/* I5: read back from the snapshot's view, parsed, its layer named */
	mktree("t5");
	CHECK(write_it(".zfs/snapshot/s1/" SH_SHAKEN_HOUSE) == 0);
	cap_reset(&c);
	n = sh_shaken_read(tree, "s1", SH_SHAKEN_HOUSE, "web@s1", &c.c_err);
	CHECK(n != NULL && strcmp(n->sn_from, "web@s1") == 0);
	CHECK(sh_obj_get(sh_obj_get(n, "pkg"), "nginx") != NULL);
	sh_node_free(n);

	/* I6: a link on the way in the snapshot: refused */
	CHECK(mkdir(in(".zfs/snapshot/s2"), 0755) == 0);
	CHECK(symlink(in(".zfs/snapshot/s1/var"),
	    in(".zfs/snapshot/s2/var")) == 0);
	cap_reset(&c);
	CHECK(sh_shaken_read(tree, "s2", SH_SHAKEN_HOUSE, "web@s2",
	    &c.c_err) == NULL);
	CHECK(cap_has(&c, ".zfs/snapshot/s2/var: a link"));

	/* I7: the file itself a link: refused */
	CHECK(mkdir(in(".zfs/snapshot/s3"), 0755) == 0);
	CHECK(mkdir(in(".zfs/snapshot/s3/var"), 0755) == 0);
	CHECK(mkdir(in(".zfs/snapshot/s3/var/db"), 0755) == 0);
	CHECK(mkdir(in(".zfs/snapshot/s3/var/db/shtetl"), 0755) == 0);
	CHECK(symlink(in(".zfs/snapshot/s1/" SH_SHAKEN_HOUSE),
	    in(".zfs/snapshot/s3/" SH_SHAKEN_HOUSE)) == 0);
	cap_reset(&c);
	CHECK(sh_shaken_read(tree, "s3", SH_SHAKEN_HOUSE, "web@s3",
	    &c.c_err) == NULL);
	CHECK(cap_has(&c, ".zfs/snapshot/s3/var/db/shtetl/house.conf: a "
	    "link"));

	/* I8: none there: the system's words */
	cap_reset(&c);
	CHECK(sh_shaken_read(tree, "s9", SH_SHAKEN_HOUSE, "web@s9",
	    &c.c_err) == NULL);
	CHECK(cap_has(&c, ".zfs/snapshot/s9: No such file or directory"));

	/* I15: the tree as it is, no snapshot: read, a link refused */
	CHECK(write_it(SH_SHAKEN_HOUSE) == 0);
	cap_reset(&c);
	n = sh_shaken_read(tree, NULL, SH_SHAKEN_HOUSE, "live", &c.c_err);
	CHECK(n != NULL && strcmp(n->sn_from, "live") == 0);
	sh_node_free(n);
	CHECK(sh_shaken_read(scratch_path("t3"), NULL, SH_SHAKEN_HOUSE, "t3",
	    &c.c_err) == NULL && cap_has(&c, "var/db: a link"));

	/* the flags off, or the scratch directory will not go */
	(void) chflags(scratch_path("t1/" SH_SHAKEN_HOUSE), 0);
	(void) chflags(scratch_path("t4/" SH_SHAKEN_HOUSE), 0);
	(void) chflags(scratch_path("t4/var/db/shtetl/x"), 0);
	(void) chflags(scratch_path("t5/.zfs/snapshot/s1/" SH_SHAKEN_HOUSE),
	    0);
	cap_close(&c);
	scratch_clean();
	return (0);
}
