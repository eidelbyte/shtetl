/*
 * shtetl beam F (planning.md 7.3): tests/MATRIX.md family M, over the
 * fake ZFS, a lake root in one pool and a run root in another.
 */

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "core/beam.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

#define	LF	"tank/shtetl/foundations/generic"
#define	RF	"fast/run/foundations/generic"

static struct cap c, wc;
static struct sh_zfs *z;
static struct sh_spaces s;
static struct sh_beam p;
static char *ran;

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

/* PROP of NAME, or "(refused)"; where it comes from into *SRC. */
static const char *
prop(const char *name, const char *pr, enum sh_zsrc *src)
{
	static char val[4][512];
	static int n;

	n = (n + 1) % 4;
	if (sh_zfs_get(z, name, pr, val[n], sizeof (val[n]), src,
	    &wc.c_err) != 0)
		return ("(refused)");
	return (val[n]);
}

/* NAME snapshotted with shtetl:kind KIND (NULL: none). */
static void
snap(const char *name, const char *kind)
{
	struct sh_zprop zp[2];
	size_t n;

	n = 0;
	if (kind != NULL) {
		zp[n].zp_name = "shtetl:kind";
		zp[n++].zp_value = kind;
		zp[n].zp_name = "shtetl:commit";
		zp[n++].zp_value = "built";
	}
	cap_reset(&wc);
	CHECK(sh_zfs_snapshot(z, &name, 1, zp, n, &wc.c_err) == 0);
}

