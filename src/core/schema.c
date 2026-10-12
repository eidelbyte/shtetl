/*
 * The keys of a recipe (planning.md 5.5) and the check of one layer.
 *
 * A layer is checked against the kind of the recipe it is merged
 * into, not against the file it is: an imported common/web.conf is a
 * layer of a foundation, a container or a system, and takes that
 * kind's keys (5.3: "keys that belong to another kind are errors").
 * The default's files (shtetl.conf and its imports) are a layer of
 * every kind, so they take every kind's keys, and each recipe takes
 * those of its own (ruled 2026-10-12). Entries may be given in parts
 * across files, a dataset's properties or a patch source's fields;
 * a derivation is given whole, in one file.
 */

#include <string.h>

#include "core/err.h"
#include "core/schema.h"
#include "core/site.h"
#include "core/syntax.h"
#include "core/tree.h"

#define	F	SH_FOUNDATION
#define	C	SH_CONTAINER

/*
 * kernconf_file is the kernel config the foundation's starts from (a
 * name in the src tree's conf directory, GENERIC; or a file in
 * zconfig, a path) and kernconf_lines the lines added to it; shtetl
 * writes the config itself (planning.md 5.5), as it writes the
 * src.conf buildopts goes into and the jail's block jailconf goes
 * into.
 */
static const struct sh_rkey rkeys[] = {
	{ "import", SH_ALLKINDS, SH_V_IMPORT, SH_C_NONE, SH_M_LAYERS },
	{ "ver", SH_ALLKINDS, SH_V_VER, SH_C_NONE, SH_M_ALL },
	{ "src_branch", F, SH_V_STRING, SH_C_NONE, SH_M_AGREE },
	{ "kernconf_file", F, SH_V_FILE, SH_C_NONE, SH_M_AGREE },
	{ "kernconf_lines", F, SH_V_STRING, SH_C_NONE, SH_M_TEXT },
	{ "buildopts", F, SH_V_STRING, SH_C_NONE, SH_M_TEXT },
	{ "patches", F, SH_V_COLL, SH_C_PATCHES, SH_M_COLL },
	{ "datasets", SH_ALLKINDS, SH_V_COLL, SH_C_DATASETS, SH_M_COLL },
	{ "pkg", SH_ALLKINDS, SH_V_COLL, SH_C_PKG, SH_M_COLL },
	{ "commands", SH_ALLKINDS, SH_V_COLL, SH_C_COMMANDS, SH_M_COLL },
	{ "derivations", SH_ALLKINDS, SH_V_COLL, SH_C_DERIVATIONS, SH_M_COLL },
	{ "requires", C, SH_V_COLL, SH_C_REQUIRES, SH_M_COLL },
	{ "jailconf", C, SH_V_STRING, SH_C_NONE, SH_M_TEXT },
	{ NULL, 0, SH_V_STRING, SH_C_NONE, SH_M_LAYERS }
};

/* Keys there are no more, and what to write instead. */
static const struct {
	const char	*g_name;
	const char	*g_why;
} gone[] = {
	{ "reset", "nothing is reset: the layers stack, and what one gives "
	    "stays (ruled 2026-10-12)" },
	{ "kernconf", "kernconf_file names the config to start from and "
	    "kernconf_lines adds to it (ruled 2026-10-12)" },
	{ "jailconf_file", "shtetl writes the jail's block itself, and "
	    "jailconf gives the options in it (ruled 2026-10-12)" },
	{ "buildopts_file", "shtetl writes the src.conf itself, and "
	    "buildopts gives the lines in it (ruled 2026-10-12)" },
	{ NULL, NULL }
};

#undef	F
#undef	C

/* The forms a collection's entry takes. */
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
	[SH_C_PKG] = { "a package's name", sh_syn_pkg, 1, E_TRUE, "true" },
	[SH_C_DATASETS] = { "a dataset path", sh_syn_dataset, 1,
	    E_TRUE | E_OBJ, "true, or its properties" },
	[SH_C_COMMANDS] = { "a label", sh_syn_label, 0, E_STR,
	    "its commands, as a string or heredoc" },
	[SH_C_DERIVATIONS] = { "a derived file's path", sh_syn_relpath, 0,
	    E_OBJ, "{ source; command; }" },
	[SH_C_REQUIRES] = { "a container's name", sh_syn_name, 1, E_TRUE,
	    "true" },
	[SH_C_PATCHES] = { "a patch source's location", cd_location, 1,
	    E_TRUE | E_OBJ, "true, or { ref; dir; }" }
};

