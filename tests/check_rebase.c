/*
 * zfs_rebase as shtetl runs it (planning.md 9, 7.6.1, 7.7):
 * tests/MATRIX.md family W. The real rebaser against a script that
 * logs its words and exits as told; the resolution read and answered;
 * the fake rebaser held to the tool's contract over the fake ZFS.
 */

#include <sys/stat.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "core/rebase.h"
#include "core/zfs.h"
#include "rbfake.h"
#include "zfake.h"

static struct cap c;
static struct sh_zfs *z;
static char tool[1200], words[1200], exitf[1200], logf[1200];

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

/* The file PATH whole, or "(none)". */
static const char *
slurp(const char *path)
{
	static char buf[8192];
	size_t n;
	FILE *f;

	if ((f = fopen(path, "r")) == NULL)
		return ("(none)");
	n = fread(buf, 1, sizeof (buf) - 1, f);
	buf[n] = '\0';
	(void) fclose(f);
	return (buf);
}

/* The last line the script logged. */
static const char *
said(void)
{
	static char last[1024];
	const char *all, *p, *nl;

	all = slurp(words);
	last[0] = '\0';
	for (p = all; *p != '\0'; p = nl + 1) {
		if ((nl = strchr(p, '\n')) == NULL)
			break;
		(void) snprintf(last, sizeof (last), "%.*s", (int)(nl - p), p);
	}
	return (last);
}

/* The script's next exit. */
static void
exits(const char *rc)
{
	(void) scratch_write("exit", rc);
}

/* W1 - W3: the real rebaser's words and exits. */
static void
test_real(void)
{
	struct sh_rebaser real = sh_zfs_rebaser;
	struct sh_rbstart s;
	char text[4096];
	int log;

	(void) snprintf(words, sizeof (words), "%s", scratch_path("words"));
	(void) snprintf(exitf, sizeof (exitf), "%s", scratch_path("exit"));
	(void) snprintf(logf, sizeof (logf), "%s", scratch_path("log"));
	(void) snprintf(text, sizeof (text), "#!/bin/sh\n"
	    "echo \"$*\" >> %s\necho the tool said so\n"
	    "[ -f %s ] && exit $(cat %s)\nexit 0\n", words, exitf, exitf);
	(void) scratch_write("tool/zfs_rebase", text);
	(void) snprintf(tool, sizeof (tool), "%s",
	    scratch_path("tool/zfs_rebase"));
	CHECK(chmod(tool, 0755) == 0);
	real.rb_tool = tool;
	CHECK((log = open(logf, O_WRONLY | O_CREAT | O_TRUNC, 0644)) != -1);

	/* W1: a start, each form */
	(void) memset(&s, 0, sizeof (s));
	s.rs_from = "p/x@s1";
	s.rs_onto = "p/f@v2";
	s.rs_result = "p/xs";
	s.rs_manifest = "/run/m1";
	exits("0");
	cap_reset(&c);
	CHECK(real.rb_start(&real, &s, log, &c.c_err) == SH_RB_DONE);
	CHECK(is(said(), "--from p/x@s1 --onto p/f@v2 --result p/xs -o "
	    "/run/m1"));
	s.rs_from = "p/x@s2";
	s.rs_onto = "p/xs";
	s.rs_result = "s1";
	s.rs_base = "p/x@s1";
	s.rs_manifest = "/run/m2";
	CHECK(real.rb_start(&real, &s, log, &c.c_err) == SH_RB_DONE);
	CHECK(is(said(), "--allow-unrelated --base p/x@s1 --from p/x@s2 "
	    "--onto p/xs --result s1 -o /run/m2"));
	/* W2: every exit the tool gives, and one it does not */
	exits("1");
	CHECK(real.rb_start(&real, &s, log, &c.c_err) == SH_RB_GATE);
	exits("2");
	CHECK(real.rb_start(&real, &s, log, &c.c_err) == SH_RB_REFUSED);
	exits("3");
	CHECK(real.rb_start(&real, &s, log, &c.c_err) == SH_RB_FAILED);
	CHECK(c.c_err.se_count == 0);
	exits("7");
	CHECK(real.rb_start(&real, &s, log, &c.c_err) == -1);
	CHECK(c.c_err.se_count == 1 && cap_has(&c, "7"));
	CHECK(is(sh_rb_said(SH_RB_GATE), "stopped at conflicts"));
	/* its output in the log */
	CHECK(strstr(slurp(logf), "the tool said so\n") != NULL);
	/* W3: the verbs, by IDENT */
	exits("0");
	CHECK(real.rb_continue(&real, "p/xs", 0, log, &c.c_err) == 0);
	CHECK(is(said(), "--continue p/xs"));
	exits("1");
	CHECK(real.rb_continue(&real, "p/xs", SH_RB_NOMERGE, log,
	    &c.c_err) == SH_RB_GATE);
	CHECK(is(said(), "--continue --no-merge p/xs"));
	CHECK(real.rb_continue(&real, "p/xs", SH_RB_NOMERGE | SH_RB_PICK, log,
	    &c.c_err) == SH_RB_GATE);
	CHECK(is(said(), "--continue --no-merge -i p/xs"));
	exits("0");
	CHECK(real.rb_abort(&real, "p/xs", log, &c.c_err) == 0);
	CHECK(is(said(), "--abort p/xs"));
	/* not there at all */
	real.rb_tool = "no-such-zfs-rebase";
	cap_reset(&c);
	CHECK(real.rb_abort(&real, "p/xs", log, &c.c_err) == -1);
	CHECK(cap_has(&c, "no-such-zfs-rebase: not found"));
	cap_reset(&c);
	CHECK(real.rb_continue(&real, "p/xs", SH_RB_PICK, log,
	    &c.c_err) == -1);
	CHECK(cap_has(&c, "no-such-zfs-rebase: not found"));
	(void) close(log);
	{
		char mnt[256];

		sh_rb_privmnt("tank/run/containers/x-staging/var", mnt,
		    sizeof (mnt));
		CHECK(is(mnt, "/var/db/zfs_rebase/tank/run/containers/"
		    "x-staging/var/mnt"));
	}
}

