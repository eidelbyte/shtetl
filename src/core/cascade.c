/*
 * The recipe cascade (planning.md 5.4).
 *
 * Loading: every layer is parsed and checked against the target's
 * kind before anything merges, and a refused layer does not stop the
 * others from being read, so one run reports every layer's problems.
 * "import" is followed in every layer, default.conf's included: a
 * file's imports merge before it, each recipe once, at the first
 * place it is reached. A cycle is refused.
 *
 * Merging, layer by layer: reset empties the collections it names;
 * a scalar is replaced and keeps its place (a heredoc key replaces
 * its _file twin too); a collection merges entry by entry, a list
 * of names standing for name = true. In an entry, false drops it,
 * true adds it or leaves it as it is, fields merge field by field,
 * and a string replaces. A key keeps the place where it was first
 * added, so a commands block a later layer overrides runs where it
 * was first defined.
 *
 * The drop rule: a container or system may not drop, by false or by
 * reset, a pkg or dataset that a shared layer gave, since its
 * foundation has it too and a container keeps everything its
 * foundation has (7.4). A drop of what no layer gave is refused as
 * well: it is a misspelling, or a layer imported where it does not
 * belong.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cascade.h"
#include "core/err.h"
#include "core/parse.h"
#include "core/root.h"
#include "core/schema.h"
#include "core/syntax.h"
#include "core/tree.h"

static const char cascade_builtin[] = "(built-in)";

/* The built-in recipe defaults, 5.4's first layer: none yet. */
static const char cascade_builtin_text[] = "";

/* One load: the stack of files being read, for cycles. */
struct ld {
	const char		*ld_root;
	struct sh_recipe	*ld_r;
	struct sh_err		*ld_err;
	const char		*ld_stack[SH_IMPORT_DEPTH];
	size_t			ld_depth;
	int			ld_shared;	/* loading default.conf's */
	int			ld_failed;
};

/* One merge: the layer going in, and where in it. */
struct mg {
	struct sh_recipe	*mg_r;
	const struct sh_layer	*mg_l;
	struct sh_err		*mg_err;
	struct sh_path		mg_path;
	int			mg_nomem;
};

const struct sh_layer *
sh_recipe_layer(const struct sh_recipe *r, const char *from)
{
	size_t i;

	for (i = 0; i < r->sr_nlayers; i++) {
		if (r->sr_layers[i].sl_file == from)
			return (&r->sr_layers[i]);
	}
	return (NULL);
}

static int
ld_append(struct ld *x, char *file, struct sh_node *tree)
{
	struct sh_recipe *r = x->ld_r;
	struct sh_layer *nl;

	nl = realloc(r->sr_layers, (r->sr_nlayers + 1) * sizeof (*nl));
	if (nl == NULL)
		return (-1);
	r->sr_layers = nl;
	nl[r->sr_nlayers].sl_file = file;
	nl[r->sr_nlayers].sl_tree = tree;
	nl[r->sr_nlayers].sl_shared = x->ld_shared;
	r->sr_nlayers++;
	return (0);
}

static int
ld_has(const struct sh_recipe *r, const char *file)
{
	size_t i;

	for (i = 0; i < r->sr_nlayers; i++) {
		if (strcmp(r->sr_layers[i].sl_file, file) == 0)
			return (1);
	}
	return (0);
}

/* A cycle: the files from stack entry I on, and NAME again. */
static void
ld_cycle(struct ld *x, size_t i, const char *name, const char *from,
    const struct sh_path *fpath)
{
	char buf[1024];
	size_t n;

	buf[0] = '\0';
	for (; i < x->ld_depth; i++) {
		n = strlen(buf);
		(void) snprintf(buf + n, sizeof (buf) - n, "%s, ",
		    x->ld_stack[i]);
	}
	n = strlen(buf);
	(void) snprintf(buf + n, sizeof (buf) - n, "%s", name);
	sh_err_at(x->ld_err, from, fpath, "an import cycle: %s", buf);
}

static int ld_load(struct ld *x, const char *name, const char *from,
    const struct sh_path *fpath, int optional);

