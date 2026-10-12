/*
 * The recipe cascade (planning.md 5.4).
 *
 * Loading: every layer is parsed and checked against the target's
 * kind before anything merges, and a refused layer does not stop the
 * others from being read, so one run reports every layer's problems.
 * The default comes first: shtetl.conf, checked whole, its settings
 * too, and what it imports, checked as files of any kind. "import"
 * is followed in every layer, the default's included: a file's
 * imports merge before it, each recipe once, at the first place it
 * is reached. A cycle is refused.
 *
 * Merging, layer by layer, with everything stacking (ruled
 * 2026-10-12): nothing is dropped and nothing reset, and no file
 * redefines what another gave. A collection merges entry by entry, a
 * list of names standing for name = true: an entry listed again adds
 * nothing; its fields compose across files, each given once, but for
 * a dataset's properties, which a nearer file may override (with a
 * warning); a derivation is given whole, in one file; a commands
 * block's texts stack, a piece from each file. Every file's ver is
 * kept, to hold together, and the ranges they allow must meet;
 * buildopts, jailconf and kernconf's lines append, a piece from each
 * file, lower layers first; src_branch, one value, may be given again
 * only alike, and kernconf's file is given by one file. A recipe takes
 * from the default only its kind's keys, and what would redefine the
 * default's value overrides it instead. A key keeps the place where it
 * was first added, so a commands block runs where it was first
 * defined.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cascade.h"
#include "core/err.h"
#include "core/parse.h"
#include "core/root.h"
#include "core/schema.h"
#include "core/site.h"
#include "core/syntax.h"
#include "core/tree.h"

/* The file of the default layer (planning.md 5.4). */
static const char cascade_default[] = "shtetl.conf";

/* One load: the stack of files being read, for cycles. */
struct ld {
	const char		*ld_root;
	struct sh_recipe	*ld_r;
	struct sh_err		*ld_err;
	const char		*ld_stack[SH_IMPORT_DEPTH];
	size_t			ld_depth;
	int			ld_default;	/* loading the default */
	int			ld_failed;
};

/* One merge: the layer going in, and where in it. */
struct mg {
	struct sh_recipe	*mg_r;
	const struct sh_layer	*mg_l;
	struct sh_err		*mg_err;
	struct sh_err		*mg_warn;	/* or NULL */
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
	nl[r->sr_nlayers].sl_default = x->ld_default;
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
	sh_err_at(x->ld_err, from, fpath, "import cycle: %s", buf);
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
	unsigned n;
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
		sh_err_at(x->ld_err, from, fpath, "imports nested deeper than "
		    "%d", SH_IMPORT_DEPTH);
		x->ld_failed = 1;
		return (0);
	}
	if ((file = strdup(name)) == NULL)
		return (-1);
	tree = sh_root_parse(x->ld_root, file, &missing, x->ld_err);
	if (tree == NULL) {
		if (missing && from != NULL)
			sh_err_at(x->ld_err, from, fpath, "%s: no such file",
			    name);
		else if (missing && !optional)
			sh_errf(x->ld_err, "%s: no such file", name);
		if (!missing || !optional)
			x->ld_failed = 1;
		free(file);
		return (0);
	}
	/*
	 * shtetl.conf is checked whole, its settings too, wherever it is
	 * read (ruled 2026-10-12); the rest of the default as files of
	 * any kind, and every other file as one of the recipe's kind.
	 */
	if (x->ld_default && strcmp(file, cascade_default) == 0)
		n = sh_site_check(tree, file, x->ld_err);
	else if (x->ld_default)
		n = sh_schema_default(tree, file, 0, x->ld_err);
	else
		n = sh_schema_recipe(tree, x->ld_r->sr_kind, file, x->ld_err);
	if (n != 0)
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
		sh_errf(e, "%s: invalid recipe: use foundations/NAME, "
		    "containers/NAME or systems/NAME", target);
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
		sh_errf(e, "%s: invalid recipe name: %s", target, why);
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

