/*
 * The keys of a recipe (planning.md 5.5) and the check of one layer.
 *
 * A layer is checked against the kind of the recipe it is merged
 * into, not against the file it is: default.conf and an imported
 * common/web.conf are each a layer of a foundation, a container or a
 * system, and take that kind's keys (5.3: "keys that belong to
 * another kind are errors"). A layer may set part of an entry (one
 * field of a derivation) for a lower layer's to complete; whether
 * the merged entry is whole is the cascade's check.
 */

#include <string.h>

#include "core/err.h"
#include "core/schema.h"
#include "core/syntax.h"
#include "core/tree.h"

static const struct sh_rkey rkeys[] = {
	{ "import", SH_ALLKINDS, SH_V_IMPORT, SH_C_NONE, NULL },
	{ "reset", SH_ALLKINDS, SH_V_RESET, SH_C_NONE, NULL },
	{ "ver", SH_ALLKINDS, SH_V_VER, SH_C_NONE, NULL },
	{ "src_branch", SH_FOUNDATION, SH_V_STRING, SH_C_NONE, NULL },
	{ "kernconf", SH_FOUNDATION, SH_V_STRING, SH_C_NONE,
	    "kernconf_file" },
	{ "kernconf_file", SH_FOUNDATION, SH_V_FILE, SH_C_NONE, "kernconf" },
	{ "buildopts", SH_FOUNDATION, SH_V_STRING, SH_C_NONE,
	    "buildopts_file" },
	{ "buildopts_file", SH_FOUNDATION, SH_V_FILE, SH_C_NONE,
	    "buildopts" },
	{ "patches", SH_FOUNDATION, SH_V_COLL, SH_C_PATCHES, NULL },
	{ "datasets", SH_ALLKINDS, SH_V_COLL, SH_C_DATASETS, NULL },
	{ "pkg", SH_ALLKINDS, SH_V_COLL, SH_C_PKG, NULL },
	{ "commands", SH_ALLKINDS, SH_V_COLL, SH_C_COMMANDS, NULL },
	{ "derivations", SH_ALLKINDS, SH_V_COLL, SH_C_DERIVATIONS, NULL },
	{ "requires", SH_CONTAINER, SH_V_COLL, SH_C_REQUIRES, NULL },
	{ "jailconf", SH_CONTAINER, SH_V_STRING, SH_C_NONE,
	    "jailconf_file" },
	{ "jailconf_file", SH_CONTAINER, SH_V_FILE, SH_C_NONE, "jailconf" },
	{ NULL, 0, SH_V_STRING, SH_C_NONE, NULL }
};

/* The forms a collection's entry takes besides false, which drops. */
#define	E_TRUE	0x1
#define	E_OBJ	0x2
#define	E_STR	0x4

static const char *cd_location(const char *s);

/* Each collection: its keys, and what an entry may be. */
static const struct cdesc {
	const char	*cd_key;	/* what its keys are */
	const char	*(*cd_syn)(const char *);
	int		cd_list;	/* a list of keys may stand for it */
	unsigned	cd_entry;
	const char	*cd_forms;	/* the entry's forms, for messages */
} cdescs[] = {
	[SH_C_NONE] = { NULL, NULL, 0, 0, NULL },
	[SH_C_PKG] = { "a package's name", sh_syn_pkg, 1, E_TRUE,
	    "true, or false to drop it" },
	[SH_C_DATASETS] = { "a dataset path", sh_syn_dataset, 1,
	    E_TRUE | E_OBJ, "true, its properties, or false to drop it" },
	[SH_C_COMMANDS] = { "a label", sh_syn_label, 0, E_STR,
	    "its commands as a string or heredoc, or false to drop it" },
	[SH_C_DERIVATIONS] = { "a derived file's path", sh_syn_relpath, 0,
	    E_OBJ, "{ source; command; }, or false to drop it" },
	[SH_C_REQUIRES] = { "a container's name", sh_syn_name, 1, E_TRUE,
	    "true, or false to drop it" },
	[SH_C_PATCHES] = { "a patch source's location", cd_location, 1,
	    E_TRUE | E_OBJ, "true, { ref; dir; }, or false to drop it" }
};

/* One check's place: the file, the kind, where in the tree. */
struct sc {
	const char	*sc_file;
	enum sh_kind	sc_kind;
	struct sh_err	*sc_err;
	struct sh_path	sc_path;
};