/* Each dataset of the foundation generic, in zlake, snapshotted. */
static void
version(const char *v, const char *kind)
{
	static const char *const ds[] = { LF, LF "/tmp", LF "/usr",
	    LF "/usr/local", NULL };
	char name[256];
	size_t i;

	for (i = 0; ds[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", ds[i], v);
		snap(name, kind);
	}
}

/*
 * A lake root in tank, a run root in fast, as bootstrap lays them out;
 * the foundation generic with a placeholder, its versions v1, v2 and
 * v2-pkg, and a snapshot no version between them; with RUN 0, no run
 * root.
 */
static void
world(int run)
{
	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/shtetl");
	zfake_set("tank/shtetl", "shtetl:space", "lake");
	zfake_set("tank/shtetl", "mountpoint", "none");
	zfake_fs("tank/shtetl/foundations");
	zfake_fs("tank/shtetl/foundation-builds");
	zfake_fs(LF);
	zfake_fs(LF "/tmp");
	zfake_fs(LF "/usr");
	zfake_set(LF "/usr", "canmount", "off");
	zfake_fs(LF "/usr/local");
	zfake_set(LF, "shtetl:kind", "foundation");
	zfake_pool("fast");
	if (run) {
		zfake_fs("fast/run");
		zfake_set("fast/run", "shtetl:space", "run");
		zfake_set("fast/run", "mountpoint", "none");
		zfake_fs("fast/run/foundations");
		zfake_set("fast/run/foundations", "mountpoint",
		    "/shtetl/foundations");
		zfake_set("fast/run/foundations", "canmount", "off");
		zfake_fs("fast/run/containers");
		zfake_set("fast/run/containers", "mountpoint", "/containers");
	}
	version("v1", "version");
	version("scratch", NULL);
	version("v2", "version");
	version("v2-pkg", "pkg");
	sh_spaces_free(&s);
	cap_reset(&c);
	CHECK(sh_spaces_discover(z, &s, &c.c_err) == 0);
}

static int
plan(const char *target, const char *v)
{
	cap_reset(&c);
	return (sh_beam_plan(z, &s, target, v, &p, &c.c_err));
}

static void
refused(const char *target, const char *v, const char *want)
{
	int rc;

	rc = plan(target, v);
	if (rc == 0 || !cap_has(&c, want))
		printf("%s: wanted \"%s\", got:\n%s", target, want,
		    cap_text(&c));
	CHECK(rc == -1 && cap_has(&c, want));
}

/* The plan for generic at V, run: its return; what it said in ran. */
static int
beam(const char *v)
{
	size_t len;
	FILE *f;
	int rc;

	if (plan("generic", v) != 0) {
		printf("plan refused:\n%s", cap_text(&c));
		CHECK(0);
	}
	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	rc = sh_beam_run(z, &p, f, &c.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

/* M1, M2: the plan, and what is refused. */
static void
test_plan(void)
{
	char *out;
	size_t len;
	FILE *f;

	world(0);
	refused("generic", NULL, "no run root");
	world(1);
	refused("Gen.eric", NULL, "Gen.eric: invalid foundation name");
	refused("containers/x", NULL, "containers/x: invalid foundation");
	refused("nope", NULL, "no foundation nope");
	zfake_fs("tank/shtetl/foundations/bare");
	snap("tank/shtetl/foundations/bare@scratch", NULL);
	refused("bare", NULL, "foundation bare exists without a version");
	refused("generic", "v9", "foundation generic has no version v9");
	refused("generic", "scratch",
	    "foundation generic has no version scratch");
	/* M1: the newest by default, a -pkg here; foundations/F alike */
	CHECK(plan("generic", NULL) == 0 && is(p.bm_version, "v2-pkg"));
	CHECK(plan("foundations/generic", "v1") == 0 &&
	    is(p.bm_version, "v1") && is(p.bm_from, ""));
	CHECK(is(p.bm_lake, LF) && is(p.bm_run, RF) && p.bm_n == 4);
	CHECK(is(p.bm_rel[2], "usr") && p.bm_placeholder[2] == 1);
	CHECK(is(p.bm_rel[3], "usr/local") && p.bm_placeholder[3] == 0);
	out = NULL;
	f = open_memstream(&out, &len);
	CHECK(f != NULL);
	sh_beam_show(f, &p);
	CHECK(fclose(f) == 0);
	CHECK(is(out, "foundation generic, @v1 to zrun, whole\n"
	    "  " LF " -> " RF "\n"
	    "  " LF "/tmp -> " RF "/tmp\n"
	    "  " LF "/usr -> " RF "/usr  (placeholder)\n"
	    "  " LF "/usr/local -> " RF "/usr/local\n"
	    "  readonly=on, canmount=noauto; a placeholder's canmount=off\n"));
	free(out);
}

/* M3 - M5: a first beam, a later one, the copy of shtetl:*. */
static void
test_beams(void)
{
	enum sh_zsrc src;

	world(1);
	/* M3 */
	CHECK(beam("v1") == 0);
	CHECK(strstr(ran, "received " RF "@v1, whole\n"
	    "received " RF "/tmp@v1, whole\n") != NULL);
	CHECK(strstr(ran, "beamed generic@v1 to " RF "\n") != NULL);
	CHECK(is(prop(RF, "readonly", &src), "on") && src == SH_ZSRC_LOCAL);
	CHECK(is(prop(RF "/usr/local", "readonly", &src), "on") &&
	    src == SH_ZSRC_INHERITED);
	CHECK(is(prop(RF, "canmount", &src), "noauto") &&
	    src == SH_ZSRC_LOCAL);
	CHECK(is(prop(RF "/usr/local", "canmount", NULL), "noauto"));
	CHECK(is(prop(RF "/usr", "canmount", NULL), "off"));
	CHECK(is(prop(RF "/usr/local", "mountpoint", &src),
	    "/shtetl/foundations/generic/usr/local") &&
	    src == SH_ZSRC_INHERITED);
	CHECK(is(prop(RF, "mounted", NULL), "no"));
	CHECK(is(prop(RF "/tmp", "mounted", NULL), "no"));
	/* M5: the version's shtetl:*, nothing of the filesystem's */
	CHECK(is(prop(RF "/usr/local@v1", "shtetl:kind", &src), "version") &&
	    src == SH_ZSRC_LOCAL);
	CHECK(is(prop(RF "@v1", "shtetl:commit", NULL), "built"));
	CHECK(is(prop(RF, "shtetl:kind", NULL), "-"));
	CHECK(!zfake_snapshot(RF "@scratch"));
	/* M2: forward only */
	refused("generic", "v1", "generic@v1 is on zrun already");
	/* M4: a later beam, from zrun's newest */
	CHECK(plan("generic", "v2") == 0 && is(p.bm_from, "v1"));
	CHECK(beam("v2") == 0);
	CHECK(strstr(ran, "received " RF "/usr/local@v2, from its last\n") !=
	    NULL);
	CHECK(zfake_snapshot(RF "@v1") && zfake_snapshot(RF "/usr@v2"));
	CHECK(is(prop(RF "@v2", "shtetl:kind", NULL), "version"));
	/* M5: a copy cut short, made again before the next */
	CHECK(sh_zfs_inherit(z, RF "/tmp@v2", "shtetl:kind", &wc.c_err) == 0);
	CHECK(is(prop(RF "/tmp@v2", "shtetl:kind", NULL), "-"));
	CHECK(beam(NULL) == 0);
	CHECK(strstr(ran, "copied shtetl:* onto " RF "/tmp@v2 again\n") !=
	    NULL);
	CHECK(is(prop(RF "/tmp@v2", "shtetl:kind", NULL), "version"));
	CHECK(is(prop(RF "@v2-pkg", "shtetl:kind", NULL), "pkg"));
	/* M2: an older one than zrun's newest, v2 passed over */
	world(1);
	CHECK(beam("v1") == 0 && beam("v2-pkg") == 0);
	refused("generic", "v2", "zrun has generic@v2-pkg, newer than v2");
}

/* M2, M6: datasets differing; a send failing part way. */
static void
test_failures(void)
{
	world(1);
	/* M6: a first beam's whole tree gone */
	zfake_fail_nth("send", 3);
	CHECK(beam("v1") == -1);
	CHECK(cap_has(&c, "beam of generic@v1 failed"));
	CHECK(strstr(ran, "destroyed " RF "\n") != NULL);
	CHECK(!zfake_exists(RF) && !zfake_exists(RF "/tmp"));
	/* a later one's new snapshots gone, the old kept */
	CHECK(beam("v1") == 0);
	zfake_fail_nth("send", 2);
	CHECK(beam("v2") == -1);
	CHECK(strstr(ran, "destroyed " RF "@v2\n") != NULL);
	CHECK(!zfake_snapshot(RF "@v2") && zfake_snapshot(RF "@v1"));
	CHECK(!zfake_snapshot(RF "/tmp@v2"));
	CHECK(beam("v2") == 0);
	/* M2: a dataset zrun has that zlake has not */
	zfake_fs(RF "/extra");
	refused("generic", "v2-pkg", "foundation generic: its datasets on "
	    "zrun differ");
}

int
main(void)
{
	cap_open(&c);
	cap_open(&wc);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_plan();
	test_beams();
	test_failures();
	free(ran);
	sh_spaces_free(&s);
	sh_zfs_close(z);
	cap_close(&wc);
	cap_close(&c);
	printf("check_beam: %d checks\n", checks);
	return (0);
}