/* Whether the value V came from the default layer. */
static int
mg_default(const struct mg *m, const struct sh_node *v)
{
	const struct sh_layer *l;

	l = sh_recipe_layer(m->mg_r, v->sn_from);
	return (l != NULL && l->sl_default);
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

/* A value as a message shows it. */
static const char *
mg_said(const struct sh_node *v)
{
	if (v->sn_type == SH_STR)
		return (v->sn_str);
	if (v->sn_type == SH_BOOL)
		return (v->sn_bool ? "true" : "false");
	return (sh_type_name(v->sn_type));
}

/*
 * A commands block's text EV, one more piece of the block EK: the
 * block is an object of its pieces, each keyed by the file that gives
 * it, in the order the files merge. A file gives a block once, so a
 * key is never given twice; and the key is what a container needs to
 * run each piece once (7.4).
 */
static void
mg_piece(struct mg *m, struct sh_node *coll, struct sh_pair *cur,
    const char *ek, const struct sh_node *ev)
{
	struct sh_node *block;

	if (cur != NULL) {
		mg_set(m, cur->pr_val, m->mg_l->sl_file, ev);
		return;
	}
	if ((block = sh_node_new(SH_OBJECT, m->mg_l->sl_file)) == NULL ||
	    sh_obj_set(coll, ek, block, m->mg_l->sl_file) != 0) {
		m->mg_nomem = 1;
		return;
	}
	mg_set(m, block, m->mg_l->sl_file, ev);
}

/*
 * One entry, EV under key EK of the collection RK, into the merged
 * COLL (ruled 2026-10-12). Listing an entry again adds nothing and is
 * no redefinition. What an entry holds is given once: a field a file
 * gives a second time is refused, and so is a derivation given in two
 * files; two files giving one entry different parts of it compose.
 * But a dataset's property a nearer file may override, with a
 * warning, the default's as any other's; and a commands block's texts
 * stack, each a piece of its own. What the default gives, a recipe
 * overrides, always with a warning (ruled 2026-10-12).
 */
static void
mg_entry(struct mg *m, struct sh_node *coll, const struct sh_rkey *rk,
    const char *ek, const struct sh_node *ev)
{
	struct sh_pair *cur, *have;
	const struct sh_pair *f;
	size_t i, was;

	cur = sh_obj_find(coll, ek);
	if (rk->rk_coll == SH_C_COMMANDS) {
		mg_piece(m, coll, cur, ek, ev);
		return;
	}
	if (rk->rk_coll == SH_C_DERIVATIONS && cur != NULL) {
		if (mg_default(m, cur->pr_val)) {
			if (m->mg_warn != NULL &&
			    !sh_node_equal(cur->pr_val, ev))
				sh_err_at(m->mg_warn, m->mg_l->sl_file,
				    &m->mg_path, "overrides %s",
				    cur->pr_val->sn_from);
			mg_set(m, coll, ek, ev);
		} else
			sh_err_at(m->mg_err, m->mg_l->sl_file, &m->mg_path,
			    "already defined in %s", cur->pr_from);
		return;
	}
	if (ev->sn_type == SH_BOOL) {
		if (cur == NULL)
			mg_set(m, coll, ek, ev);
		return;
	}
	if (cur == NULL || cur->pr_val->sn_type != SH_OBJECT) {
		mg_set(m, coll, ek, ev);
		return;
	}
	for (i = 0; i < ev->sn_obj.ob_n; i++) {
		f = &ev->sn_obj.ob_v[i];
		have = sh_obj_find(cur->pr_val, f->pr_key);
		was = sh_path_push(&m->mg_path, f->pr_key);
		if (have != NULL && rk->rk_coll != SH_C_DATASETS &&
		    !mg_default(m, have->pr_val)) {
			sh_err_at(m->mg_err, m->mg_l->sl_file, &m->mg_path,
			    "already defined in %s", have->pr_val->sn_from);
		} else {
			if (have != NULL && m->mg_warn != NULL &&
			    !sh_node_equal(have->pr_val, f->pr_val))
				sh_err_at(m->mg_warn, m->mg_l->sl_file,
				    &m->mg_path, "%s overrides %s from %s",
				    mg_said(f->pr_val), mg_said(have->pr_val),
				    have->pr_val->sn_from);
			mg_set(m, cur->pr_val, f->pr_key, f->pr_val);
		}
		sh_path_pop(&m->mg_path, was);
	}
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

/* V onto the list KEY of the merged recipe, made where it is not yet. */
static void
mg_append(struct mg *m, const char *key, struct sh_node *v)
{
	struct sh_node *list;

	if (v == NULL) {
		m->mg_nomem = 1;
		return;
	}
	list = sh_obj_get(m->mg_r->sr_merged, key);
	if (list == NULL) {
		if ((list = sh_node_new(SH_ARRAY, m->mg_l->sl_file)) == NULL ||
		    sh_obj_set(m->mg_r->sr_merged, key, list,
		    m->mg_l->sl_file) != 0) {
			sh_node_free(v);
			m->mg_nomem = 1;
			return;
		}
	}
	if (sh_arr_push(list, v) != 0)
		m->mg_nomem = 1;
}

/* The object KEY of OBJ, made there if it is not yet. */
static struct sh_node *
mg_obj(struct mg *m, struct sh_node *obj, const char *key)
{
	struct sh_node *o;

	if ((o = sh_obj_get(obj, key)) != NULL)
		return (o);
	if ((o = sh_node_new(SH_OBJECT, m->mg_l->sl_file)) == NULL ||
	    sh_obj_set(obj, key, o, m->mg_l->sl_file) != 0) {
		m->mg_nomem = 1;
		return (NULL);
	}
	return (o);
}

/*
 * A piece of text (buildopts, jailconf, kernconf's lines) from this
 * layer onto the key KEY of OBJ: an object of its pieces, each keyed
 * by the file that gives it, lower files first, a file giving one.
 */
static void
mg_text(struct mg *m, struct sh_node *obj, const char *key,
    const struct sh_node *v)
{
	struct sh_node *text;

	if ((text = mg_obj(m, obj, key)) != NULL)
		mg_set(m, text, m->mg_l->sl_file, v);
}

/*
 * kernconf, merged as { file; lines { FILE = TEXT; ... } }: lines, a
 * heredoc, stack, a piece from each file; a string is the config they
 * are added to, given by one file of the stack, the default's being
 * overridden with a warning.
 */
static void
mg_kernconf(struct mg *m, const struct sh_node *v)
{
	struct sh_node *kc;
	const struct sh_pair *have;

	if ((kc = mg_obj(m, m->mg_r->sr_merged, "kernconf")) == NULL)
		return;
	if (v->sn_block) {
		mg_text(m, kc, "lines", v);
		return;
	}
	have = sh_obj_find(kc, "file");
	if (have != NULL && !mg_default(m, have->pr_val)) {
		sh_err_at(m->mg_err, m->mg_l->sl_file, &m->mg_path,
		    "already defined in %s", have->pr_val->sn_from);
		return;
	}
	if (have != NULL && m->mg_warn != NULL &&
	    !sh_node_equal(have->pr_val, v))
		sh_err_at(m->mg_warn, m->mg_l->sl_file, &m->mg_path,
		    "%s overrides %s from %s", mg_said(v),
		    mg_said(have->pr_val), have->pr_val->sn_from);
	mg_set(m, kc, "file", v);
}

/* One layer into the merged recipe. */
static void
mg_layer(struct mg *m)
{
	const struct sh_node *tree = m->mg_l->sl_tree;
	const struct sh_rkey *rk;
	const struct sh_pair *pr, *have;
	const char *into;
	size_t i, j, was;

	for (i = 0; i < tree->sn_obj.ob_n; i++) {
		pr = &tree->sn_obj.ob_v[i];
		/*
		 * The default gives any kind's keys and shtetl.conf's
		 * settings besides; a recipe takes the keys of its kind.
		 */
		if ((rk = sh_rkey(pr->pr_key)) == NULL ||
		    (rk->rk_kinds & m->mg_r->sr_kind) == 0)
			continue;
		into = rk->rk_name;
		was = sh_path_push(&m->mg_path, rk->rk_name);
		switch (rk->rk_merge) {
		case SH_M_LAYERS:
			break;
		case SH_M_COLL:
			mg_coll(m, rk, pr->pr_val);
			break;
		case SH_M_ALL:
			/* one constraint, or a list of them */
			if (pr->pr_val->sn_type != SH_ARRAY) {
				mg_append(m, into, sh_node_copy(pr->pr_val));
				break;
			}
			for (j = 0; j < pr->pr_val->sn_arr.ar_n; j++)
				mg_append(m, into,
				    sh_node_copy(pr->pr_val->sn_arr.ar_v[j]));
			break;
		case SH_M_TEXT:
			mg_text(m, m->mg_r->sr_merged, rk->rk_name,
			    pr->pr_val);
			break;
		case SH_M_KERNCONF:
			mg_kernconf(m, pr->pr_val);
			break;
		case SH_M_AGREE:
			have = sh_obj_find(m->mg_r->sr_merged, rk->rk_name);
			if (have != NULL && mg_default(m, have->pr_val) &&
			    m->mg_warn != NULL &&
			    !sh_node_equal(have->pr_val, pr->pr_val))
				sh_err_at(m->mg_warn, m->mg_l->sl_file,
				    &m->mg_path, "%s overrides %s from %s",
				    mg_said(pr->pr_val), mg_said(have->pr_val),
				    have->pr_val->sn_from);
			if (have == NULL || mg_default(m, have->pr_val))
				mg_set(m, m->mg_r->sr_merged, rk->rk_name,
				    pr->pr_val);
			else if (!sh_node_equal(have->pr_val, pr->pr_val))
				sh_err_at(m->mg_err, m->mg_l->sl_file,
				    &m->mg_path, "%s conflicts with %s in %s",
				    mg_said(pr->pr_val), mg_said(have->pr_val),
				    have->pr_val->sn_from);
			break;
		}
		sh_path_pop(&m->mg_path, was);
	}
}

/*
 * Every ver must hold together (ruled 2026-10-12): the ranges they
 * allow must meet. Refused, naming the two that do not.
 */
static void
ck_ver(const struct sh_recipe *r, struct sh_err *e)
{
	const struct sh_node *ver, *c, *low, *high;
	long lo, hi, maxlo, minhi;
	size_t i;

	if ((ver = sh_obj_get(r->sr_merged, "ver")) == NULL)
		return;
	maxlo = SH_VER_LOW;
	minhi = SH_VER_HIGH;
	low = high = NULL;
	for (i = 0; i < ver->sn_arr.ar_n; i++) {
		c = ver->sn_arr.ar_v[i];
		sh_ver_range(c->sn_str, &lo, &hi);
		if (lo > maxlo) {
			maxlo = lo;
			low = c;
		}
		if (hi < minhi) {
			minhi = hi;
			high = c;
		}
	}
	if (low != NULL && high != NULL && maxlo > minhi)
		sh_errf(e, "%s: ver: %s in %s conflicts with %s in %s",
		    r->sr_file, low->sn_str, low->sn_from, high->sn_str,
		    high->sn_from);
}

/*
 * The checks only the merged recipe can answer: what a kind needs,
 * entries made whole across layers, and what the configuration root
 * must hold.
 */
static void
ck_merged(const struct sh_recipe *r, const char *root, struct sh_err *e)
{
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
		sh_errf(e, "%s: missing src_branch", r->sr_file);
	/* kernconf's file: a name is the src tree's, a path zconfig's */
	if ((v = sh_obj_get(sh_obj_get(merged, "kernconf"), "file")) !=
	    NULL && strchr(v->sn_str, '/') != NULL &&
	    sh_root_kind(root, v->sn_str) != 'f') {
		(void) sh_path_push(&path, "kernconf");
		sh_err_at(e, v->sn_from, &path, "%s: no such file",
		    v->sn_str);
		sh_path_pop(&path, 0);
	}
	ck_ver(r, e);
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
			    "directory", sub);
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
			sh_err_at(e, pr->pr_from, &path, "requires itself");
		else if (sh_root_kind(root, rel) != 'f')
			sh_err_at(e, pr->pr_from, &path, "no such container");
		sh_path_pop(&path, 0);
	}
}

int
sh_recipe_resolve(const char *root, const char *target,
    struct sh_recipe *r, struct sh_err *e, struct sh_err *w)
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
		sh_errf(e, "%s: no such file", r->sr_file);
		return (-1);
	}
	(void) memset(&x, 0, sizeof (x));
	x.ld_root = root;
	x.ld_r = r;
	x.ld_err = e;
	x.ld_default = 1;
	if (ld_load(&x, cascade_default, NULL, NULL, 1) != 0)
		goto nomem;
	x.ld_default = 0;
	if (ld_load(&x, r->sr_file, NULL, NULL, 0) != 0)
		goto nomem;
	if (x.ld_failed)
		return (-1);

	if ((r->sr_merged = sh_node_new(SH_OBJECT, r->sr_file)) == NULL)
		goto nomem;
	(void) memset(&m, 0, sizeof (m));
	m.mg_r = r;
	m.mg_err = e;
	m.mg_warn = w;
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
