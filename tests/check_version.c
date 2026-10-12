/*
 * A foundation version's name and its ver check (planning.md 6.1,
 * 7.1): tests/MATRIX.md family V, every cell (V1 - V8).
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "core/parse.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/version.h"

static struct cap c;

#define	SHA	"0f691888dc56a1b2c3d4e5f60718293a4b5c6d7e"

/* 2026-10-08 12:00:00 UTC */
#define	NOON	((time_t)1791460800)

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

/* S read as a branch: NULL and B, or why not. */
static const char *
br(const char *s, struct sh_branch *b)
{
	return (sh_syn_branch(s, b));
}

/* V1: the spellings. */
static void
test_branches(void)
{
	static const char *const poor[] = { "releng/15", "stable/15.1",
	    "stable/", "releng/a.b", "Main", "main/x", "releng/15.1.2",
	    "stable/15x", "", "releng/.1", "stable/1000" };
	struct sh_branch b;
	size_t i;

	CHECK(br("main", &b) == NULL && b.br_type == SH_BR_MAIN);
	CHECK(b.br_major == -1 && b.br_minor == -1);
	CHECK(br("stable/15", &b) == NULL && b.br_type == SH_BR_STABLE);
	CHECK(b.br_major == 15 && b.br_minor == -1);
	CHECK(br("releng/15.1", &b) == NULL && b.br_type == SH_BR_RELENG);
	CHECK(b.br_major == 15 && b.br_minor == 1);
	CHECK(br("releng/15.10", &b) == NULL && b.br_minor == 10);
	for (i = 0; i < sizeof (poor) / sizeof (poor[0]); i++) {
		if (br(poor[i], &b) == NULL)
			printf("took \"%s\"\n", poor[i]);
		CHECK(br(poor[i], &b) != NULL);
	}
	CHECK(is(br("stable/", &b), "not main, stable/N or releng/N.M"));
}

/* The name of a version from S, MAJOR, WHEN, among HAVE. */
static const char *
name(const char *s, long major, time_t when, const char *const *have,
    size_t nhave)
{
	static char out[SH_VERSION_MAX];
	struct sh_branch b;

	CHECK(br(s, &b) == NULL);
	cap_reset(&c);
	CHECK(sh_version_name(&b, major, when, SHA, have, nhave, out,
	    sizeof (out), &c.c_err) == 0);
	return (out);
}

/* V2 - V5: the names. */
static void
test_names(void)
{
	static const char *const have[] = {
		"15.1-releng-2026-10-08-0f691888dc56",
		"15.1-releng-2026-10-08-0f691888dc56-pkg",
		"15.1-releng-2026-10-07-0f691888dc56",
		"shtetl-patch-0"
	};
	static const char *const two[] = {
		"15.1-releng-2026-10-08-0f691888dc56",
		"15.1-releng-2026-10-08-0f691888dc56.2"
	};
	struct sh_branch b;
	char out[SH_VERSION_MAX];

	/* V2 */
	CHECK(is(name("releng/15.1", 15, NOON, NULL, 0),
	    "15.1-releng-2026-10-08-0f691888dc56"));
	CHECK(is(name("stable/15", 15, NOON, NULL, 0),
	    "15-stable-2026-10-08-0f691888dc56"));
	CHECK(is(name("main", 16, NOON, NULL, 0),
	    "16-current-2026-10-08-0f691888dc56"));
	/* V3: the UTC day, whatever the zone */
	(void) setenv("TZ", "Pacific/Kiritimati", 1);	/* UTC+14 */
	tzset();
	CHECK(is(name("main", 16, NOON + 11 * 3600, NULL, 0),
	    "16-current-2026-10-08-0f691888dc56"));
	(void) setenv("TZ", "Pacific/Pago_Pago", 1);	/* UTC-11 */
	tzset();
	CHECK(is(name("main", 16, NOON - 12 * 3600 - 1, NULL, 0),
	    "16-current-2026-10-07-0f691888dc56"));
	(void) unsetenv("TZ");
	tzset();
	/* V5: .2, .3; the -pkg twin, another day, other names, no matter */
	CHECK(is(name("releng/15.1", 15, NOON, have, 4),
	    "15.1-releng-2026-10-08-0f691888dc56.2"));
	CHECK(is(name("releng/15.1", 15, NOON, two, 2),
	    "15.1-releng-2026-10-08-0f691888dc56.3"));
	CHECK(is(name("releng/15.1", 15, NOON + 86400, have, 4),
	    "15.1-releng-2026-10-09-0f691888dc56"));
	/* V4: a commit of 12 hex digits or more */
	CHECK(br("main", &b) == NULL);
	cap_reset(&c);
	CHECK(sh_version_name(&b, 16, NOON, "0f691888dc56", NULL, 0, out,
	    sizeof (out), &c.c_err) == 0);
	CHECK(is(out, "16-current-2026-10-08-0f691888dc56"));
	CHECK(sh_version_name(&b, 16, NOON, "0f691888dc5", NULL, 0, out,
	    sizeof (out), &c.c_err) == -1);
	CHECK(cap_has(&c, "invalid commit: 0f691888dc5\n"));
	cap_reset(&c);
	CHECK(sh_version_name(&b, 16, NOON, "0F691888DC56", NULL, 0, out,
	    sizeof (out), &c.c_err) == -1);
	CHECK(sh_version_name(&b, 16, NOON, "0f691888dc5g", NULL, 0, out,
	    sizeof (out), &c.c_err) == -1);
}

