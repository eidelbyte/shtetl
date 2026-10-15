/*
 * A foundation's patches, the pieces alone (src/core/patch.c):
 * tests/MATRIX.md family P, P1, P2, and what P8 and P9 rest on: the
 * files a patch touches, what a fix leaves, the pause record.
 */

#include <sys/stat.h>

#include <string.h>

#include "check.h"
#include "core/patch.h"

static struct cap c;

/* Whether LIST is the NULL-ended WANT. */
static int
same(char **list, const char *const *want)
{
	size_t i;

	for (i = 0; list[i] != NULL && want[i] != NULL; i++) {
		if (strcmp(list[i], want[i]) != 0)
			break;
	}
	if (list[i] != NULL || want[i] != NULL) {
		printf("got:");
		for (i = 0; list[i] != NULL; i++)
			printf(" %s", list[i]);
		printf("\n");
		return (0);
	}
	return (1);
}

/* P1: natural order, and a directory's patches. */
static void
test_order(void)
{
	/* 02-w and 2-y tie as numbers: then -w before -y */
	static const char *const want[] = { "1-z.patch", "02-w.patch",
	    "2-y.patch", "10-x.patch", "a2b.patch", "a10b.patch", NULL };
	static const char *const files[] = { "10-x.patch", "2-y.patch",
	    "1-z.patch", "02-w.patch", "a10b.patch", "a2b.patch",
	    "notes.txt", ".hidden.patch", "x.patch.orig", ".patch",
	    "sub/inner.patch", NULL };
	char rel[256];
	char **l;
	size_t i;

	CHECK(sh_natcmp("2.patch", "10.patch") < 0);
	CHECK(sh_natcmp("10.patch", "2.patch") > 0);
	CHECK(sh_natcmp("a2b", "a10b") < 0);
	CHECK(sh_natcmp("abc", "abd") < 0);
	CHECK(sh_natcmp("ab", "abc") < 0);
	CHECK(sh_natcmp("x", "x") == 0);
	/* equal as numbers: the bytes decide, so the order is total */
	CHECK(sh_natcmp("02", "2") < 0 && sh_natcmp("2", "02") > 0);
	CHECK(sh_natcmp("007-a", "7-b") < 0);
	CHECK(sh_natcmp("99999999999999999999", "100000000000000000000") <
	    0);
	for (i = 0; files[i] != NULL; i++) {
		(void) snprintf(rel, sizeof (rel), "p1/%s", files[i]);
		(void) scratch_write(rel, "x\n");
	}
	/* a directory named like a patch is left out */
	(void) snprintf(rel, sizeof (rel), "%s", scratch_path("p1/dir.patch"));
	CHECK(mkdir(rel, 0755) == 0);
	cap_reset(&c);
	l = sh_patch_list(scratch_path("p1"), &c.c_err);
	CHECK(l != NULL && same(l, want));
	sh_patch_list_free(l);
	l = sh_patch_list(scratch_path("p1/sub"), &c.c_err);
	CHECK(l != NULL && l[0] != NULL && strcmp(l[0], "inner.patch") == 0);
	sh_patch_list_free(l);
	(void) scratch_write("p1e/notes.txt", "x\n");
	l = sh_patch_list(scratch_path("p1e"), &c.c_err);
	CHECK(l != NULL && l[0] == NULL);
	sh_patch_list_free(l);
	cap_reset(&c);
	CHECK(sh_patch_list(scratch_path("nope"), &c.c_err) == NULL);
	CHECK(cap_has(&c, "nope: No such file or directory"));
}

/* P2: where a source is fetched from. */
static void
test_location(void)
{
	char buf[256];
	const char *u;

	u = sh_patch_url("https://example.org/p.git", buf, sizeof (buf));
	CHECK(u != NULL && strcmp(u, "https://example.org/p.git") == 0);
	u = sh_patch_url("github.com/eidelbyte/freebsd-patches", buf,
	    sizeof (buf));
	CHECK(u != NULL &&
	    strcmp(u, "https://github.com/eidelbyte/freebsd-patches") == 0);
	u = sh_patch_url("file:///var/tmp/p", buf, sizeof (buf));
	CHECK(u != NULL && strcmp(u, "file:///var/tmp/p") == 0);
	u = sh_patch_url("ssh://git@example.org/p", buf, sizeof (buf));
	CHECK(u != NULL && strcmp(u, "ssh://git@example.org/p") == 0);
	CHECK(sh_patch_url("patches/generic", buf, sizeof (buf)) == NULL);
}

