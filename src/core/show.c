/*
 * show. The text is UCL that libucl parses back to the tree it
 * was printed from (tests/check_show.c holds it to that), with the
 * layers as comments:
 *
 *     sudo = true;  # shtetl.conf
 *     compression = "zstd";  # containers/x.conf, over common/f.conf
 *
 * The first names the layer that set the value; "over" names the one
 * that added the key, when that was another. A collection's entries
 * carry their own comments and the collection none, since many
 * layers fill it; so does ver, each of its values on a line of its
 * own that ends with its layer. Not on a line above: libucl 0.9.4
 * reads a comment between [ and the first value as a part of that
 * value, quotes and all (tried; a comment after a value and its comma
 * it reads as a comment). A text that stacks (buildopts, jailconf,
 * kernconf's lines, a commands block) is an object of pieces keyed by
 * their files, which says their layers itself.
 *
 * A string with a newline in it is printed as a heredoc, by libucl
 * 0.9.4's rules (ucl_parse_multiline_string): the opening <<TERM ends
 * its line, so the comment goes on the line above; the value ends at
 * the first line that begins with TERM and then a newline, ';' or
 * ',', so TERM is chosen to begin none of the value's lines; and the
 * newline before TERM is not part of the value, so one that ends in
 * a newline still comes back whole. A heredoc may not stand in an
 * array, and an empty one does not parse: those are quoted.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cascade.h"
#include "core/schema.h"
#include "core/show.h"
#include "core/site.h"
#include "core/tree.h"

/* The longest heredoc terminator tried: EOD, EODX, EODXX, ... */
#define	SW_TERM_MAX	16

struct sw {
	FILE	*sw_out;
	int	sw_entries;	/* the depth whose objects are entries */
};

static void
sw_indent(struct sw *w, int depth)
{
	int i;

	for (i = 0; i < depth; i++)
		(void) fputs("    ", w->sw_out);
}

static void
sw_quoted(struct sw *w, const char *s)
{
	char esc[8];
	const char *q;

	(void) fputc('"', w->sw_out);
	for (; *s != '\0'; s++) {
		if ((q = sh_escape((unsigned char)*s, esc)) != NULL)
			(void) fputs(q, w->sw_out);
		else
			(void) fputc(*s, w->sw_out);
	}
	(void) fputc('"', w->sw_out);
}

static void
sw_key(struct sw *w, const char *key)
{
	if (sh_key_bare(key))
		(void) fputs(key, w->sw_out);
	else
		sw_quoted(w, key);
}

/* Whether a line of S begins with TERM as a terminator would. */
static int
sw_term_clash(const char *s, const char *term)
{
	size_t n = strlen(term);
	const char *line;

	for (line = s; line != NULL; line = strchr(line, '\n')) {
		if (*line == '\n')
			line++;
		if (strncmp(line, term, n) == 0 && (line[n] == '\0' ||
		    line[n] == '\n' || line[n] == ';' || line[n] == ','))
			return (1);
	}
	return (0);
}

/* A terminator for S as a heredoc, into TERM; 0 if S is quoted. */
static int
sw_heredoc(const char *s, char *term)
{
	size_t n;

	if (*s == '\0' || strchr(s, '\n') == NULL)
		return (0);
	(void) strcpy(term, "EOD");
	for (n = 3; sw_term_clash(s, term); n++) {
		if (n == SW_TERM_MAX)
			return (0);
		term[n] = 'X';
		term[n + 1] = '\0';
	}
	return (1);
}

/* A scalar's value, as UCL reads it back. */
static void
sw_scalar(struct sw *w, const struct sh_node *v)
{
	char buf[64];

	switch (v->sn_type) {
	case SH_NULL:
		(void) fputs("null", w->sw_out);
		break;
	case SH_BOOL:
		(void) fputs(v->sn_bool ? "true" : "false", w->sw_out);
		break;
	case SH_INT:
		(void) fprintf(w->sw_out, "%lld", (long long)v->sn_int);
		break;
	case SH_NUM:
		(void) snprintf(buf, sizeof (buf), "%.17g", v->sn_num);
		(void) fputs(buf, w->sw_out);
		if (strpbrk(buf, ".eEn") == NULL)
			(void) fputs(".0", w->sw_out);
		break;
	case SH_STR:
		sw_quoted(w, v->sn_str);
		break;
	default:
		break;
	}
}

/*
 * LEAD, then "# SET", or "# SET, over ADDED"; nothing when the key
 * names the layer itself (a commands block's pieces).
 */
static void
sw_from(struct sw *w, const struct sh_pair *pr, const char *lead)
{
	const char *set = pr->pr_val->sn_from;

	if (strcmp(pr->pr_key, set) == 0)
		return;

	(void) fprintf(w->sw_out, "%s# %s", lead, set);
	if (pr->pr_from != NULL && strcmp(pr->pr_from, set) != 0)
		(void) fprintf(w->sw_out, ", over %s", pr->pr_from);
}

static void sw_body(struct sw *w, const struct sh_node *obj, int depth);

/* An array element, or an array: never a heredoc. */
static void
sw_element(struct sw *w, const struct sh_node *v, int depth)
{
	size_t i;

	if (v->sn_type == SH_OBJECT) {
		(void) fputs("{\n", w->sw_out);
		sw_body(w, v, depth + 1);
		sw_indent(w, depth);
		(void) fputc('}', w->sw_out);
	} else if (v->sn_type == SH_ARRAY) {
		(void) fputs("[\n", w->sw_out);
		for (i = 0; i < v->sn_arr.ar_n; i++) {
			sw_indent(w, depth + 1);
			sw_element(w, v->sn_arr.ar_v[i], depth + 1);
			(void) fputs(i + 1 < v->sn_arr.ar_n ? ",\n" : "\n",
			    w->sw_out);
		}
		sw_indent(w, depth);
		(void) fputc(']', w->sw_out);
	} else {
		sw_scalar(w, v);
	}
}

