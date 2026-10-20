/*
 * What a roll fetches, src/core/src.c: family G of tests/MATRIX.md.
 * git runs for real, over the repositories tests/src-fixture.sh makes
 * in the scratch directory, reached as file:// remotes.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <time.h>

#include "check.h"
#include "core/cascade.h"
#include "core/job.h"
#include "core/site.h"
#include "core/src.h"

static struct cap c;
static char *logbuf;
static size_t loglen;
static FILE *logf;
static struct sh_job job;
static int ended;
static char why[2048];
static char build[1100];

static void
end(struct sh_job *j, const char *w, void *arg)
{
	(void) j;
	(void) arg;
	ended++;
	(void) snprintf(why, sizeof (why), "%s", w != NULL ? w : "(well)");
}

/* A fresh job, its log and its sink empty. */
static void
fresh(void)
{
	if (logf != NULL)
		(void) fclose(logf);
	free(logbuf);
	logbuf = NULL;
	CHECK((logf = open_memstream(&logbuf, &loglen)) != NULL);
	cap_reset(&c);
	sh_job_init(&job, logf, end, NULL);
	ended = 0;
}

/* The job run to its end, no step taking more than 60 s: its why. */
static const char *
run(void)
{
	struct timespec ts = { 60, 0 };
	struct kevent ev[8];
	int i, n, kq;

	CHECK((kq = kqueue()) >= 0);
	sh_job_run(&job, kq);
	while (!ended) {
		CHECK((n = kevent(kq, NULL, 0, ev, 8, &ts)) > 0);
		for (i = 0; i < n; i++)
			sh_job_event(ev[i].udata, kq, &ev[i]);
	}
	(void) close(kq);
	(void) fflush(logf);
	return (why);
}

/* CMD run in the scratch directory: whether it exited 0. */
static int
sh(const char *cmd)
{
	char buf[4096];

	(void) snprintf(buf, sizeof (buf), "cd '%s' && %s", scratch_dir,
	    cmd);
	return (system(buf) == 0);
}

/* Whether the scratch file NAME holds WANT, or, HAS, has it in it. */
static int
holds(const char *name, const char *want, int has)
{
	char buf[4096];
	size_t n;
	FILE *f;

	if ((f = fopen(scratch_path(name), "r")) == NULL)
		return (0);
	n = fread(buf, 1, sizeof (buf) - 1, f);
	(void) fclose(f);
	buf[n] = '\0';
	return (has ? strstr(buf, want) != NULL : strcmp(buf, want) == 0);
}

/* Whether the checkout has nothing of its own, ignored files too. */
static int
clean(void)
{
	return (sh("test -z \"$(git -C build/src status --porcelain "
	    "--ignored)\""));
}

/* What a roll of the foundation F of zconfig CONFIG fetches, run. */
static const char *
roll(const char *config, const char *f)
{
	struct sh_site site;
	struct sh_recipe r;
	struct sh_src s;
	const char **repos;
	char root[1100];

	(void) snprintf(root, sizeof (root), "%s", scratch_path(config));
	cap_reset(&c);
	CHECK(sh_site_load(root, &site, &c.c_err) == 0);
	repos = sh_site_repos(&site);
	CHECK(sh_recipe_resolve(root, repos, f, NULL, &r, &c.c_err,
	    NULL) == 0);
	fresh();
	s.sc_build = build;
	s.sc_config = root;
	s.sc_site = &site;
	s.sc_recipe = &r;
	sh_src_steps(&job, &s);
	(void) run();
	sh_recipe_free(&r);
	free(repos);
	sh_site_free(&site);
	return (why);
}

/* The recipe repositories of zconfig CONFIG: the job's first step. */
static const char *
recipes(const char *config)
{
	struct sh_site site;
	char root[1100];

	(void) snprintf(root, sizeof (root), "%s", scratch_path(config));
	cap_reset(&c);
	CHECK(sh_site_load(root, &site, &c.c_err) == 0);
	fresh();
	sh_src_recipes(&job, root, &site);
	sh_site_free(&site);
	return (sh_job_step(&job));
}

static int
is(const char *a, const char *b)
{
	return (a != NULL && strcmp(a, b) == 0);
}

