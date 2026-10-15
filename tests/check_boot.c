/*
 * shtetl bootstrap and its property table (planning.md 4.2, 4.3):
 * tests/MATRIX.md family B, the cells the core answers (B2 - B15,
 * B17; B1 and B16 are the command line's), over the fake ZFS. The
 * recipes are cloned from a git repository made in the scratch
 * directory, so the clone runs the real git.
 */

#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "core/boot.h"
#include "core/parse.h"
#include "core/site.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

static struct cap c;
static struct sh_zfs *z;
static struct sh_spaces s;
static struct sh_bootreq req;
static struct sh_bplan *p;
static char *out;
static size_t outlen;
static char confdir[1100];

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

static int
has(const char *text, const char *want)
{
	if (strstr(text, want) == NULL) {
		printf("not found: \"%s\" in:\n%s\n", want, text);
		return (0);
	}
	return (1);
}

/* A property of DS in the fake, with its source. */
static const char *
prop(const char *ds, const char *name, enum sh_zsrc *src)
{
	static char val[SH_ZFS_VALMAX];

	CHECK(sh_zfs_get(z, ds, name, val, sizeof (val), src, &c.c_err) == 0);
	return (val);
}

static int
local(const char *ds, const char *name, const char *want)
{
	enum sh_zsrc src;
	const char *v = prop(ds, name, &src);

	if (src != SH_ZSRC_LOCAL || strcmp(v, want) != 0) {
		printf("%s %s: \"%s\" (source %d), wanted \"%s\", local\n",
		    ds, name, v, (int)src, want);
		return (0);
	}
	return (1);
}

/*
 * Two pools with what a person has before bootstrap: tank/shtetl for
 * the lake, fast/run for zrun, tank/zconfig mounted at a scratch
 * directory, tank/zbuild at /usr/src.
 */
static void
world(void)
{
	zfake_reset();
	zfake_pool("tank");
	zfake_pool("fast");
	zfake_fs("tank/shtetl");
	zfake_fs("fast/run");
	zfake_fs("tank/zconfig");
	zfake_set("tank/zconfig", "mountpoint", confdir);
	zfake_set("tank/zconfig", "mounted", "yes");
	zfake_fs("tank/zbuild");
	zfake_set("tank/zbuild", "mountpoint", "/usr/src");
	(void) memset(&req, 0, sizeof (req));
}

static void
ask(const char *ds, enum sh_space sp)
{
	req.br_ds[req.br_n] = ds;
	req.br_sp[req.br_n++] = sp;
}

static void
discover(void)
{
	sh_spaces_free(&s);
	CHECK(sh_spaces_discover(z, &s, &c.c_err) == 0);
}

/* The plan for req, with SITE; its table into out. 0 or -1. */
static int
plan(const struct sh_node *site)
{
	FILE *f;
	int r;

	discover();
	cap_reset(&c);
	r = sh_boot_plan(z, &s, site, &req, p, &c.c_err);
	free(out);
	out = NULL;
	f = open_memstream(&out, &outlen);
	CHECK(f != NULL);
	if (r == 0)
		sh_boot_show(f, p);
	CHECK(fclose(f) == 0);
	return (r);
}

/* The plan applied; what it said into out. 0 or -1. */
static int
apply(void)
{
	FILE *f;
	int r;

	free(out);
	out = NULL;
	f = open_memstream(&out, &outlen);
	CHECK(f != NULL);
	r = sh_boot_apply(z, p, f, &c.c_err);
	CHECK(fclose(f) == 0);
	return (r);
}

static void
refused(const char *want)
{
	if (plan(NULL) == 0 || !cap_has(&c, want))
		printf("plan: wanted a refusal with \"%s\", got:\n%s", want,
		    cap_text(&c));
	CHECK(c.c_err.se_count >= 1);
	CHECK(cap_has(&c, want));
}