/* A list of values from several layers, each line ending in its layer. */
static void
sw_list(struct sw *w, const struct sh_pair *pr, int depth)
{
	const struct sh_node *v = pr->pr_val, *el;
	size_t i;

	sw_indent(w, depth);
	sw_key(w, pr->pr_key);
	(void) fputs(" = [\n", w->sw_out);
	for (i = 0; i < v->sn_arr.ar_n; i++) {
		el = v->sn_arr.ar_v[i];
		sw_indent(w, depth + 1);
		sw_element(w, el, depth + 1);
		(void) fprintf(w->sw_out, "%s  # %s\n",
		    i + 1 < v->sn_arr.ar_n ? "," : "", el->sn_from);
	}
	sw_indent(w, depth);
	(void) fputs("]\n", w->sw_out);
}

static void
sw_pair(struct sw *w, const struct sh_pair *pr, int depth)
{
	const struct sh_node *v = pr->pr_val;
	char term[SW_TERM_MAX + 2];
	int entry = depth == w->sw_entries;

	if (v->sn_type == SH_ARRAY && v->sn_arr.ar_n > 0) {
		sw_list(w, pr, depth);
		return;
	}
	if (v->sn_type == SH_STR && sw_heredoc(v->sn_str, term)) {
		if (strcmp(pr->pr_key, v->sn_from) != 0) {
			sw_indent(w, depth);
			sw_from(w, pr, "");
			(void) fputc('\n', w->sw_out);
		}
		sw_indent(w, depth);
		sw_key(w, pr->pr_key);
		(void) fprintf(w->sw_out, " = <<%s\n%s\n%s\n", term, v->sn_str,
		    term);
		return;
	}
	sw_indent(w, depth);
	sw_key(w, pr->pr_key);
	if (v->sn_type == SH_OBJECT) {
		(void) fputs(v->sn_obj.ob_n == 0 ? " {}" : " {", w->sw_out);
		if (entry)
			sw_from(w, pr, "  ");
		(void) fputc('\n', w->sw_out);
		if (v->sn_obj.ob_n == 0)
			return;
		sw_body(w, v, depth + 1);
		sw_indent(w, depth);
		(void) fputs("}\n", w->sw_out);
		return;
	}
	(void) fputs(" = ", w->sw_out);
	sw_element(w, v, depth);
	(void) fputc(';', w->sw_out);
	sw_from(w, pr, "  ");
	(void) fputc('\n', w->sw_out);
}

static void
sw_body(struct sw *w, const struct sh_node *obj, int depth)
{
	size_t i;

	for (i = 0; i < obj->sn_obj.ob_n; i++)
		sw_pair(w, &obj->sn_obj.ob_v[i], depth);
}

void
sh_show_recipe(FILE *out, const struct sh_recipe *r)
{
	const struct sh_pair *pr;
	const struct sh_rkey *rk;
	struct sw w;
	size_t i;

	w.sw_out = out;
	(void) fprintf(out, "# %s: a %s's recipe, its layers merged\n",
	    r->sr_file, sh_kind_name(r->sr_kind));
	(void) fprintf(out, "# layers, nearest last:");
	for (i = 0; i < r->sr_nlayers; i++)
		(void) fprintf(out, "%s %s", i > 0 ? "," : "",
		    r->sr_layers[i].sl_file);
	(void) fputc('\n', out);
	/* a collection's entries carry their layers, nothing else's */
	for (i = 0; i < r->sr_merged->sn_obj.ob_n; i++) {
		pr = &r->sr_merged->sn_obj.ob_v[i];
		rk = sh_rkey(pr->pr_key);
		w.sw_entries = rk != NULL && rk->rk_merge == SH_M_COLL ? 1 : -1;
		sw_pair(&w, pr, 0);
	}
}

void
sh_show_site(FILE *out, const struct sh_site *s)
{
	struct sw w;

	w.sw_out = out;
	w.sw_entries = -1;
	if (s->ss_has_conf)
		(void) fprintf(out, "# the site's settings: %s over the "
		    "built-in ones\n", sh_site_conf);
	else
		(void) fprintf(out, "# the site's settings: the built-in ones; "
		    "there is no %s\n", sh_site_conf);
	sw_body(&w, s->ss_merged, 0);
}

int
sh_show(const char *root, const char *recipe, FILE *out, FILE *err)
{
	struct sh_recipe r;
	struct sh_site s;
	struct sh_err e, w;
	const char **repos;
	int rc = 0;

	sh_err_init(&e, err, "shtetl: ");
	sh_err_init(&w, err, "shtetl: warning: ");
	if (sh_site_load(root, &s, &e) != 0)
		return (1);
	if (recipe == NULL) {
		sh_show_site(out, &s);
	} else {
		repos = sh_site_repos(&s);
		rc = sh_recipe_resolve(root, repos, recipe, &r, &e, &w) != 0;
		if (rc == 0)
			sh_show_recipe(out, &r);
		sh_recipe_free(&r);
		free(repos);
	}
	sh_site_free(&s);
	return (rc);
}