int
main(void)
{
	char want[2048], cwd[1024];
	struct stat st;
	time_t start;
	char *url;

	cap_open(&c);
	start = time(NULL) - 1;
	(void) scratch_path("");
	(void) snprintf(build, sizeof (build), "%s", scratch_path("build"));
	CHECK(setenv("HOME", scratch_dir, 1) == 0);
	CHECK(setenv("GIT_CONFIG_NOSYSTEM", "1", 1) == 0);
	CHECK(setenv("GIT_AUTHOR_NAME", "t", 1) == 0);
	CHECK(setenv("GIT_AUTHOR_EMAIL", "t@t", 1) == 0);
	CHECK(setenv("GIT_COMMITTER_NAME", "t", 1) == 0);
	CHECK(setenv("GIT_COMMITTER_EMAIL", "t@t", 1) == 0);
	CHECK(sh("git config --global init.defaultBranch main"));
	(void) snprintf(want, sizeof (want), "sh '%s/tests/src-fixture.sh' "
	    "'%s'", getcwd(cwd, sizeof (cwd)), scratch_dir);
	CHECK(system(want) == 0);

	/* GR3, GR2: site is there and left; other, missing, cloned */
	CHECK(is(recipes("c1"), "other: clone"));
	CHECK(is(run(), "(well)"));
	CHECK(holds("c1/other/x", "x\n", 0));
	CHECK(stat(scratch_path("c1/site/new"), &st) != 0);
	/* GR1: none named */
	CHECK(recipes("c2") == NULL);
	/* GR4: a remote not there */
	CHECK(is(recipes("c3"), "gone: clone"));
	CHECK(is(run(), "gone: clone: exited 128"));

	/* GS1, GP1: no checkout yet, no patches: at releng/15.1's head */
	CHECK(is(roll("c1", "foundations/f1"), "(well)"));
	CHECK(holds("build/src/a.c", "line 25\n", 1) && clean());

	/* GS2: at an older head, with changes of its own: at the new */
	CHECK(sh("git -C up branch -f releng/15.1 main"));
	CHECK(sh("echo x >> build/src/a.c && touch build/src/u "
	    "build/src/x.o"));
	CHECK(is(roll("c1", "foundations/f1"), "(well)"));
	CHECK(holds("build/src/a.c", "LINE 25\n", 1) && clean());

	/*
	 * GP2, GP7 - GP10: pa at its head, at its top: 1, 2, 10 in that
	 * order, README and .#3.patch never; line 20 with its context
	 * moved, line 3 merged, line 5 already in
	 */
	CHECK(is(roll("c1", "foundations/f2"), "(well)"));
	CHECK(holds("build/src/seq", "0\n1\n2\n10\n", 0));
	CHECK(holds("build/src/a.c", "LINE 20\n", 1) &&
	    holds("build/src/a.c", "LINE 3\n", 1));
	CHECK(stat(scratch_path("build/patches/1/.git"), &st) == 0);

	/* GS6: the patch dropped: the file unpatched, of now */
	CHECK(sh("touch -t 200001010000 build/src/a.c"));
	CHECK(is(roll("c1", "foundations/f1"), "(well)"));
	CHECK(!holds("build/src/a.c", "LINE 20\n", 1));
	CHECK(stat(scratch_path("build/src/a.c"), &st) == 0 &&
	    st.st_mtime >= start);

	/* GP3: pa at v2, its dir 15, not its top's bad patch */
	CHECK(is(roll("c1", "foundations/f3"), "(well)"));
	CHECK(holds("build/src/seq", "0\n1\n", 0));

	/* GP4: patches/local in site's place, not the top's */
	CHECK(is(roll("c1", "foundations/f4"), "(well)"));
	CHECK(holds("build/src/seq", "0\n1\n", 0));

	/* GP5: the default's patches/top, at zconfig's top */
	CHECK(is(roll("c2", "foundations/f5"), "(well)"));
	CHECK(holds("build/src/seq", "0\n1\n", 0));

	/* GP6: two sources, in the recipe's order */
	CHECK(is(roll("c1", "foundations/f6"), "(well)"));
	CHECK(holds("build/src/seq", "0\n1\n2\n", 0));

	/* GP11: a conflict; d.patch after it never */
	CHECK(is(roll("c1", "foundations/f7"),
	    "patches/conflict: c5.patch: exited 1"));
	CHECK(holds("build/src/seq", "0\n", 0));
	CHECK(holds("build/src/a.c", "<<<<<<<", 1));

	/* GS3: the conflict left in the index: the next roll's reset */
	CHECK(is(roll("c1", "foundations/f1"), "(well)"));
	CHECK(!holds("build/src/a.c", "<<<<<<<", 1) && clean());

	/* GS7: sh_src_reset after a failure */
	CHECK(is(roll("c1", "foundations/f7"),
	    "patches/conflict: c5.patch: exited 1"));
	fresh();
	sh_src_reset(&job, build);
	CHECK(is(run(), "(well)"));
	CHECK(!holds("build/src/a.c", "<<<<<<<", 1) && clean());

	/* GP12: a base the checkout lacks, the patch not applying */
	CHECK(is(roll("c1", "foundations/f8"),
	    "patches/bogus: bogus.patch: exited 1"));

	/* GP13: a patch source not there */
	(void) snprintf(want, sizeof (want), "file://%s/nope: fetch: "
	    "exited 128", scratch_dir);
	CHECK(is(roll("c1", "foundations/f9"), want));

	/* GP14: a dir the source has not */
	(void) snprintf(want, sizeof (want), "patches: %s/patches/1/nodir: "
	    "No such file or directory", build);
	CHECK(is(roll("c1", "foundations/f10"), want));

	/* GS4: the src remote not there; GS5: the branch not on it */
	CHECK(is(roll("c3", "foundations/f1"), "src: fetch: exited 128"));
	CHECK(is(roll("c1", "foundations/f11"), "src: fetch: exited 128"));

	/* GS8, GS9: a remote as git takes it */
	url = sh_src_url("github.com/a/b");
	CHECK(is(url, "https://github.com/a/b"));
	free(url);
	url = sh_src_url("ssh://git@host/a/b");
	CHECK(is(url, "ssh://git@host/a/b"));
	free(url);

	/* GP15: natural order */
	CHECK(sh_natcmp("2.patch", "10.patch") < 0);
	CHECK(sh_natcmp("10.patch", "2.patch") > 0);
	CHECK(sh_natcmp("a2", "a10") < 0);
	CHECK(sh_natcmp("02", "2") < 0 && sh_natcmp("2", "02") > 0);
	CHECK(sh_natcmp("x", "x1") < 0 && sh_natcmp("x1", "x") > 0);
	CHECK(sh_natcmp("b", "a") > 0 && sh_natcmp("1a", "1a") == 0);
	CHECK(sh_natcmp("0010", "9") > 0);

	(void) fclose(logf);
	free(logbuf);
	cap_close(&c);
	scratch_clean();
	return (0);
}