/* V6: REVISION as newvers.sh -V prints it. */
static void
test_revision(void)
{
	static const char *const poor[] = { "REVISION=\"16\"\n",
	    "REVISION=16.0\n", "REVISION=\"16.0\"\nBRANCH=\"CURRENT\"\n",
	    "", "revision=\"16.0\"\n", "REVISION=\"16.0-CURRENT\"\n",
	    " REVISION=\"16.0\"\n" };
	long major, minor;
	size_t i;

	cap_reset(&c);
	CHECK(sh_version_revision("REVISION=\"16.0\"\n", &major, &minor,
	    &c.c_err) == 0);
	CHECK(major == 16 && minor == 0);
	CHECK(sh_version_revision("REVISION=\"15.10\"", &major, &minor,
	    &c.c_err) == 0);
	CHECK(major == 15 && minor == 10);
	for (i = 0; i < sizeof (poor) / sizeof (poor[0]); i++) {
		cap_reset(&c);
		CHECK(sh_version_revision(poor[i], &major, &minor,
		    &c.c_err) == -1);
		CHECK(cap_has(&c, "newvers.sh: no REVISION=\"N.M\" line"));
	}
}

/* A merged ver: the constraints C, from FROM, as the cascade keeps. */
static struct sh_node *
ver(const char *const *cs, const char *from)
{
	struct sh_node *v, *s;

	v = sh_node_new(SH_ARRAY, from);
	CHECK(v != NULL);
	for (; *cs != NULL; cs++) {
		s = sh_node_str(*cs, strlen(*cs), from);
		CHECK(s != NULL && sh_arr_push(v, s) == 0);
	}
	return (v);
}

/* V7, V8: the checks. */
static void
test_checks(void)
{
	static const char *const ge151[] = { ">=15.1", "<16", NULL };
	static const char *const ge16[] = { ">=16", NULL };
	static const char *const eq15[] = { "=15", NULL };
	struct sh_node *v;
	struct sh_branch b;

	v = ver(ge151, "shtetl.conf");
	/* V7: before the checkout */
	CHECK(br("releng/15.1", &b) == NULL);
	cap_reset(&c);
	CHECK(sh_version_check_branch(&b, v, "foundations/f.conf",
	    &c.c_err) == 0);
	CHECK(br("releng/15.0", &b) == NULL);
	CHECK(sh_version_check_branch(&b, v, "foundations/f.conf",
	    &c.c_err) == -1);
	CHECK(cap_has(&c, "foundations/f.conf: ver: 15.0 does not satisfy "
	    ">=15.1 in shtetl.conf\n"));
	CHECK(c.c_err.se_count == 1);
	CHECK(br("stable/15", &b) == NULL);
	cap_reset(&c);
	CHECK(sh_version_check_branch(&b, v, "foundations/f.conf",
	    &c.c_err) == 0);
	CHECK(br("stable/16", &b) == NULL);
	CHECK(sh_version_check_branch(&b, v, "foundations/f.conf",
	    &c.c_err) == -1);
	CHECK(cap_has(&c, "16.x does not satisfy <16 in shtetl.conf\n"));
	CHECK(br("main", &b) == NULL);
	cap_reset(&c);
	CHECK(sh_version_check_branch(&b, v, "foundations/f.conf",
	    &c.c_err) == 0);
	CHECK(sh_version_check_branch(&b, NULL, "foundations/f.conf",
	    &c.c_err) == 0);
	/* V8: after it */
	CHECK(sh_version_check(15, 2, v, "foundations/f.conf", &c.c_err) ==
	    0);
	CHECK(sh_version_check(16, 0, v, "foundations/f.conf", &c.c_err) ==
	    -1);
	CHECK(cap_has(&c, "foundations/f.conf: ver: 16.0 does not satisfy "
	    "<16 in shtetl.conf\n"));
	sh_node_free(v);
	v = ver(ge16, "common/new.conf");
	cap_reset(&c);
	CHECK(sh_version_check(15, 9, v, "foundations/f.conf", &c.c_err) ==
	    -1);
	CHECK(cap_has(&c, "15.9 does not satisfy >=16 in common/new.conf\n"));
	sh_node_free(v);
	/* a bare major in ver is every minor of it */
	v = ver(eq15, "foundations/f.conf");
	cap_reset(&c);
	CHECK(sh_version_check(15, 3, v, "foundations/f.conf", &c.c_err) ==
	    0);
	CHECK(br("stable/15", &b) == NULL);
	CHECK(sh_version_check_branch(&b, v, "foundations/f.conf",
	    &c.c_err) == 0);
	sh_node_free(v);
}

int
main(void)
{
	cap_open(&c);
	test_branches();
	test_names();
	test_revision();
	test_checks();
	cap_close(&c);
	printf("check_version: %d checks\n", checks);
	return (0);
}