static const char sample[] =
	"#rebase-resolution 5\n"
	"#base t/f@v1 1\n#from t/x@s1 2\n#onto t/f@v2 3\n"
	"#mode strict\n#names 6\n#unanswered 3\n"
	"/\n"
	"    etc/\n"
	"        pwd.db conflict 1 -\n"
	"        master.passwd conflict 2 -\n"
	"        ..\n"
	"    a\\040b conflict 3 onto\n"
	"    d/ conflict 4 -\n"
	"        f conflict 4 keep\n"
	"        ..\n"
	"    k drift keep\n"
	"    ..\n";

/* The sample with /etc/pwd.db answered keep, and #unanswered 2. */
static const char answered[] =
	"#rebase-resolution 5\n"
	"#base t/f@v1 1\n#from t/x@s1 2\n#onto t/f@v2 3\n"
	"#mode strict\n#names 6\n#unanswered 2\n"
	"/\n"
	"    etc/\n"
	"        pwd.db conflict 1 keep\n"
	"        master.passwd conflict 2 -\n"
	"        ..\n"
	"    a\\040b conflict 3 onto\n"
	"    d/ conflict 4 -\n"
	"        f conflict 4 keep\n"
	"        ..\n"
	"    k drift keep\n"
	"    ..\n";

/* TEXT read as a resolution: 0, or -1 with the reason in c. */
static int
res(const char *text, struct sh_resolution *r)
{
	(void) scratch_write("res", text);
	cap_reset(&c);
	sh_res_free(r);
	return (sh_res_read(scratch_path("res"), r, &c.c_err));
}