/* The imports of FILE, whose tree is TREE, each loaded before it. */
static int
ld_imports(struct ld *x, const char *file, const struct sh_node *tree)
{
	const struct sh_node *imp, *el;
	struct sh_path path;
	size_t i;

	imp = sh_obj_get(tree, "import");
	if (imp == NULL)
		return (0);
	sh_path_init(&path);
	(void) sh_path_push(&path, "import");
	if (imp->sn_type == SH_STR) {
		if (sh_syn_relpath(imp->sn_str) != NULL)
			return (0);
		return (ld_load(x, imp->sn_str, file, &path, 0));
	}
	if (imp->sn_type != SH_ARRAY)
		return (0);
	for (i = 0; i < imp->sn_arr.ar_n; i++) {
		el = imp->sn_arr.ar_v[i];
		/* what the schema refused, it has reported */
		if (el->sn_type != SH_STR || sh_syn_relpath(el->sn_str) != NULL)
			continue;
		(void) sh_path_index(&path, i);
		if (ld_load(x, el->sn_str, file, &path, 0) != 0)
			return (-1);
		sh_path_pop(&path, strlen("import"));
	}
	return (0);
}

/*
 * Loads NAME, after what it imports. FROM and FPATH say where NAME
 * was named, for messages (FROM NULL: by shtetl itself). OPTIONAL: a
 * missing file is no layer rather than an error. -1 only when out of
 * memory: a refusal sets ld_failed, and loading goes on.
 */
static int
ld_load(struct ld *x, const char *name, const char *from,
    const struct sh_path *fpath, int optional)
{
	struct sh_node *tree;
	char *file;
	size_t i;
	int missing;

	if (ld_has(x->ld_r, name))
		return (0);
	for (i = 0; i < x->ld_depth; i++) {
		if (strcmp(x->ld_stack[i], name) == 0) {
			ld_cycle(x, i, name, from, fpath);
			x->ld_failed = 1;
			return (0);
		}
	}
	if (x->ld_depth == SH_IMPORT_DEPTH) {
		sh_err_at(x->ld_err, from, fpath, "imports nested more than "
		    "%d deep", SH_IMPORT_DEPTH);
		x->ld_failed = 1;
		return (0);
	}
	if ((file = strdup(name)) == NULL)
		return (-1);
	tree = sh_root_parse(x->ld_root, file, &missing, x->ld_err);
	if (tree == NULL) {
		if (missing && from != NULL)
			sh_err_at(x->ld_err, from, fpath, "%s: no such recipe "
			    "in the configuration root", name);
		else if (missing && !optional)
			sh_errf(x->ld_err, "%s: no such recipe in the "
			    "configuration root", name);
		if (!missing || !optional)
			x->ld_failed = 1;
		free(file);
		return (0);
	}
	if (sh_schema_recipe(tree, x->ld_r->sr_kind, file, x->ld_err) != 0)
		x->ld_failed = 1;
	x->ld_stack[x->ld_depth++] = file;
	if (ld_imports(x, file, tree) != 0) {
		x->ld_depth--;
		sh_node_free(tree);
		free(file);
		return (-1);
	}
	x->ld_depth--;
	if (ld_append(x, file, tree) != 0) {
		sh_node_free(tree);
		free(file);
		return (-1);
	}
	return (0);
}

/* The built-in layer, when it holds anything. */
static int
ld_builtin(struct ld *x)
{
	struct sh_node *tree;
	char *file;

	if ((file = strdup(cascade_builtin)) == NULL)
		return (-1);
	tree = sh_parse_buf(cascade_builtin_text,
	    sizeof (cascade_builtin_text) - 1, file, x->ld_err);
	if (tree == NULL) {
		free(file);
		x->ld_failed = 1;
		return (0);
	}
	if (tree->sn_obj.ob_n == 0) {
		sh_node_free(tree);
		free(file);
		return (0);
	}
	if (sh_schema_recipe(tree, x->ld_r->sr_kind, file, x->ld_err) != 0)
		x->ld_failed = 1;
	if (ld_append(x, file, tree) != 0) {
		sh_node_free(tree);
		free(file);
		return (-1);
	}
	return (0);
}