static const char *
cd_location(const char *s)
{
	enum sh_loc kind;

	return (sh_syn_location(s, &kind));
}

const char *
sh_kind_name(enum sh_kind kind)
{
	switch (kind) {
	case SH_FOUNDATION:
		return ("foundation");
	case SH_CONTAINER:
		return ("container");
	case SH_SYSTEM:
		return ("system");
	}
	return ("recipe");
}

const struct sh_rkey *
sh_rkey(const char *key)
{
	const struct sh_rkey *rk;

	for (rk = rkeys; rk->rk_name != NULL; rk++) {
		if (strcmp(rk->rk_name, key) == 0)
			return (rk);
	}
	return (NULL);
}

int
sh_coll_superset(enum sh_coll coll)
{
	return (coll == SH_C_PKG || coll == SH_C_DATASETS);
}

/*
 * "expected WHAT, not ...": what V is, and, where a quoted string
 * would have done and UCL read a bare word as something else, to
 * quote it.
 */
static void
sc_type(struct sc *x, const char *what, const struct sh_node *v, int quote)
{
	const char *found;

	if (v->sn_type == SH_BOOL)
		found = v->sn_bool ? "true" : "false";
	else
		found = sh_type_name(v->sn_type);
	sh_err_at(x->sc_err, x->sc_file, &x->sc_path, "expected %s, not %s%s",
	    what, found, quote && (v->sn_type == SH_INT ||
	    v->sn_type == SH_NUM || v->sn_type == SH_BOOL) ?
	    ": quote it" : "");
}

/* WHY, from a syntax check, refusing S as WHAT. 1 if S passed. */
static int
sc_syn(struct sc *x, const char *what, const char *why)
{
	if (why == NULL)
		return (1);
	sh_err_at(x->sc_err, x->sc_file, &x->sc_path, "not %s: %s", what, why);
	return (0);
}

/* A non-empty string. 1 if V is one. */
static int
sc_string(struct sc *x, const struct sh_node *v, const char *what)
{
	if (v->sn_type != SH_STR) {
		sc_type(x, what, v, 1);
		return (0);
	}
	if (v->sn_str[0] == '\0') {
		sh_err_at(x->sc_err, x->sc_file, &x->sc_path, "empty");
		return (0);
	}
	return (1);
}

static void
sc_relpath(struct sc *x, const struct sh_node *v, const char *what)
{
	if (sc_string(x, v, what))
		(void) sc_syn(x, what, sh_syn_relpath(v->sn_str));
}

/* import: a recipe's path, or a list of them. */
static void
sc_import(struct sc *x, const struct sh_node *v)
{
	size_t i, was;

	if (v->sn_type == SH_STR) {
		sc_relpath(x, v, "a recipe's path");
		return;
	}
	if (v->sn_type != SH_ARRAY) {
		sc_type(x, "a list of recipes' paths", v, 0);
		return;
	}
	for (i = 0; i < v->sn_arr.ar_n; i++) {
		was = sh_path_index(&x->sc_path, i);
		sc_relpath(x, v->sn_arr.ar_v[i], "a recipe's path");
		sh_path_pop(&x->sc_path, was);
	}
}

/* reset: names of collections this kind of recipe has. */
static void
sc_reset_one(struct sc *x, const struct sh_node *v)
{
	const struct sh_rkey *rk;

	if (!sc_string(x, v, "a collection's name"))
		return;
	rk = sh_rkey(v->sn_str);
	if (rk == NULL || rk->rk_vt != SH_V_COLL ||
	    (rk->rk_kinds & x->sc_kind) == 0) {
		sh_err_at(x->sc_err, x->sc_file, &x->sc_path,
		    "not a collection a %s's recipe has: \"%s\"",
		    sh_kind_name(x->sc_kind), v->sn_str);
	}
}

static void
sc_reset(struct sc *x, const struct sh_node *v)
{
	size_t i, was;

	if (v->sn_type == SH_STR) {
		sc_reset_one(x, v);
		return;
	}
	if (v->sn_type != SH_ARRAY) {
		sc_type(x, "a list of collections' names", v, 0);
		return;
	}
	for (i = 0; i < v->sn_arr.ar_n; i++) {
		was = sh_path_index(&x->sc_path, i);
		sc_reset_one(x, v->sn_arr.ar_v[i]);
		sh_path_pop(&x->sc_path, was);
	}
}