/* W4, W5: the resolution read, answered, written whole. */
static void
test_resolution(void)
{
	static const char *const keep[] = { "/etc/pwd.db", "/d/f", "/nope" };
	struct sh_resolution r;

	(void) memset(&r, 0, sizeof (r));
	CHECK(res(sample, &r) == 0);
	CHECK(r.rs_n == 6 && r.rs_names == 6);
	CHECK(is(r.rs_lines[0].rl_path, "/etc/pwd.db"));
	CHECK(r.rs_lines[0].rl_conflict && r.rs_lines[0].rl_group == 1);
	CHECK(is(r.rs_lines[1].rl_path, "/etc/master.passwd"));
	CHECK(is(r.rs_lines[2].rl_path, "/a b"));
	CHECK(is(r.rs_lines[2].rl_choice, "onto"));
	CHECK(is(r.rs_lines[3].rl_path, "/d"));
	CHECK(is(r.rs_lines[4].rl_path, "/d/f"));
	CHECK(is(r.rs_lines[5].rl_path, "/k") && !r.rs_lines[5].rl_conflict);
	CHECK(sh_res_unanswered(&r) == 3);
	CHECK(sh_res_find(&r, "/a b") == &r.rs_lines[2]);
	CHECK(sh_res_find(&r, "/a") == NULL);
	/* W5: keep where still "-"; the rest byte for byte */
	CHECK(sh_res_keep(&r, keep, 3) == 1);
	CHECK(sh_res_unanswered(&r) == 2);
	CHECK(sh_res_write(scratch_path("res"), &r, &c.c_err) == 0);
	CHECK(is(slurp(scratch_path("res")), answered));
	CHECK(access(scratch_path("res.shtetl"), F_OK) != 0);
	CHECK(res(slurp(scratch_path("res")), &r) == 0);
	CHECK(sh_res_unanswered(&r) == 2);
	/* W4: refused */
	CHECK(res("#names 1\n#unanswered 2\n/\n    a conflict 1 -\n..\n",
	    &r) == -1);
	CHECK(cap_has(&c, "#unanswered says 2 but 1 of the lines are"));
	CHECK(res("#unanswered 0\n/\n    a frob\n..\n", &r) == -1);
	CHECK(cap_has(&c, "res:3: not a resolution's line"));
	CHECK(res("#unanswered 0\n/\n    a conflict 1 maybe\n..\n", &r) ==
	    -1);
	CHECK(res("#unanswered 0\n/\n    a\\09 conflict 1 keep\n..\n", &r) ==
	    -1);
	CHECK(res("#unanswered 0\n/\n    d/\n    a conflict 1 keep\n..\n",
	    &r) == -1);
	CHECK(cap_has(&c, "the tree does not close"));
	CHECK(res("/\n..\n", &r) == -1);
	CHECK(cap_has(&c, "no #unanswered"));
	CHECK(res("#unanswered 0\n..\n", &r) == -1);
	sh_res_free(&r);
}

/* The fake picker: every line still "-" answered keep. */
static int
pick_all(const char *path, void *arg)
{
	struct sh_resolution r;
	const char *all[16];
	size_t i, n;
	struct sh_err *e = arg;

	(void) memset(&r, 0, sizeof (r));
	if (sh_res_read(path, &r, e) != 0) {
		sh_res_free(&r);
		return (-1);
	}
	for (i = n = 0; i < r.rs_n && n < 16; i++)
		all[n++] = r.rs_lines[i].rl_path;
	(void) sh_res_keep(&r, all, n);
	(void) sh_res_write(path, &r, e);
	sh_res_free(&r);
	return (0);
}

static const char *
prop(const char *ds, const char *name)
{
	static char val[256];

	if (sh_zfs_get(z, ds, name, val, sizeof (val), NULL, &c.c_err) != 0)
		return ("(refused)");
	return (val);
}

