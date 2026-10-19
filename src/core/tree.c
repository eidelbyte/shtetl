/*
 * The format-free tree: nodes, ordered objects, copies and equality.
 * Objects are small (a recipe has a dozen keys, a collection a few
 * dozen), so a key is found by a walk, and order costs nothing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/mem.h"
#include "core/tree.h"

#define	TREE_CAP_MIN	8

struct sh_node *
sh_node_new(enum sh_type type, const char *from)
{
	struct sh_node *n;

	n = sh_calloc(1, sizeof (*n));
	n->sn_type = type;
	n->sn_from = from;
	return (n);
}

struct sh_node *
sh_node_str(const char *s, size_t len, const char *from)
{
	struct sh_node *n;

	n = sh_node_new(SH_STR, from);
	n->sn_str = sh_calloc(1, len + 1);
	(void) memcpy(n->sn_str, s, len);
	return (n);
}

void
sh_node_free(struct sh_node *n)
{
	size_t i;

	if (n == NULL)
		return;
	switch (n->sn_type) {
	case SH_STR:
		free(n->sn_str);
		break;
	case SH_ARRAY:
		for (i = 0; i < n->sn_arr.ar_n; i++)
			sh_node_free(n->sn_arr.ar_v[i]);
		free(n->sn_arr.ar_v);
		break;
	case SH_OBJECT:
		for (i = 0; i < n->sn_obj.ob_n; i++) {
			free(n->sn_obj.ob_v[i].pr_key);
			sh_node_free(n->sn_obj.ob_v[i].pr_val);
		}
		free(n->sn_obj.ob_v);
		break;
	default:
		break;
	}
	free(n);
}

struct sh_node *
sh_node_copy(const struct sh_node *n)
{
	struct sh_node *c;
	const struct sh_pair *pr;
	size_t i;

	if (n->sn_type == SH_STR) {
		c = sh_node_str(n->sn_str, strlen(n->sn_str), n->sn_from);
		c->sn_block = n->sn_block;
		return (c);
	}
	c = sh_node_new(n->sn_type, n->sn_from);
	switch (n->sn_type) {
	case SH_ARRAY:
		for (i = 0; i < n->sn_arr.ar_n; i++)
			sh_arr_push(c, sh_node_copy(n->sn_arr.ar_v[i]));
		break;
	case SH_OBJECT:
		for (i = 0; i < n->sn_obj.ob_n; i++) {
			pr = &n->sn_obj.ob_v[i];
			sh_obj_set(c, pr->pr_key, sh_node_copy(pr->pr_val),
			    pr->pr_from);
		}
		break;
	default:
		c->sn_u = n->sn_u;
		break;
	}
	return (c);
}

/*
 * The same value: type, scalar, elements in order, keys in order.
 * Layers are not compared; this is what a round trip must keep.
 */
int
sh_node_equal(const struct sh_node *a, const struct sh_node *b)
{
	size_t i;

	if (a->sn_type != b->sn_type)
		return (0);
	switch (a->sn_type) {
	case SH_NULL:
		return (1);
	case SH_BOOL:
		return (!a->sn_bool == !b->sn_bool);
	case SH_INT:
		return (a->sn_int == b->sn_int);
	case SH_NUM:
		return (a->sn_num == b->sn_num);
	case SH_STR:
		return (strcmp(a->sn_str, b->sn_str) == 0);
	case SH_ARRAY:
		if (a->sn_arr.ar_n != b->sn_arr.ar_n)
			return (0);
		for (i = 0; i < a->sn_arr.ar_n; i++) {
			if (!sh_node_equal(a->sn_arr.ar_v[i],
			    b->sn_arr.ar_v[i]))
				return (0);
		}
		return (1);
	case SH_OBJECT:
		if (a->sn_obj.ob_n != b->sn_obj.ob_n)
			return (0);
		for (i = 0; i < a->sn_obj.ob_n; i++) {
			if (strcmp(a->sn_obj.ob_v[i].pr_key,
			    b->sn_obj.ob_v[i].pr_key) != 0 ||
			    !sh_node_equal(a->sn_obj.ob_v[i].pr_val,
			    b->sn_obj.ob_v[i].pr_val))
				return (0);
		}
		return (1);
	}
	return (0);
}

