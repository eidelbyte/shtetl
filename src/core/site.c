/*
 * shtetl.conf: its schema, the built-in settings, and the merge.
 */

#include <string.h>

#include "core/err.h"
#include "core/parse.h"
#include "core/root.h"
#include "core/schema.h"
#include "core/site.h"
#include "core/syntax.h"
#include "core/tree.h"

const char sh_site_builtin[] = "(built-in)";
const char sh_site_conf[] = "shtetl.conf";

/*
 * The built-in settings: 4.3's dataset properties (mountpoint and
 * canmount are shtetl's own and no setting's), 6.3's policies at 0,
 * everything, and the staging suffix of 5.2.
 */
static const char site_builtin_text[] =
	"properties {\n"
	"    default {\n"
	"        atime = \"off\";\n"
	"        compression = \"zstd\";\n"
	"        aclmode = \"passthrough\";\n"
	"    }\n"
	"}\n"
	"build {\n"
	"    make_jobs = 0;\n"
	"}\n"
	"update {\n"
	"    suffix = \"staging\";\n"
	"    depth = 0;\n"
	"}\n"
	"retention {\n"
	"    run = 0;\n"
	"    archive = 0;\n"
	"}\n";

enum sv {
	SV_SECTION,	/* an object of the keys in sk_sub */
	SV_PROPS,	/* ZFS properties */
	SV_COUNT,	/* an integer, 0 or more */
	SV_POLICY,	/* 6.3: 0, a count, or a quoted interval */
	SV_NAME		/* a name, as a recipe's */
};

struct skey {
	const char		*sk_name;
	enum sv			sk_type;
	const struct skey	*sk_sub;
};

#define	SK_END	{ NULL, SV_SECTION, NULL }

static const struct skey sk_properties[] = {
	{ "default", SV_PROPS, NULL },
	{ "lake", SV_PROPS, NULL },
	{ "run", SV_PROPS, NULL },
	{ "config", SV_PROPS, NULL },
	{ "build", SV_PROPS, NULL },
	SK_END
};
static const struct skey sk_build[] = {
	{ "make_jobs", SV_COUNT, NULL },
	SK_END
};
static const struct skey sk_update[] = {
	{ "suffix", SV_NAME, NULL },
	{ "depth", SV_POLICY, NULL },
	SK_END
};
static const struct skey sk_retention[] = {
	{ "run", SV_POLICY, NULL },
	{ "archive", SV_POLICY, NULL },
	SK_END
};
static const struct skey sk_top[] = {
	{ "properties", SV_SECTION, sk_properties },
	{ "build", SV_SECTION, sk_build },
	{ "update", SV_SECTION, sk_update },
	{ "retention", SV_SECTION, sk_retention },
	SK_END
};

int
sh_site_section(const char *key)
{
	const struct skey *k;

	for (k = sk_top; k->sk_name != NULL; k++) {
		if (strcmp(k->sk_name, key) == 0)
			return (1);
	}
	return (0);
}

struct ck {
	const char	*ck_file;
	struct sh_err	*ck_err;
	struct sh_path	ck_path;
};

static void
ck_type(struct ck *x, const char *what)
{
	sh_err_at(x->ck_err, x->ck_file, &x->ck_path, "must be %s", what);
}

static void
ck_value(struct ck *x, const struct skey *k, const struct sh_node *v);

/* The keys of one section, against TABLE. */
static void
ck_section(struct ck *x, const struct skey *table, const struct sh_node *v)
{
	const struct skey *k;
	const struct sh_pair *pr;
	size_t i, was;

	if (v->sn_type != SH_OBJECT) {
		ck_type(x, "an object");
		return;
	}
	for (i = 0; i < v->sn_obj.ob_n; i++) {
		pr = &v->sn_obj.ob_v[i];
		/* the default recipe's keys: sh_schema_default's to check */
		if (table == sk_top && !sh_site_section(pr->pr_key))
			continue;
		was = sh_path_push(&x->ck_path, pr->pr_key);
		for (k = table; k->sk_name != NULL; k++) {
			if (strcmp(k->sk_name, pr->pr_key) == 0)
				break;
		}
		if (k->sk_name != NULL)
			ck_value(x, k, pr->pr_val);
		else
			sh_err_at(x->ck_err, x->ck_file, &x->ck_path,
			    "invalid key");
		sh_path_pop(&x->ck_path, was);
	}
}