/* B2 - B7: what the plan refuses. */
static void
test_refused(void)
{
	world();
	ask("tank/nope", SH_SP_LAKE);
	refused("tank/nope: no such filesystem");
	world();
	ask("tank//x", SH_SP_LAKE);
	refused("tank//x: invalid dataset name");
	/* B3 */
	world();
	zfake_set("tank/shtetl", SH_SPACE_PROP, "lake");
	ask("tank/shtetl", SH_SP_LAKE);
	refused("tank/shtetl: already a lake root");
	world();
	zfake_set("tank/shtetl", SH_SPACE_PROP, "run");
	ask("tank/shtetl", SH_SP_LAKE);
	refused("tank/shtetl: already a run root");
	/* B4 */
	world();
	zfake_fs("tank/c2");
	ask("tank/zconfig", SH_SP_CONFIG);
	ask("tank/c2", SH_SP_CONFIG);
	refused("two config roots: tank/zconfig and tank/c2");
	world();
	zfake_set("tank/zbuild", SH_SPACE_PROP, "build");
	zfake_fs("fast/src");
	ask("fast/src", SH_SP_BUILD);
	refused("two build roots: tank/zbuild and fast/src");
	world();
	zfake_set("tank/shtetl", SH_SPACE_PROP, "lake");
	zfake_fs("fast/lake");
	ask("fast/lake", SH_SP_LAKE);
	refused("two lake roots: tank/shtetl and fast/lake");
	world();
	zfake_fs("tank/run");
	ask("fast/run", SH_SP_RUN);
	ask("tank/run", SH_SP_RUN);
	refused("two run roots: fast/run and tank/run");
	/* B5 */
	world();
	zfake_fs("tank/shtetl/run");
	ask("tank/shtetl", SH_SP_LAKE);
	ask("tank/shtetl/run", SH_SP_RUN);
	refused("tank/shtetl/run: inside tank/shtetl");
	world();
	zfake_set("fast/run", SH_SPACE_PROP, "run");
	zfake_fs("fast/run/lake");
	ask("fast/run/lake", SH_SP_LAKE);
	refused("fast/run/lake: overlaps run root fast/run");
	world();
	zfake_set("fast/run", SH_SPACE_PROP, "run");
	ask("fast", SH_SP_LAKE);
	refused("fast: overlaps run root fast/run");
	/* B6 */
	world();
	zfake_fs("tank/shtetl/home");
	ask("tank/shtetl", SH_SP_LAKE);
	refused("tank/shtetl: unexpected child tank/shtetl/home");
	world();
	zfake_fs("tank/shtetl/archive");
	zfake_fs("tank/shtetl/archive/old");
	ask("tank/shtetl", SH_SP_LAKE);
	refused("tank/shtetl/archive: unexpected child "
	    "tank/shtetl/archive/old");
	world();
	zfake_fs("fast/run/archive");
	ask("fast/run", SH_SP_RUN);
	refused("fast/run: unexpected child fast/run/archive");
	/* B7 */
	world();
	zfake_set("tank/zconfig", "mountpoint", "none");
	ask("tank/zconfig", SH_SP_CONFIG);
	refused("tank/zconfig: no mountpoint");
	world();
	zfake_set("tank/zbuild", "mountpoint", "legacy");
	ask("tank/zbuild", SH_SP_BUILD);
	refused("tank/zbuild: no mountpoint");
}