/* Room for one more of SIZE bytes in V, which holds N of *CAP. */
static void *
tree_grow(void *v, size_t *cap, size_t n, size_t size)
{
	if (n < *cap)
		return (v);
	*cap = *cap < TREE_CAP_MIN ? TREE_CAP_MIN : *cap * 2;
	return (sh_realloc(v, *cap * size));
}

void
sh_arr_push(struct sh_node *arr, struct sh_node *v)
{
	struct sh_arr *a = &arr->sn_arr;

	a->ar_v = tree_grow(a->ar_v, &a->ar_cap, a->ar_n, sizeof (*a->ar_v));
	a->ar_v[a->ar_n++] = v;
}

struct sh_pair *
sh_obj_find(const struct sh_node *obj, const char *key)
{
	size_t i;

	if (obj == NULL || obj->sn_type != SH_OBJECT)
		return (NULL);
	for (i = 0; i < obj->sn_obj.ob_n; i++) {
		if (strcmp(obj->sn_obj.ob_v[i].pr_key, key) == 0)
			return (&obj->sn_obj.ob_v[i]);
	}
	return (NULL);
}

struct sh_node *
sh_obj_get(const struct sh_node *obj, const char *key)
{
	struct sh_pair *pr;

	pr = sh_obj_find(obj, key);
	return (pr != NULL ? pr->pr_val : NULL);
}

/*
 * KEY = V. A key already there keeps its place and the layer that
 * added it, and its old value is freed; a new key goes last, added by
 * FROM.
 */
void
sh_obj_set(struct sh_node *obj, const char *key, struct sh_node *v,
    const char *from)
{
	struct sh_obj *o = &obj->sn_obj;
	struct sh_pair *pr;

	if ((pr = sh_obj_find(obj, key)) != NULL) {
		sh_node_free(pr->pr_val);
		pr->pr_val = v;
		return;
	}
	o->ob_v = tree_grow(o->ob_v, &o->ob_cap, o->ob_n, sizeof (*o->ob_v));
	pr = &o->ob_v[o->ob_n++];
	pr->pr_key = sh_strdup(key);
	pr->pr_val = v;
	pr->pr_from = from;
}

/* Removes KEY, keeping the order of the rest: 1 if it was there. */
int
sh_obj_del(struct sh_node *obj, const char *key)
{
	struct sh_obj *o = &obj->sn_obj;
	struct sh_pair *pr;
	size_t i;

	if ((pr = sh_obj_find(obj, key)) == NULL)
		return (0);
	i = (size_t)(pr - o->ob_v);
	free(pr->pr_key);
	sh_node_free(pr->pr_val);
	(void) memmove(pr, pr + 1, (o->ob_n - i - 1) * sizeof (*pr));
	o->ob_n--;
	return (1);
}

const char *
sh_type_name(enum sh_type type)
{
	switch (type) {
	case SH_NULL:
		return ("null");
	case SH_BOOL:
		return ("a boolean");
	case SH_INT:
		return ("an integer");
	case SH_NUM:
		return ("a number");
	case SH_STR:
		return ("a string");
	case SH_ARRAY:
		return ("a list");
	case SH_OBJECT:
		return ("an object");
	}
	return ("a value");
}

/*
 * A key that UCL reads bare: a letter, then letters, digits, '_' and
 * '-'. libucl takes more than this bare ('.', for one), but a key
 * outside it is quoted, which reads the same to every parser.
 */
int
sh_key_bare(const char *key)
{
	const char *k;

	if (!((*key >= 'a' && *key <= 'z') || (*key >= 'A' && *key <= 'Z')))
		return (0);
	for (k = key + 1; *k != '\0'; k++) {
		if (!((*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') ||
		    (*k >= '0' && *k <= '9') || *k == '_' || *k == '-'))
			return (0);
	}
	return (1);
}

/*
 * The escapes libucl's quoted strings take (ucl_unescape_json_string):
 * the JSON set. Bytes from 0x80 up stand for themselves, so UTF-8 in
 * a value passes through.
 */
const char *
sh_escape(unsigned char c, char *buf)
{
	switch (c) {
	case '"':
		return ("\\\"");
	case '\\':
		return ("\\\\");
	case '\n':
		return ("\\n");
	case '\t':
		return ("\\t");
	case '\r':
		return ("\\r");
	case '\b':
		return ("\\b");
	case '\f':
		return ("\\f");
	default:
		break;
	}
	if (c < 0x20 || c == 0x7f) {
		(void) snprintf(buf, 7, "\\u%04x", (unsigned)c);
		return (buf);
	}
	return (NULL);
}