/* P8, P9: the files a patch touches, as git apply --numstat -z says. */
static void
test_files(void)
{
	static const char out[] = "1\t2\tsys/kern/a.c\0"
	    "-\t-\tbin/x.dat\0"
	    "3\t0\t\0old/name.c\0new/name.c\0"
	    "0\t9\tgone.h\0";
	static const char *const want[] = { "sys/kern/a.c", "bin/x.dat",
	    "new/name.c", "gone.h", NULL };
	static const char cut[] = "1\t2\tsys/kern/a.c\0" "4\t5";
	static const char cutrename[] = "1\t2\t\0old.c";
	char **l;

	cap_reset(&c);
	l = sh_patch_files(out, sizeof (out) - 1, &c.c_err);
	CHECK(l != NULL && same(l, want));
	sh_patch_list_free(l);
	l = sh_patch_files("", 0, &c.c_err);
	CHECK(l != NULL && l[0] == NULL);
	sh_patch_list_free(l);
	CHECK(sh_patch_files(cut, sizeof (cut) - 1, &c.c_err) == NULL);
	CHECK(cap_has(&c, "git apply --numstat: unexpected output"));
	CHECK(sh_patch_files(cutrename, sizeof (cutrename) - 1,
	    &c.c_err) == NULL);
}

/* P9: what a fix leaves. */
static void
test_left(void)
{
	static char *const clean[] = { "a.c", "eq.c", "gone.c", "deep/d.c",
	    NULL };
	static char *const marker[] = { "a.c", "b.c", NULL };
	static char *const bare[] = { "bare.c", NULL };
	static char *const base[] = { "base.c", NULL };
	static char *const rej[] = { "a.c", "r.c", NULL };
	static char *const eight[] = { "eight.c", "mid.c", NULL };
	char longline[600];

	(void) scratch_write("t/a.c", "int a;\n");
	(void) scratch_write("t/eq.c", "=======\nheading\n=======\n");
	(void) scratch_write("t/deep/d.c", "int d;\n");
	(void) scratch_write("t/b.c", "int b;\n<<<<<<< ours\nint x;\n"
	    "=======\nint y;\n>>>>>>> theirs\n");
	(void) scratch_write("t/bare.c", "x\n>>>>>>>\n");
	(void) scratch_write("t/base.c", "x\n||||||| base\n");
	(void) scratch_write("t/r.c", "int r;\n");
	(void) scratch_write("t/r.c.rej", "@@ -1 +1 @@\n");
	(void) scratch_write("t/eight.c", "<<<<<<<< not one\n");
	(void) memset(longline, 'x', sizeof (longline));
	(void) memcpy(longline + 255, "<<<<<<< ", 8);
	longline[sizeof (longline) - 2] = '\n';
	longline[sizeof (longline) - 1] = '\0';
	(void) scratch_write("t/mid.c", longline);
	cap_reset(&c);
	CHECK(sh_patch_left(scratch_path("t"), clean, &c.c_err) == 0);
	CHECK(sh_patch_left(scratch_path("t"), eight, &c.c_err) == 0);
	CHECK(c.c_err.se_count == 0);
	CHECK(sh_patch_left(scratch_path("t"), marker, &c.c_err) == 1);
	CHECK(cap_has(&c, "b.c: conflict marker left"));
	cap_reset(&c);
	CHECK(sh_patch_left(scratch_path("t"), bare, &c.c_err) == 1);
	CHECK(sh_patch_left(scratch_path("t"), base, &c.c_err) == 1);
	CHECK(cap_has(&c, "base.c: conflict marker left"));
	cap_reset(&c);
	CHECK(sh_patch_left(scratch_path("t"), rej, &c.c_err) == 1);
	CHECK(cap_has(&c, "r.c.rej left") && c.c_err.se_count == 1);
}