/* B4, B8, B10, B11, B17: the whole of a bootstrap. */
static void
test_first(void)
{
	world();
	zfake_set("tank", "compression", "zstd");
	ask("tank/shtetl", SH_SP_LAKE);
	ask("fast/run", SH_SP_RUN);
	ask("tank/zconfig", SH_SP_CONFIG);
	ask("tank/zbuild", SH_SP_BUILD);
	CHECK(plan(NULL) == 0);
	/* B8: the table */
	CHECK(has(out, "tank/shtetl: a lake root\n"
	    "    property      default      shtetl.conf  used         now\n"
	    "  * atime         off          -            off          on "
	    "(default)\n"
	    "    compression   zstd         -            zstd         zstd "
	    "(inherited)\n"
	    "  * aclmode       passthrough  -            passthrough  discard "
	    "(default)\n"
	    "  * mountpoint    none         shtetl's     none         "
	    "/tank/shtetl (default)\n"
	    "  * canmount      noauto       shtetl's     noauto       on "
	    "(default)\n"));
	CHECK(has(out, "  + tank/shtetl/foundations\n"));
	CHECK(has(out, "  + tank/shtetl/archive/systems\n"));
	CHECK(has(out, "fast/run: a run root\n"));
	CHECK(has(out, "  + fast/run/foundations  mountpoint=/shtetl/"
	    "foundations, canmount=off\n"));
	CHECK(has(out, "  + fast/run/containers  mountpoint=/containers, "
	    "canmount=on, mounted\n"));
	CHECK(has(out, "  + fast/run/systems\n"));
	CHECK(has(out, "tank/zconfig: the config root\n"));
	CHECK(has(out, "    mountpoint    -            yours"));
	CHECK(has(out, "tank/zbuild: the build root\n"));
	CHECK(has(out, "pool tank: ashift 12; autotrim off, set to on\n"));
	CHECK(has(out, "then shtetl:space on tank/shtetl (lake), fast/run "
	    "(run), tank/zconfig (config), tank/zbuild (build)\n"));

	CHECK(apply() == 0);
	CHECK(c.c_err.se_count == 0);
	/* B10 */
	CHECK(local("tank/shtetl", "mountpoint", "none"));
	CHECK(local("tank/shtetl", "canmount", "noauto"));
	CHECK(local("tank/shtetl", "atime", "off"));
	CHECK(local("tank/shtetl", "aclmode", "passthrough"));
	CHECK(zfake_exists("tank/shtetl/foundation-builds"));
	CHECK(zfake_exists("tank/shtetl/archive/containers"));
	CHECK(is(prop("tank/shtetl/foundations", "mountpoint", NULL),
	    "none"));
	CHECK(local("tank/shtetl", SH_SPACE_PROP, "lake"));
	/* B11 */
	CHECK(local("fast/run/foundations", "mountpoint",
	    "/shtetl/foundations"));
	CHECK(local("fast/run/foundations", "canmount", "off"));
	CHECK(local("fast/run/containers", "mountpoint", "/containers"));
	CHECK(local("fast/run/containers", "canmount", "on"));
	CHECK(is(prop("fast/run/containers", "mounted", NULL), "yes"));
	CHECK(is(prop("fast/run/systems", "mountpoint", NULL), "none"));
	CHECK(local("tank/zconfig", SH_SPACE_PROP, "config"));
	CHECK(local("tank/zbuild", SH_SPACE_PROP, "build"));
	/* config and build keep their own mountpoints */
	CHECK(local("tank/zbuild", "mountpoint", "/usr/src"));
	/* B17 */
	CHECK(has(out, "set autotrim=on on pool tank\n"));
	CHECK(has(out, "set autotrim=on on pool fast\n"));
	CHECK(has(out, "fast/run is a run root\n"));
	/* the marks come last */
	CHECK(strstr(out, "is a lake root") > strstr(out, "mounted "));
	discover();
	CHECK(s.ss_root[SH_SP_LAKE] != NULL && s.ss_root[SH_SP_RUN] != NULL);
	CHECK(s.ss_root[SH_SP_CONFIG] != NULL &&
	    s.ss_root[SH_SP_BUILD] != NULL);

	/* B4: one root to a space, so none more */
	zfake_fs("tank/run2");
	(void) memset(&req, 0, sizeof (req));
	ask("tank/run2", SH_SP_RUN);
	refused("two run roots: fast/run and tank/run2");

	/* B17: a pool's autotrim on already is left alone */
	world();
	CHECK(sh_zpool_set(z, "tank", "autotrim", "on", &c.c_err) == 0);
	ask("tank/shtetl", SH_SP_LAKE);
	CHECK(plan(NULL) == 0);
	CHECK(has(out, "pool tank: ashift 12; autotrim on\n"));
	CHECK(apply() == 0);
	CHECK(strstr(out, "autotrim") == NULL);
}

/* B9: built-in, properties.default, properties.<space>. */
static void
test_settings(void)
{
	struct sh_site site;

	(void) scratch_write("site/shtetl.conf", "properties {\n"
	    "  default { compression = \"zstd-9\"; recordsize = \"1M\"; }\n"
	    "  lake { atime = true; }\n"
	    "}\n");
	CHECK(sh_site_load(scratch_path("site"), &site, &c.c_err) == 0);
	world();
	ask("tank/shtetl", SH_SP_LAKE);
	ask("fast/run", SH_SP_RUN);
	CHECK(plan(site.ss_merged) == 0);
	CHECK(has(out, "tank/shtetl: a lake root\n"
	    "    property      default      shtetl.conf  used         now\n"
	    "    atime         off          on           on           on "
	    "(default)\n"
	    "  * compression   zstd         zstd-9       zstd-9       on "
	    "(default)\n"
	    "  * aclmode       passthrough  -            passthrough  discard "
	    "(default)\n"
	    "  * recordsize    -            1M           1M           128K "
	    "(default)\n"));
	CHECK(has(out, "fast/run: a run root\n"
	    "    property      default      shtetl.conf  used         now\n"
	    "  * atime         off          -            off          on "
	    "(default)\n"));
	sh_site_free(&site);
}