/* One check's place: the file, the kind, where in the tree. */
struct sc {
	const char	*sc_file;
	unsigned	sc_kind;	/* kinds taken: one, or all */
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

/* ver: a version constraint, or a list of them, all to hold. */
static void
sc_ver_one(struct sc *x, const struct sh_node *v)
{
	if (sc_string(x, v, "a version constraint"))
		(void) sc_syn(x, "a version constraint",
		    sh_syn_ver(v->sn_str));
}

static void
sc_ver(struct sc *x, const struct sh_node *v)
{
	size_t i, was;

	if (v->sn_type != SH_ARRAY) {
		sc_ver_one(x, v);
		return;
	}
	for (i = 0; i < v->sn_arr.ar_n; i++) {
		was = sh_path_index(&x->sc_path, i);
		sc_ver_one(x, v->sn_arr.ar_v[i]);
		sh_path_pop(&x->sc_path, was);
	}
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

/*
 * The fields of a derivation, or of a patch source. A derivation is
 * given whole, in one file: no file adds to or redefines another's
 * (ruled 2026-10-12).
 */
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
	if (coll == SH_C_DERIVATIONS && (sh_obj_get(entry, "source") == NULL ||
	    sh_obj_get(entry, "command") == NULL))
		sh_err_at(x->sc_err, x->sc_file, &x->sc_path, "a derivation "
		    "gives its source and its command together, in one file");
}

/* One entry of a collection: its key is on the path already. */
static void
sc_entry(struct sc *x, const struct sh_rkey *rk, const struct sh_node *v)
{
	const struct cdesc *cd = &cdescs[rk->rk_coll];

	switch (v->sn_type) {
	case SH_BOOL:
		if (!v->sn_bool) {
			sh_err_at(x->sc_err, x->sc_file, &x->sc_path,
			    "false drops nothing: the layers stack, and what "
			    "one gives stays (ruled 2026-10-12)");
			return;
		}
		if ((cd->cd_entry & E_TRUE) != 0)
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

/*
 * One layer against the keys of the kinds in KINDS: one kind for a
 * recipe's own files, all of them for the default's. SETTINGS: the
 * file is shtetl.conf, whose site settings are not recipe keys.
 */
static unsigned
sc_layer(const struct sh_node *layer, unsigned kinds, const char *file,
    int settings, struct sh_err *e)
{
	const struct sh_rkey *rk;
	const struct sh_pair *pr;
	struct sc x;
	unsigned before;
	size_t i, j, was;

	before = e->se_count;
	x.sc_file = file;
	x.sc_kind = kinds;
	x.sc_err = e;
	sh_path_init(&x.sc_path);
	if (layer->sn_type != SH_OBJECT) {
		sc_type(&x, "an object", layer, 0);
		return (e->se_count - before);
	}
	for (i = 0; i < layer->sn_obj.ob_n; i++) {
		pr = &layer->sn_obj.ob_v[i];
		if (settings && sh_site_section(pr->pr_key))
			continue;
		was = sh_path_push(&x.sc_path, pr->pr_key);
		rk = sh_rkey(pr->pr_key);
		for (j = 0; gone[j].g_name != NULL; j++) {
			if (strcmp(pr->pr_key, gone[j].g_name) == 0)
				break;
		}
		if (gone[j].g_name != NULL) {
			sh_err_at(e, file, &x.sc_path, "not a key any more: %s",
			    gone[j].g_why);
		} else if (rk == NULL) {
			sh_err_at(e, file, &x.sc_path, "not a key %s takes",
			    settings ? "shtetl.conf" : "a recipe");
		} else if ((rk->rk_kinds & kinds) == 0) {
			sh_err_at(e, file, &x.sc_path, "not a key of a %s's "
			    "recipe: only %s takes it",
			    sh_kind_name((enum sh_kind)kinds),
			    sc_whose(rk->rk_kinds));
		} else {
			switch (rk->rk_vt) {
			case SH_V_IMPORT:
				sc_import(&x, pr->pr_val);
				break;
			case SH_V_VER:
				sc_ver(&x, pr->pr_val);
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
		}
		sh_path_pop(&x.sc_path, was);
	}
	return (e->se_count - before);
}

unsigned
sh_schema_recipe(const struct sh_node *layer, enum sh_kind kind,
    const char *file, struct sh_err *e)
{
	return (sc_layer(layer, (unsigned)kind, file, 0, e));
}

unsigned
sh_schema_default(const struct sh_node *layer, const char *file,
    int settings, struct sh_err *e)
{
	return (sc_layer(layer, SH_ALLKINDS, file, settings, e));
}