/* P8, P9: the pause record, written and read back. */
static void
test_pause(void)
{
	struct sh_pause a, b;
	char path[1200];

	(void) memset(&a, 0, sizeof (a));
	(void) snprintf(a.pz_config, sizeof (a.pz_config), "%s",
	    "tank/zconfig@build-generic-20261008-120000");
	(void) snprintf(a.pz_version, sizeof (a.pz_version), "%s",
	    "15.1-releng-2026-10-08-0f691888dc56");
	(void) snprintf(a.pz_sha, sizeof (a.pz_sha), "%s",
	    "0f691888dc56a1b2c3d4e5f60718293a4b5c6d7e");
	(void) snprintf(a.pz_msg, sizeof (a.pz_msg), "%s",
	    "a \"quoted\" message\nwith a newline, a \\ and a tab\t");
	(void) snprintf(a.pz_base, sizeof (a.pz_base), "%s",
	    "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
	a.pz_at = 3;
	a.pz_out[0] = SH_PO_APPLIED;
	a.pz_out[1] = SH_PO_ALREADY;
	a.pz_out[2] = SH_PO_FIXED;
	a.pz_nsrc = 2;
	(void) snprintf(a.pz_commit[0], sizeof (a.pz_commit[0]), "%s",
	    "1111111111111111111111111111111111111111");
	(void) snprintf(path, sizeof (path), "%s", scratch_path("paused.conf"));
	cap_reset(&c);
	CHECK(sh_pause_write(path, &a, &c.c_err) == 0);
	CHECK(sh_pause_read(path, &b, &c.c_err) == 0);
	CHECK(strcmp(a.pz_config, b.pz_config) == 0);
	CHECK(strcmp(a.pz_version, b.pz_version) == 0);
	CHECK(strcmp(a.pz_sha, b.pz_sha) == 0);
	CHECK(strcmp(a.pz_msg, b.pz_msg) == 0);
	CHECK(strcmp(a.pz_base, b.pz_base) == 0);
	CHECK(b.pz_at == 3 && memcmp(a.pz_out, b.pz_out, 3) == 0);
	CHECK(b.pz_nsrc == 2 && strcmp(a.pz_commit[0], b.pz_commit[0]) == 0);
	CHECK(b.pz_commit[1][0] == '\0');
	/* none decided yet, no source fetched */
	a.pz_at = 0;
	a.pz_nsrc = 0;
	CHECK(sh_pause_write(path, &a, &c.c_err) == 0);
	CHECK(sh_pause_read(path, &b, &c.c_err) == 0);
	CHECK(b.pz_at == 0 && b.pz_nsrc == 0);
	CHECK(c.c_err.se_count == 0);
	/* not one */
	(void) scratch_write("bad.conf", "config = \"x\";\n");
	(void) snprintf(path, sizeof (path), "%s", scratch_path("bad.conf"));
	CHECK(sh_pause_read(path, &b, &c.c_err) == -1);
	CHECK(cap_has(&c, "bad.conf: not a pause record"));
	(void) scratch_write("bad2.conf", "config = \"x\"; version = \"v\";\n"
	    "commit = \"c\"; message = \"\"; base = \"b\"; at = 2;\n"
	    "outcomes = [ \"applied\" ]; fetched = [];\n");
	(void) snprintf(path, sizeof (path), "%s", scratch_path("bad2.conf"));
	cap_reset(&c);
	CHECK(sh_pause_read(path, &b, &c.c_err) == -1);
	CHECK(cap_has(&c, "bad2.conf: not a pause record"));
	(void) scratch_write("bad3.conf", "config = \"x\"; version = \"v\";\n"
	    "commit = \"c\"; message = \"\"; base = \"b\"; at = 1;\n"
	    "outcomes = [ \"patched\" ]; fetched = [];\n");
	(void) snprintf(path, sizeof (path), "%s", scratch_path("bad3.conf"));
	CHECK(sh_pause_read(path, &b, &c.c_err) == -1);
	/* merged, a clean three-way merge's outcome, read back */
	(void) scratch_write("merged.conf", "config = \"x\"; version = "
	    "\"v\";\ncommit = \"c\"; message = \"\"; base = \"b\"; "
	    "at = 1;\noutcomes = [ \"merged\" ]; fetched = [];\n");
	(void) snprintf(path, sizeof (path), "%s", scratch_path("merged.conf"));
	CHECK(sh_pause_read(path, &b, &c.c_err) == 0 &&
	    b.pz_out[0] == SH_PO_MERGED);
	CHECK(sh_pause_read(scratch_path("none.conf"), &b, &c.c_err) == -1);
}

int
main(void)
{
	cap_open(&c);
	test_order();
	test_location();
	test_files();
	test_left();
	test_pause();
	cap_close(&c);
	scratch_clean();
	printf("check_patch: %d checks\n", checks);
	return (0);
}