/* Whether the directory PATH holds nothing. */
static int
emptydir(const char *path)
{
	struct dirent *de;
	DIR *d;
	int n;

	if ((d = opendir(path)) == NULL)
		return (0);
	n = 0;
	while ((de = readdir(d)) != NULL) {
		if (strcmp(de->d_name, ".") != 0 &&
		    strcmp(de->d_name, "..") != 0)
			n++;
	}
	(void) closedir(d);
	return (n == 0);
}

/* B13: the recipes, cloned into the config root. */
static void
test_clone(void)
{
	char cmd[4200], repo[1100], file[1200];
	struct stat st;
	int saved, null;

	/* git's own output, which bootstrap passes on, is not the test's */
	saved = dup(STDERR_FILENO);
	null = open("/dev/null", O_WRONLY);
	CHECK(saved != -1 && null != -1 && dup2(null, STDERR_FILENO) != -1);
	(void) close(null);

	(void) snprintf(repo, sizeof (repo), "%s", scratch_path("recipes"));
	(void) snprintf(cmd, sizeof (cmd), "set -e; git init -q '%s'; "
	    "echo 'pkg = [ \"sudo\" ];' > '%s/shtetl.conf'; cd '%s'; "
	    "git add shtetl.conf; git -c user.name=t -c user.email=t@t "
	    "commit -qm recipes", repo, repo, repo);
	CHECK(system(cmd) == 0);

	/* no --recipes: no clone */
	world();
	ask("tank/zconfig", SH_SP_CONFIG);
	CHECK(plan(NULL) == 0);
	CHECK(strstr(out, "git clone") == NULL);
	CHECK(apply() == 0);
	CHECK(zfake_last_snapshot() == NULL);
	(void) snprintf(file, sizeof (file), "%s/shtetl.conf", confdir);
	CHECK(stat(file, &st) == -1);

	/* --recipes and an empty root: a snapshot, then the clone */
	world();
	ask("tank/zconfig", SH_SP_CONFIG);
	req.br_recipes = repo;
	CHECK(plan(NULL) == 0);
	CHECK(has(out, "  + git clone "));
	CHECK(apply() == 0);
	CHECK(zfake_last_snapshot() != NULL);
	CHECK(strncmp(zfake_last_snapshot(), "tank/zconfig@bootstrap-",
	    23) == 0);
	CHECK(stat(file, &st) == 0);
	CHECK(has(out, "cloned "));
	CHECK(local("tank/zconfig", SH_SPACE_PROP, "config"));

	/* not empty: not cloned into, and no snapshot taken */
	world();
	ask("tank/zconfig", SH_SP_CONFIG);
	req.br_recipes = repo;
	CHECK(plan(NULL) == 0);
	CHECK(apply() == 0);
	CHECK(zfake_last_snapshot() == NULL);
	CHECK(has(out, "is not empty: not cloned into\n"));

	/* a clone that fails: the root left empty, every root marked */
	(void) snprintf(cmd, sizeof (cmd), "rm -rf '%s'; mkdir '%s'", confdir,
	    confdir);
	CHECK(system(cmd) == 0);
	world();
	ask("tank/shtetl", SH_SP_LAKE);
	ask("tank/zconfig", SH_SP_CONFIG);
	req.br_recipes = scratch_path("no-such-repo");
	CHECK(plan(NULL) == 0);
	cap_reset(&c);
	CHECK(apply() == 0);
	CHECK(has(out, " failed: tank/zconfig left empty, with no "
	    "recipes\n"));
	CHECK(strncmp(zfake_last_rollback(), "tank/zconfig@bootstrap-",
	    23) == 0);
	CHECK(emptydir(confdir));
	CHECK(is(prop("tank/zconfig", SH_SPACE_PROP, NULL), "config"));
	CHECK(is(prop("tank/shtetl", SH_SPACE_PROP, NULL), "lake"));
	CHECK(dup2(saved, STDERR_FILENO) != -1);
	(void) close(saved);
}

/* What sh_confirm decides, given TEXT on stdin. */
static int
confirm(const char *text, int yes, int tty)
{
	FILE *in, *o;
	char *obuf = NULL;
	size_t olen;
	int r;

	/* fmemopen takes no empty buffer: no answer is /dev/null */
	if (*text == '\0')
		in = fopen("/dev/null", "r");
	else
		in = fmemopen((void *)(uintptr_t)text, strlen(text), "r");
	o = open_memstream(&obuf, &olen);
	CHECK(in != NULL && o != NULL);
	r = sh_confirm(in, o, yes, tty, "Go?");
	(void) fclose(in);
	(void) fclose(o);
	CHECK(tty && !yes ? strstr(obuf, "Go? [y/N] ") != NULL : olen == 0);
	free(obuf);
	return (r);
}

