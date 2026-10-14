/*
 * The config root's snapshots (src/core/cfgsnap.c): tests/MATRIX.md
 * family N, over the fake ZFS.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "core/cfgsnap.h"
#include "core/zfs.h"
#include "zfake.h"

#define	ROOT	"tank/zconfig"
/* 2026-10-08 12:00:00 UTC */
#define	NOON	((time_t)1791460800)

static struct cap c, wc;
static struct sh_zfs *z;
static char *ran, got[512];

static int
is(const char *have, const char *want)
{
	if (strcmp(have, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", have, want);
		return (0);
	}
	return (1);
}

static const char *
prop(const char *name, const char *pr)
{
	static char val[4][512];
	static int n;

	n = (n + 1) % 4;
	if (sh_zfs_get(z, name, pr, val[n], sizeof (val[n]), NULL,
	    &wc.c_err) != 0)
		return ("(refused)");
	return (val[n]);
}

/* ROOT@ draw TRY of NOW's name. */
static const char *
drawn(time_t now, unsigned try)
{
	static char buf[2][512];
	static int n;
	char words[256];

	n = (n + 1) % 2;
	sh_cfgsnap_name(now, try, words, sizeof (words));
	(void) snprintf(buf[n], sizeof (buf[n]), ROOT "@%s", words);
	return (buf[n]);
}

/* The config root, nothing on it. */
static void
world(void)
{
	zfake_reset();
	zfake_pool("tank");
	zfake_fs(ROOT);
}

/* A snapshot of ROOT named NAME, of shtetl:kind KIND (NULL: none). */
static void
snap(const char *name, const char *kind)
{
	struct sh_zprop zp = { "shtetl:kind", NULL };
	char full[512];
	const char *one = full;

	(void) snprintf(full, sizeof (full), ROOT "@%s", name);
	zp.zp_value = kind;
	CHECK(sh_zfs_snapshot(z, &one, 1, &zp, kind != NULL ? 1 : 0,
	    &wc.c_err) == 0);
}

/* sh_cfgsnap at NOW, its message MSG: its return, GOT, RAN. */
static int
take(time_t now, const char *msg)
{
	size_t len;
	FILE *f;
	int rc;

	free(ran);
	ran = NULL;
	f = open_memstream(&ran, &len);
	CHECK(f != NULL);
	cap_reset(&c);
	got[0] = '\0';
	rc = sh_cfgsnap(z, ROOT, msg, now, got, sizeof (got), f, &c.c_err);
	CHECK(fclose(f) == 0);
	return (rc);
}

/* N1 - N4: taken when there is none, or the root has changed. */
static void
test_taken(void)
{
	char want[600];

	/* N1 */
	world();
	CHECK(take(NOON, "build generic") == 0);
	CHECK(is(got, drawn(NOON, 0)));
	CHECK(zfake_snapshot(got));
	CHECK(is(prop(got, "shtetl:kind"), "config"));
	CHECK(is(prop(got, "shtetl:commit"), "build generic"));
	(void) snprintf(want, sizeof (want), "snapshot %s\n", got);
	CHECK(is(ran, want));
	/* N2: nothing written since: the same, nothing said */
	CHECK(take(NOON + 60, "create x on generic@v1") == 0);
	CHECK(is(got, drawn(NOON, 0)) && is(ran, ""));
	CHECK(!zfake_snapshot(drawn(NOON + 60, 0)));
	CHECK(is(prop(got, "shtetl:commit"), "build generic"));
	/* N3: written since: a new one */
	zfake_write(ROOT, 512);
	CHECK(take(NOON + 60, "update x to generic@v2") == 0);
	CHECK(is(got, drawn(NOON + 60, 0)) && zfake_snapshot(got));
	CHECK(is(prop(got, "shtetl:commit"), "update x to generic@v2"));
	/* the newest counts: written since the first, not the second */
	CHECK(take(NOON + 120, "build generic") == 0);
	CHECK(is(got, drawn(NOON + 60, 0)) && is(ran, ""));
	/* N4: another kind's snapshot after it, passed over */
	world();
	snap("one", "config");
	zfake_write(ROOT, 512);
	snap("bootstrap-20261008-120000", NULL);
	snap("mine", "save");
	CHECK(take(NOON, "build generic") == 0);
	CHECK(is(got, drawn(NOON, 0)));
	world();
	snap("one", "config");
	snap("mine", NULL);
	CHECK(take(NOON, "build generic") == 0);
	CHECK(is(got, ROOT "@one") && is(ran, ""));
}

/* N5: the name taken: the next draw; every one taken: refused. */
static void
test_names_taken(void)
{
	char words[256];
	unsigned i;

	world();
	sh_cfgsnap_name(NOON, 0, words, sizeof (words));
	snap(words, "save");
	CHECK(take(NOON, "build generic") == 0);
	CHECK(is(got, drawn(NOON, 1)) && zfake_snapshot(got));
	world();
	for (i = 0; i < 100; i++) {
		sh_cfgsnap_name(NOON, i, words, sizeof (words));
		snap(words, NULL);
	}
	CHECK(take(NOON, "build generic") == -1);
	CHECK(cap_has(&c, ROOT ": no free name for a config snapshot"));
	CHECK(is(ran, ""));
}

/* Whether NAME is three different words of lowercase letters. */
static int
threewords(const char *name)
{
	char w[3][64];
	const char *p, *d;
	int i;

	for (i = 0, p = name; i < 3; i++) {
		d = i < 2 ? strchr(p, '-') : p + strlen(p);
		if (d == NULL || d == p || (size_t)(d - p) >= sizeof (w[i]))
			return (0);
		(void) snprintf(w[i], sizeof (w[i]), "%.*s", (int)(d - p), p);
		for (; p < d; p++) {
			if (!islower((unsigned char)*p))
				return (0);
		}
		if (i < 2)
			p++;
	}
	return (*p == '\0' && strcmp(w[0], w[1]) != 0 &&
	    strcmp(w[0], w[2]) != 0 && strcmp(w[1], w[2]) != 0);
}

/* N6: the names. */
static void
test_names(void)
{
	char a[256], b[256], seen[200][64];
	unsigned i, j, same;

	sh_cfgsnap_name(NOON, 0, a, sizeof (a));
	sh_cfgsnap_name(NOON, 0, b, sizeof (b));
	CHECK(is(a, b));
	same = 0;
	for (i = 0; i < 200; i++) {
		sh_cfgsnap_name(NOON + (time_t)(i / 2), i % 2, seen[i],
		    sizeof (seen[i]));
		if (!threewords(seen[i])) {
			printf("not three words: %s\n", seen[i]);
			CHECK(0);
		}
		for (j = 0; j < i; j++)
			same += strcmp(seen[i], seen[j]) == 0;
	}
	CHECK(same == 0);
	sh_cfgsnap_name(NOON, 1, b, sizeof (b));
	CHECK(strcmp(a, b) != 0);
}

/* N7: ZFS refusing. */
static void
test_refused(void)
{
	world();
	zfake_fail("snapshots");
	CHECK(take(NOON, "build generic") == -1 && got[0] == '\0');
	world();
	snap("one", "config");
	zfake_write(ROOT, 1);
	zfake_fail_nth("get", 2);	/* the kind, then written@ */
	CHECK(take(NOON, "build generic") == -1);
	CHECK(!zfake_snapshot(drawn(NOON, 0)));
	world();
	zfake_fail("snapshot");
	CHECK(take(NOON, "build generic") == -1);
	CHECK(!zfake_snapshot(drawn(NOON, 0)) && is(ran, ""));
}

int
main(void)
{
	cap_open(&c);
	cap_open(&wc);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_taken();
	test_names_taken();
	test_names();
	test_refused();
	free(ran);
	sh_zfs_close(z);
	cap_close(&wc);
	cap_close(&c);
	scratch_clean();
	printf("check_cfgsnap: %d checks\n", checks);
	return (0);
}