static void
ck_value(struct ck *x, const struct skey *k, const struct sh_node *v)
{
	const char *why;

	switch (k->sk_type) {
	case SV_SECTION:
		ck_section(x, k->sk_sub, v);
		return;
	case SV_PROPS:
		(void) sh_schema_props(v, x->ck_file, &x->ck_path, x->ck_err);
		return;
	case SV_COUNT:
	case SV_POLICY:
		if (v->sn_type == SH_INT && v->sn_int < 0) {
			ck_type(x, "0 or more");
			return;
		}
		if (v->sn_type == SH_INT)
			return;
		if (k->sk_type == SV_COUNT) {
			ck_type(x, "an integer");
			return;
		}
		if (v->sn_type == SH_STR) {
			if ((why = sh_syn_interval(v->sn_str)) != NULL)
				sh_err_at(x->ck_err, x->ck_file, &x->ck_path,
				    "invalid interval: %s", why);
			return;
		}
		/* UCL reads an unquoted 30d as a number of seconds */
		ck_type(x, "an integer or a quoted interval");
		return;
	case SV_NAME:
		if (v->sn_type != SH_STR) {
			ck_type(x, "a string");
			return;
		}
		if ((why = sh_syn_name(v->sn_str)) != NULL)
			sh_err_at(x->ck_err, x->ck_file, &x->ck_path,
			    "invalid name: %s", why);
		return;
	}
}

/*
 * shtetl.conf whole: its sections of settings here, and the keys of
 * the default recipe it holds beside them by the recipe's schema.
 */
unsigned
sh_site_check(const struct sh_node *site, const char *file,
    struct sh_err *e)
{
	struct ck x;
	unsigned before;

	before = e->se_count;
	x.ck_file = file;
	x.ck_err = e;
	sh_path_init(&x.ck_path);
	ck_section(&x, sk_top, site);
	if (site->sn_type == SH_OBJECT)
		(void) sh_schema_default(site, file, 1, e);
	return (e->se_count - before);
}

/*
 * LAYER over DST, key by key: objects on both sides merge, anything
 * else is replaced. A key keeps its place and the layer that added
 * it; the values copied keep the layer they came from.
 */
static int
site_merge(struct sh_node *dst, const struct sh_node *layer, int top)
{
	const struct sh_pair *pr;
	struct sh_node *have, *v;
	size_t i;

	for (i = 0; i < layer->sn_obj.ob_n; i++) {
		pr = &layer->sn_obj.ob_v[i];
		/* the settings only: the default recipe is the cascade's */
		if (top && !sh_site_section(pr->pr_key))
			continue;
		have = sh_obj_get(dst, pr->pr_key);
		if (have != NULL && have->sn_type == SH_OBJECT &&
		    pr->pr_val->sn_type == SH_OBJECT) {
			if (site_merge(have, pr->pr_val, 0) != 0)
				return (-1);
			continue;
		}
		if ((v = sh_node_copy(pr->pr_val)) == NULL ||
		    sh_obj_set(dst, pr->pr_key, v, pr->pr_from) != 0)
			return (-1);
	}
	return (0);
}

struct sh_node *
sh_site_builtins(struct sh_err *e)
{
	struct sh_node *built;

	built = sh_parse_buf(site_builtin_text, sizeof (site_builtin_text) - 1,
	    sh_site_builtin, e);
	if (built != NULL && sh_site_check(built, sh_site_builtin, e) != 0) {
		sh_node_free(built);
		return (NULL);
	}
	return (built);
}

int
sh_site_load(const char *root, struct sh_site *s, struct sh_err *e)
{
	struct sh_node *built, *conf;
	int missing;

	s->ss_merged = NULL;
	s->ss_has_conf = 0;
	if ((built = sh_site_builtins(e)) == NULL)
		return (-1);
	conf = sh_root_parse(root, sh_site_conf, &missing, e);
	if (conf == NULL && !missing) {
		sh_node_free(built);
		return (-1);
	}
	if (conf != NULL && sh_site_check(conf, sh_site_conf, e) != 0) {
		sh_node_free(conf);
		sh_node_free(built);
		return (-1);
	}
	if (conf != NULL && site_merge(built, conf, 1) != 0) {
		sh_err_nomem(e);
		sh_node_free(conf);
		sh_node_free(built);
		return (-1);
	}
	s->ss_has_conf = conf != NULL;
	sh_node_free(conf);
	s->ss_merged = built;
	return (0);
}

void
sh_site_free(struct sh_site *s)
{
	sh_node_free(s->ss_merged);
	s->ss_merged = NULL;
}