/* B16: going on only when confirmed. */
static void
test_confirm(void)
{
	CHECK(confirm("", 1, 0) == 1);
	CHECK(confirm("", 1, 1) == 1);
	CHECK(confirm("y\n", 0, 0) == 0);
	CHECK(confirm("y\n", 0, 1) == 1);
	CHECK(confirm("yes\n", 0, 1) == 1);
	CHECK(confirm("Y\n", 0, 1) == 0);
	CHECK(confirm("n\n", 0, 1) == 0);
	CHECK(confirm("", 0, 1) == 0);
	CHECK(confirm("yesterday\n", 0, 1) == 0);
}

/* B14, B15: interrupted, and run again; layout not as shtetl makes it. */
static void
test_again(void)
{
	world();
	ask("tank/shtetl", SH_SP_LAKE);
	ask("fast/run", SH_SP_RUN);
	CHECK(plan(NULL) == 0);
	zfake_fail_nth("create", 5);
	cap_reset(&c);
	CHECK(apply() == -1);
	CHECK(cap_has(&c, "zfake: create failed as asked"));
	CHECK(is(prop("tank/shtetl", SH_SPACE_PROP, NULL), "-"));
	CHECK(zfake_exists("tank/shtetl/foundations"));
	CHECK(!zfake_exists("tank/shtetl/archive"));
	/* the same command again */
	CHECK(plan(NULL) == 0);
	CHECK(has(out, "    tank/shtetl/foundations (there)\n"));
	CHECK(has(out, "  + tank/shtetl/archive\n"));
	CHECK(apply() == 0);
	CHECK(local("tank/shtetl", SH_SPACE_PROP, "lake"));
	CHECK(local("fast/run", SH_SPACE_PROP, "run"));
	CHECK(zfake_exists("tank/shtetl/archive/systems"));

	/* B15 */
	world();
	zfake_fs("fast/run/containers");
	zfake_set("fast/run/containers", "mountpoint", "/jails");
	zfake_set("fast/run/containers", "canmount", "on");
	ask("fast/run", SH_SP_RUN);
	refused("fast/run/containers: mountpoint is /jails, must be "
	    "/containers");
	world();
	zfake_fs("fast/run/foundations");
	ask("fast/run", SH_SP_RUN);
	refused("fast/run/foundations: mountpoint is /fast/run/foundations "
	    "(not local), must be /shtetl/foundations");
	CHECK(cap_has(&c, "fast/run/foundations: canmount is on (not local), "
	    "must be off"));

	/*
	 * B18: a pool imported with an altroot shows mountpoints below
	 * it, and the layout shtetl made is read as such
	 */
	world();
	CHECK(sh_zpool_set(z, "fast", "altroot", "/mnt", &c.c_err) == 0);
	zfake_fs("fast/run/containers");
	zfake_set("fast/run/containers", "mountpoint", "/mnt/containers");
	zfake_set("fast/run/containers", "canmount", "on");
	ask("fast/run", SH_SP_RUN);
	CHECK(plan(NULL) == 0);
	CHECK(has(out, "    fast/run/containers  mountpoint=/containers, "
	    "canmount=on, mounted (there)\n"));
	world();
	CHECK(sh_zpool_set(z, "fast", "altroot", "/mnt", &c.c_err) == 0);
	zfake_fs("fast/run/containers");
	zfake_set("fast/run/containers", "mountpoint", "/containers");
	zfake_set("fast/run/containers", "canmount", "on");
	ask("fast/run", SH_SP_RUN);
	refused("fast/run/containers: mountpoint is /containers, must be "
	    "/mnt/containers");
}

int
main(void)
{
	cap_open(&c);
	(void) snprintf(confdir, sizeof (confdir), "%s",
	    scratch_path("zconfig"));
	CHECK(mkdir(confdir, 0755) == 0);
	if ((p = calloc(1, sizeof (*p))) == NULL)
		return (1);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_refused();
	test_first();
	test_settings();
	test_clone();
	test_again();
	test_confirm();
	sh_spaces_free(&s);
	sh_zfs_close(z);
	free(p);
	free(out);
	cap_close(&c);
	scratch_clean();
	printf("check_boot: %d checks\n", checks);
	return (0);
}