/* W6: the fake rebaser, held to the tool's contract. */
static void
test_fake(void)
{
	static const char *const gate[] = { "/etc/a", "/etc/b", NULL };
	static const char *const snaps[] = { "tank/f@v1", "tank/f@v2" };
	const char *xs1 = "tank/x@s1", *xs2 = "tank/x@s2";
	struct sh_rbstart s;
	char m[1200];

	zfake_reset();
	rbfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/f");
	CHECK(sh_zfs_snapshot(z, snaps, 1, NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_clone(z, "tank/f@v1", "tank/x", NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, &xs1, 1, NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, &xs2, 1, NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_snapshot(z, snaps + 1, 1, NULL, 0, &c.c_err) == 0);
	(void) memset(&s, 0, sizeof (s));
	/* the clone form: the result unmounted, mountpoint none, readonly */
	(void) snprintf(m, sizeof (m), "%s", scratch_path("m-xs-1"));
	s.rs_from = xs1;
	s.rs_onto = "tank/f@v2";
	s.rs_result = "tank/xs";
	s.rs_manifest = m;
	CHECK(rbfake.rb_start(&rbfake, &s, -1, &c.c_err) == SH_RB_DONE);
	CHECK(is(prop("tank/xs", "origin"), "tank/f@v2"));
	CHECK(is(prop("tank/xs", "mountpoint"), "none"));
	CHECK(is(prop("tank/xs", "readonly"), "on"));
	CHECK(is(prop("tank/xs", "mounted"), "no"));
	CHECK(rbfake.rb_start(&rbfake, &s, -1, &c.c_err) == SH_RB_REFUSED);
	/* the dataset form: the pre-apply snapshot, at a gate */
	rbfake_conflicts("tank/xs", 3, gate);
	(void) snprintf(m, sizeof (m), "%s", scratch_path("m-xs-3"));
	s.rs_from = xs2;
	s.rs_onto = "tank/xs";
	s.rs_result = "s1";
	s.rs_base = xs1;
	CHECK(rbfake.rb_start(&rbfake, &s, -1, &c.c_err) == SH_RB_GATE);
	CHECK(zfake_snapshot("tank/xs@s1"));
	CHECK(is(prop("tank/xs", "zfs_rebase:phase"), "conflicts"));
	CHECK(rbfake_resolution("tank/xs") != NULL);
	CHECK(rbfake.rb_continue(&rbfake, "tank/xs", 0, -1, &c.c_err) ==
	    SH_RB_GATE);
	rbfake_picker(pick_all, &c.c_err);
	CHECK(rbfake.rb_continue(&rbfake, "tank/xs", SH_RB_PICK |
	    SH_RB_NOMERGE, -1, &c.c_err) == SH_RB_GATE);
	CHECK(rbfake.rb_continue(&rbfake, "tank/xs", 0, -1, &c.c_err) ==
	    SH_RB_DONE);
	CHECK(is(prop("tank/xs", "zfs_rebase:phase"), "-"));
	CHECK(rbfake.rb_continue(&rbfake, "tank/xs", 0, -1, &c.c_err) ==
	    SH_RB_REFUSED);
	/* --abort: the dataset rolled back, the pre-apply gone */
	rbfake_conflicts("tank/xs", 4, gate);
	(void) snprintf(m, sizeof (m), "%s", scratch_path("m-xs-4"));
	s.rs_result = "s2";
	CHECK(rbfake.rb_start(&rbfake, &s, -1, &c.c_err) == SH_RB_GATE);
	CHECK(rbfake.rb_abort(&rbfake, "tank/xs", -1, &c.c_err) == SH_RB_DONE);
	CHECK(!zfake_snapshot("tank/xs@s2") && zfake_snapshot("tank/xs@s1"));
	CHECK(is(prop("tank/xs", "zfs_rebase:phase"), "-"));
	/* the clone form's abort: the clone gone */
	rbfake_conflicts("tank/xt", 1, gate);
	(void) snprintf(m, sizeof (m), "%s", scratch_path("m-xt-1"));
	s.rs_from = xs1;
	s.rs_onto = "tank/f@v2";
	s.rs_result = "tank/xt";
	s.rs_base = NULL;
	CHECK(rbfake.rb_start(&rbfake, &s, -1, &c.c_err) == SH_RB_GATE);
	CHECK(rbfake.rb_abort(&rbfake, "tank/xt", -1, &c.c_err) == SH_RB_DONE);
	CHECK(!zfake_exists("tank/xt"));
	/* refused, and failed part way */
	rbfake_exit("tank/xu", 1, SH_RB_REFUSED);
	s.rs_result = "tank/xu";
	(void) snprintf(m, sizeof (m), "%s", scratch_path("m-xu-1"));
	CHECK(rbfake.rb_start(&rbfake, &s, -1, &c.c_err) == SH_RB_REFUSED);
	CHECK(!zfake_exists("tank/xu"));
	rbfake_exit("tank/xu", 2, SH_RB_FAILED);
	(void) snprintf(m, sizeof (m), "%s", scratch_path("m-xu-2"));
	CHECK(rbfake.rb_start(&rbfake, &s, -1, &c.c_err) == SH_RB_FAILED);
	CHECK(rbfake.rb_abort(&rbfake, "tank/xu", -1, &c.c_err) == SH_RB_DONE);
	CHECK(!zfake_exists("tank/xu"));
	CHECK(strstr(rbfake_log(), "start --from tank/x@s2 --onto tank/xs "
	    "--result s1 --base\n  base tank/x@s1\n") != NULL);
	CHECK(strstr(rbfake_log(), "continue tank/xs --no-merge -i\n") !=
	    NULL);
}

int
main(void)
{
	cap_open(&c);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_real();
	test_resolution();
	test_fake();
	sh_zfs_close(z);
	rbfake_reset();
	cap_close(&c);
	scratch_clean();
	printf("check_rebase: %d checks\n", checks);
	return (0);
}