/*
 * ZFS properties: of a dataset entry here, of a space in shtetl.conf.
 * Values are strings, or true and false for on and off; a number is
 * refused, since UCL reads 1M as 1000000 and recordsize means 1 MiB.
 */
static void
sc_props(struct sc *x, const struct sh_node *props)
{
	const struct sh_pair *pr;
	size_t i, was;

	for (i = 0; i < props->sn_obj.ob_n; i++) {
		pr = &props->sn_obj.ob_v[i];
		was = sh_path_push(&x->sc_path, pr->pr_key);
		if (sc_syn(x, "a property the configuration may set",
		    sh_syn_prop(pr->pr_key))) {
			if (pr->pr_val->sn_type == SH_STR) {
				(void) sc_string(x, pr->pr_val,
				    "a property's value");
			} else if (pr->pr_val->sn_type != SH_BOOL) {
				sc_type(x, "a string, or true or false",
				    pr->pr_val, 1);
			}
		}
		sh_path_pop(&x->sc_path, was);
	}
}

unsigned
sh_schema_props(const struct sh_node *props, const char *file,
    const struct sh_path *path, struct sh_err *e)
{
	struct sc x;
	unsigned before;

	before = e->se_count;
	x.sc_file = file;
	x.sc_kind = SH_FOUNDATION;
	x.sc_err = e;
	x.sc_path = *path;
	if (props->sn_type != SH_OBJECT)
		sc_type(&x, "an object of properties", props, 0);
	else
		sc_props(&x, props);
	return (e->se_count - before);
}

/* The fields of a derivation, or of a patch source. */
static void
sc_fields(struct sc *x, enum sh_coll coll, const struct sh_node *entry)
{
	const struct sh_pair *pr;
	const char *p;
	size_t i, was;

	for (i = 0; i < entry->sn_obj.ob_n; i++) {
		pr = &entry->sn_obj.ob_v[i];
		was = sh_path_push(&x->sc_path, pr->pr_key);
		if (coll == SH_C_DERIVATIONS &&
		    strcmp(pr->pr_key, "source") == 0) {
			sc_relpath(x, pr->pr_val, "a source file's path");
		} else if (coll == SH_C_DERIVATIONS &&
		    strcmp(pr->pr_key, "command") == 0) {
			(void) sc_string(x, pr->pr_val, "a command");
		} else if (coll == SH_C_PATCHES &&
		    strcmp(pr->pr_key, "dir") == 0) {
			sc_relpath(x, pr->pr_val, "a directory in the source");
		} else if (coll == SH_C_PATCHES &&
		    strcmp(pr->pr_key, "ref") == 0) {
			if (sc_string(x, pr->pr_val, "a git ref")) {
				for (p = pr->pr_val->sn_str; *p != '\0'; p++) {
					if ((unsigned char)*p <= 0x20 ||
					    *p == 0x7f)
						break;
				}
				if (*p != '\0')
					(void) sc_syn(x, "a git ref",
					    "a space or a control character");
			}
		} else {
			sh_err_at(x->sc_err, x->sc_file, &x->sc_path,
			    "not a field of %s: it takes %s",
			    coll == SH_C_DERIVATIONS ? "a derivation" :
			    "a patch source", coll == SH_C_DERIVATIONS ?
			    "source and command" : "ref and dir");
		}
		sh_path_pop(&x->sc_path, was);
	}
}

/* One entry of a collection: its key is on the path already. */
static void
sc_entry(struct sc *x, const struct sh_rkey *rk, const struct sh_node *v)
{
	const struct cdesc *cd = &cdescs[rk->rk_coll];

	switch (v->sn_type) {
	case SH_BOOL:
		if (!v->sn_bool || (cd->cd_entry & E_TRUE) != 0)
			return;
		break;
	case SH_OBJECT:
		if ((cd->cd_entry & E_OBJ) == 0)
			break;
		if (rk->rk_coll == SH_C_DATASETS)
			sc_props(x, v);
		else
			sc_fields(x, rk->rk_coll, v);
		return;
	case SH_STR:
		if ((cd->cd_entry & E_STR) == 0)
			break;
		(void) sc_string(x, v, cd->cd_forms);
		return;
	default:
		break;
	}
	sc_type(x, cd->cd_forms, v, (cd->cd_entry & E_STR) != 0);
}