/* TARGET's kind, name and file, into R. */
static int
rs_target(const char *target, struct sh_recipe *r, struct sh_err *e)
{
	static const struct {
		const char	*t_dir;
		enum sh_kind	t_kind;
	} dirs[] = {
		{ "foundations", SH_FOUNDATION },
		{ "containers", SH_CONTAINER },
		{ "systems", SH_SYSTEM }
	};
	const char *slash, *name, *why;
	size_t i, dlen, nlen, flen;

	slash = strchr(target, '/');
	for (i = 0; slash != NULL && i < sizeof (dirs) / sizeof (dirs[0]);
	    i++) {
		dlen = strlen(dirs[i].t_dir);
		if ((size_t)(slash - target) == dlen &&
		    strncmp(target, dirs[i].t_dir, dlen) == 0)
			break;
	}
	if (slash == NULL || i == sizeof (dirs) / sizeof (dirs[0])) {
		sh_errf(e, "%s: not a recipe: name one by its path, as "
		    "foundations/F, containers/X or systems/S", target);
		return (-1);
	}
	r->sr_kind = dirs[i].t_kind;
	name = slash + 1;
	nlen = strlen(name);
	if (nlen > 5 && strcmp(name + nlen - 5, ".conf") == 0)
		nlen -= 5;
	if ((r->sr_name = malloc(nlen + 1)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	(void) memcpy(r->sr_name, name, nlen);
	r->sr_name[nlen] = '\0';
	if ((why = sh_syn_name(r->sr_name)) != NULL) {
		sh_errf(e, "%s: not a recipe's name: %s", target, why);
		return (-1);
	}
	flen = strlen(dirs[i].t_dir) + 1 + nlen + strlen(".conf") + 1;
	if ((r->sr_file = malloc(flen)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	(void) snprintf(r->sr_file, flen, "%s/%s.conf", dirs[i].t_dir,
	    r->sr_name);
	return (0);
}

/*
 * The shared layer whose element a drop would take, when the drop
 * rule forbids it: a container or system, pkg or datasets.
 */
static const struct sh_layer *
mg_forbidden(const struct mg *m, enum sh_coll coll, const char *from)
{
	const struct sh_layer *l;

	if (m->mg_r->sr_kind == SH_FOUNDATION || !sh_coll_superset(coll))
		return (NULL);
	l = sh_recipe_layer(m->mg_r, from);
	return (l != NULL && l->sl_shared ? l : NULL);
}

static void
mg_set(struct mg *m, struct sh_node *obj, const char *key,
    const struct sh_node *v)
{
	struct sh_node *c;

	if ((c = sh_node_copy(v)) == NULL ||
	    sh_obj_set(obj, key, c, m->mg_l->sl_file) != 0)
		m->mg_nomem = 1;
}

/* One entry, EV under key EK, into the merged collection COLL. */
static void
mg_entry(struct mg *m, struct sh_node *coll, const struct sh_rkey *rk,
    const char *ek, const struct sh_node *ev)
{
	struct sh_pair *cur;
	const struct sh_layer *sl;
	const struct sh_pair *f;
	size_t i;

	cur = sh_obj_find(coll, ek);
	if (ev->sn_type == SH_BOOL && !ev->sn_bool) {
		if (cur == NULL) {
			sh_err_at(m->mg_err, m->mg_l->sl_file, &m->mg_path,
			    "false drops nothing: no layer before this one "
			    "gives it");
		} else if ((sl = mg_forbidden(m, rk->rk_coll,
		    cur->pr_from)) != NULL) {
			sh_err_at(m->mg_err, m->mg_l->sl_file, &m->mg_path,
			    "false drops what %s gives, and %s is a layer of "
			    "every foundation's recipe too: a %s keeps "
			    "everything its foundation has", sl->sl_file,
			    sl->sl_file, sh_kind_name(m->mg_r->sr_kind));
		} else {
			(void) sh_obj_del(coll, ek);
		}
		return;
	}
	if (ev->sn_type == SH_BOOL && cur != NULL)
		return;
	if (ev->sn_type == SH_OBJECT && cur != NULL &&
	    cur->pr_val->sn_type == SH_OBJECT) {
		for (i = 0; i < ev->sn_obj.ob_n; i++) {
			f = &ev->sn_obj.ob_v[i];
			mg_set(m, cur->pr_val, f->pr_key, f->pr_val);
		}
		return;
	}
	mg_set(m, coll, ek, ev);
}

static void
mg_coll(struct mg *m, const struct sh_rkey *rk, const struct sh_node *v)
{
	struct sh_node *coll, yes;
	const struct sh_node *el;
	const struct sh_pair *pr;
	size_t i, was;

	coll = sh_obj_get(m->mg_r->sr_merged, rk->rk_name);
	if (coll == NULL) {
		if ((coll = sh_node_new(SH_OBJECT, m->mg_l->sl_file)) == NULL ||
		    sh_obj_set(m->mg_r->sr_merged, rk->rk_name, coll,
		    m->mg_l->sl_file) != 0) {
			m->mg_nomem = 1;
			return;
		}
	}
	if (v->sn_type == SH_ARRAY) {
		/* a list of names: name = true, from this layer */
		(void) memset(&yes, 0, sizeof (yes));
		yes.sn_type = SH_BOOL;
		yes.sn_bool = 1;
		yes.sn_from = m->mg_l->sl_file;
		for (i = 0; i < v->sn_arr.ar_n; i++) {
			el = v->sn_arr.ar_v[i];
			was = sh_path_push(&m->mg_path, el->sn_str);
			mg_entry(m, coll, rk, el->sn_str, &yes);
			sh_path_pop(&m->mg_path, was);
		}
		return;
	}
	for (i = 0; i < v->sn_obj.ob_n; i++) {
		pr = &v->sn_obj.ob_v[i];
		was = sh_path_push(&m->mg_path, pr->pr_key);
		mg_entry(m, coll, rk, pr->pr_key, pr->pr_val);
		sh_path_pop(&m->mg_path, was);
	}
}

/* reset: one collection NAME emptied, unless the drop rule says no. */
static void
mg_reset_one(struct mg *m, const char *name)
{
	struct sh_node *merged = m->mg_r->sr_merged;
	const struct sh_node *have;
	const struct sh_layer *sl;
	const struct sh_rkey *rk;
	size_t i;

	if ((have = sh_obj_get(merged, name)) == NULL ||
	    (rk = sh_rkey(name)) == NULL)
		return;
	for (i = 0; i < have->sn_obj.ob_n; i++) {
		sl = mg_forbidden(m, rk->rk_coll,
		    have->sn_obj.ob_v[i].pr_from);
		if (sl != NULL) {
			sh_err_at(m->mg_err, m->mg_l->sl_file, &m->mg_path,
			    "emptying %s drops %s, which %s gives, and %s is "
			    "a layer of every foundation's recipe too: a %s "
			    "keeps everything its foundation has", name,
			    have->sn_obj.ob_v[i].pr_key, sl->sl_file,
			    sl->sl_file, sh_kind_name(m->mg_r->sr_kind));
			return;
		}
	}
	(void) sh_obj_del(merged, name);
}

static void
mg_reset(struct mg *m, const struct sh_node *v)
{
	size_t i, was;

	was = sh_path_push(&m->mg_path, "reset");
	if (v->sn_type == SH_STR) {
		mg_reset_one(m, v->sn_str);
	} else {
		for (i = 0; i < v->sn_arr.ar_n; i++) {
			(void) sh_path_index(&m->mg_path, i);
			mg_reset_one(m, v->sn_arr.ar_v[i]->sn_str);
			sh_path_pop(&m->mg_path, strlen("reset"));
		}
	}
	sh_path_pop(&m->mg_path, was);
}

/* One layer into the merged recipe. */
static void
mg_layer(struct mg *m)
{
	const struct sh_node *tree = m->mg_l->sl_tree;
	const struct sh_rkey *rk;
	const struct sh_pair *pr;
	const struct sh_node *v;
	size_t i, was;

	if ((v = sh_obj_get(tree, "reset")) != NULL)
		mg_reset(m, v);
	for (i = 0; i < tree->sn_obj.ob_n; i++) {
		pr = &tree->sn_obj.ob_v[i];
		rk = sh_rkey(pr->pr_key);
		if (rk->rk_vt == SH_V_IMPORT || rk->rk_vt == SH_V_RESET)
			continue;
		was = sh_path_push(&m->mg_path, pr->pr_key);
		if (rk->rk_vt == SH_V_COLL) {
			mg_coll(m, rk, pr->pr_val);
		} else {
			if (rk->rk_twin != NULL)
				(void) sh_obj_del(m->mg_r->sr_merged,
				    rk->rk_twin);
			mg_set(m, m->mg_r->sr_merged, pr->pr_key, pr->pr_val);
		}
		sh_path_pop(&m->mg_path, was);
	}
}

/*
 * The checks only the merged recipe can answer: what a kind needs,
 * entries made whole across layers, and what the configuration root
 * must hold.
 */
static void
ck_merged(const struct sh_recipe *r, const char *root, struct sh_err *e)
{
	static const char *const files[] = { "kernconf_file",
	    "buildopts_file", "jailconf_file" };
	const struct sh_node *merged = r->sr_merged;
	const struct sh_node *v, *coll, *dir;
	const struct sh_pair *pr;
	struct sh_path path;
	enum sh_loc loc;
	char *sub, rel[1100];
	size_t i;

	sh_path_init(&path);
	if (r->sr_kind == SH_FOUNDATION &&
	    sh_obj_get(merged, "src_branch") == NULL)
		sh_errf(e, "%s: no layer gives src_branch: a foundation is "
		    "built from a FreeBSD branch", r->sr_file);
	for (i = 0; i < sizeof (files) / sizeof (files[0]); i++) {
		if ((v = sh_obj_get(merged, files[i])) == NULL ||
		    sh_root_kind(root, v->sn_str) == 'f')
			continue;
		(void) sh_path_push(&path, files[i]);
		sh_err_at(e, v->sn_from, &path, "%s: no such file in the "
		    "configuration root", v->sn_str);
		sh_path_pop(&path, 0);
	}
	coll = sh_obj_get(merged, "derivations");
	for (i = 0; coll != NULL && i < coll->sn_obj.ob_n; i++) {
		pr = &coll->sn_obj.ob_v[i];
		(void) sh_path_push(&path, "derivations");
		(void) sh_path_push(&path, pr->pr_key);
		if (sh_obj_get(pr->pr_val, "source") == NULL)
			sh_err_at(e, pr->pr_from, &path, "no layer gives its "
			    "source");
		if (sh_obj_get(pr->pr_val, "command") == NULL)
			sh_err_at(e, pr->pr_from, &path, "no layer gives its "
			    "command");
		sh_path_pop(&path, 0);
	}
	coll = sh_obj_get(merged, "patches");
	for (i = 0; coll != NULL && i < coll->sn_obj.ob_n; i++) {
		pr = &coll->sn_obj.ob_v[i];
		if (sh_syn_location(pr->pr_key, &loc) != NULL ||
		    loc != SH_LOC_DIR)
			continue;
		(void) sh_path_push(&path, "patches");
		(void) sh_path_push(&path, pr->pr_key);
		dir = sh_obj_get(pr->pr_val, "dir");
		if (dir != NULL) {
			(void) snprintf(rel, sizeof (rel), "%s/%s",
			    pr->pr_key, dir->sn_str);
			sub = rel;
		} else {
			sub = pr->pr_key;
		}
		if (sh_root_kind(root, sub) != 'd')
			sh_err_at(e, pr->pr_from, &path, "%s: no such "
			    "directory in the configuration root", sub);
		sh_path_pop(&path, 0);
	}
	coll = sh_obj_get(merged, "requires");
	for (i = 0; coll != NULL && i < coll->sn_obj.ob_n; i++) {
		pr = &coll->sn_obj.ob_v[i];
		(void) sh_path_push(&path, "requires");
		(void) sh_path_push(&path, pr->pr_key);
		(void) snprintf(rel, sizeof (rel), "containers/%s.conf",
		    pr->pr_key);
		if (strcmp(pr->pr_key, r->sr_name) == 0)
			sh_err_at(e, pr->pr_from, &path, "a container cannot "
			    "require itself");
		else if (sh_root_kind(root, rel) != 'f')
			sh_err_at(e, pr->pr_from, &path, "no such container "
			    "recipe: %s", rel);
		sh_path_pop(&path, 0);
	}
}

int
sh_recipe_resolve(const char *root, const char *target,
    struct sh_recipe *r, struct sh_err *e)
{
	struct ld x;
	struct mg m;
	unsigned before;
	size_t i;

	(void) memset(r, 0, sizeof (*r));
	before = e->se_count;
	if (rs_target(target, r, e) != 0)
		return (-1);
	if (sh_root_kind(root, r->sr_file) == 0) {
		sh_errf(e, "%s: no such recipe in the configuration root",
		    r->sr_file);
		return (-1);
	}
	(void) memset(&x, 0, sizeof (x));
	x.ld_root = root;
	x.ld_r = r;
	x.ld_err = e;
	x.ld_shared = 1;
	if (ld_builtin(&x) != 0 ||
	    ld_load(&x, "default.conf", NULL, NULL, 1) != 0)
		goto nomem;
	x.ld_shared = 0;
	if (ld_load(&x, r->sr_file, NULL, NULL, 0) != 0)
		goto nomem;
	if (x.ld_failed)
		return (-1);

	if ((r->sr_merged = sh_node_new(SH_OBJECT, r->sr_file)) == NULL)
		goto nomem;
	(void) memset(&m, 0, sizeof (m));
	m.mg_r = r;
	m.mg_err = e;
	for (i = 0; i < r->sr_nlayers; i++) {
		m.mg_l = &r->sr_layers[i];
		sh_path_init(&m.mg_path);
		mg_layer(&m);
		if (m.mg_nomem)
			goto nomem;
	}
	if (e->se_count == before)
		ck_merged(r, root, e);
	return (e->se_count == before ? 0 : -1);
nomem:
	sh_err_nomem(e);
	return (-1);
}

void
sh_recipe_free(struct sh_recipe *r)
{
	size_t i;

	sh_node_free(r->sr_merged);
	for (i = 0; i < r->sr_nlayers; i++) {
		sh_node_free(r->sr_layers[i].sl_tree);
		free(r->sr_layers[i].sl_file);
	}
	free(r->sr_layers);
	free(r->sr_name);
	free(r->sr_file);
	(void) memset(r, 0, sizeof (*r));
}