static void
sc_coll(struct sc *x, const struct sh_rkey *rk, const struct sh_node *v)
{
	const struct cdesc *cd = &cdescs[rk->rk_coll];
	const struct sh_node *el;
	const struct sh_pair *pr;
	size_t i, j, was;

	if (v->sn_type == SH_OBJECT) {
		for (i = 0; i < v->sn_obj.ob_n; i++) {
			pr = &v->sn_obj.ob_v[i];
			was = sh_path_push(&x->sc_path, pr->pr_key);
			if (sc_syn(x, cd->cd_key, cd->cd_syn(pr->pr_key)))
				sc_entry(x, rk, pr->pr_val);
			sh_path_pop(&x->sc_path, was);
		}
		return;
	}
	if (v->sn_type != SH_ARRAY || !cd->cd_list) {
		sh_err_at(x->sc_err, x->sc_file, &x->sc_path,
		    "expected an object keyed by %s%s, not %s", cd->cd_key,
		    cd->cd_list ? ", or a list of them" : "",
		    sh_type_name(v->sn_type));
		return;
	}
	for (i = 0; i < v->sn_arr.ar_n; i++) {
		el = v->sn_arr.ar_v[i];
		was = sh_path_index(&x->sc_path, i);
		if (el->sn_type != SH_STR) {
			sc_type(x, cd->cd_key, el, 1);
		} else if (sc_syn(x, cd->cd_key, cd->cd_syn(el->sn_str))) {
			for (j = 0; j < i; j++) {
				if (v->sn_arr.ar_v[j]->sn_type == SH_STR &&
				    strcmp(v->sn_arr.ar_v[j]->sn_str,
				    el->sn_str) == 0)
					break;
			}
			if (j < i)
				sh_err_at(x->sc_err, x->sc_file, &x->sc_path,
				    "\"%s\" is listed twice", el->sn_str);
		}
		sh_path_pop(&x->sc_path, was);
	}
}

/* Which kinds take a key, for a message: "a foundation's". */
static const char *
sc_whose(unsigned kinds)
{
	switch (kinds) {
	case SH_FOUNDATION:
		return ("a foundation's");
	case SH_CONTAINER:
		return ("a container's");
	case SH_SYSTEM:
		return ("a system's");
	}
	return ("another kind's");
}

unsigned
sh_schema_recipe(const struct sh_node *layer, enum sh_kind kind,
    const char *file, struct sh_err *e)
{
	const struct sh_rkey *rk;
	const struct sh_pair *pr;
	struct sc x;
	unsigned before;
	size_t i, was;

	before = e->se_count;
	x.sc_file = file;
	x.sc_kind = kind;
	x.sc_err = e;
	sh_path_init(&x.sc_path);
	if (layer->sn_type != SH_OBJECT) {
		sc_type(&x, "an object", layer, 0);
		return (e->se_count - before);
	}
	for (i = 0; i < layer->sn_obj.ob_n; i++) {
		pr = &layer->sn_obj.ob_v[i];
		was = sh_path_push(&x.sc_path, pr->pr_key);
		rk = sh_rkey(pr->pr_key);
		if (rk == NULL) {
			sh_err_at(e, file, &x.sc_path, "not a key a recipe "
			    "takes");
		} else if ((rk->rk_kinds & kind) == 0) {
			sh_err_at(e, file, &x.sc_path, "not a key of a %s's "
			    "recipe: only %s takes it", sh_kind_name(kind),
			    sc_whose(rk->rk_kinds));
		} else {
			switch (rk->rk_vt) {
			case SH_V_IMPORT:
				sc_import(&x, pr->pr_val);
				break;
			case SH_V_RESET:
				sc_reset(&x, pr->pr_val);
				break;
			case SH_V_VER:
				if (sc_string(&x, pr->pr_val,
				    "a version constraint"))
					(void) sc_syn(&x,
					    "a version constraint",
					    sh_syn_ver(pr->pr_val->sn_str));
				break;
			case SH_V_STRING:
				(void) sc_string(&x, pr->pr_val, "a string");
				break;
			case SH_V_FILE:
				sc_relpath(&x, pr->pr_val, "a file's path");
				break;
			case SH_V_COLL:
				sc_coll(&x, rk, pr->pr_val);
				break;
			}
			if (rk->rk_twin != NULL && rk->rk_vt != SH_V_FILE &&
			    sh_obj_find(layer, rk->rk_twin) != NULL)
				sh_err_at(e, file, &x.sc_path, "and %s both: "
				    "a layer gives one or the other",
				    rk->rk_twin);
		}
		sh_path_pop(&x.sc_path, was);
	}
	return (e->se_count - before);
}
