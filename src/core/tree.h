/*
 * The format-free tree (planning.md 5.1): what the parse hands the
 * rest of shtetl, and what the cascade merges into. Nothing outside
 * parse.c knows the file was UCL.
 *
 * Every node and every key carries its layer: the file it came from
 * (or "(built-in)"), as a pointer the owner of the tree keeps alive.
 * A parsed tree's layer is its file throughout; in a merged tree a
 * key's pr_from is the layer that added it and its value's sn_from
 * the layer that last set it, which is what config show prints and
 * what the cascade reads to tell the default's values from a
 * recipe's.
 */

#ifndef	SH_TREE_H
#define	SH_TREE_H

#include <stddef.h>
#include <stdint.h>

enum sh_type {
	SH_NULL,
	SH_BOOL,
	SH_INT,
	SH_NUM,		/* a float, or a time UCL read as seconds */
	SH_STR,
	SH_ARRAY,
	SH_OBJECT
};

struct sh_node;

struct sh_pair {
	char		*pr_key;
	struct sh_node	*pr_val;
	const char	*pr_from;	/* the layer that added the key */
};

struct sh_arr {
	struct sh_node	**ar_v;
	size_t		ar_n;
	size_t		ar_cap;
};

/* An object's keys stay in the order they were added. */
struct sh_obj {
	struct sh_pair	*ob_v;
	size_t		ob_n;
	size_t		ob_cap;
};

struct sh_node {
	enum sh_type	sn_type;
	const char	*sn_from;	/* the layer that set the value */
	union {
		int		snu_bool;
		int64_t		snu_int;
		double		snu_num;
		char		*snu_str;	/* no NUL inside */
		struct sh_arr	snu_arr;
		struct sh_obj	snu_obj;
	} sn_u;
};

#define	sn_bool	sn_u.snu_bool
#define	sn_int	sn_u.snu_int
#define	sn_num	sn_u.snu_num
#define	sn_str	sn_u.snu_str
#define	sn_arr	sn_u.snu_arr
#define	sn_obj	sn_u.snu_obj

/*
 * Allocation fails with NULL or -1 and nothing half done; a value
 * handed to sh_arr_push or sh_obj_set is the tree's from then on,
 * and is freed if the call fails.
 */
struct sh_node *sh_node_new(enum sh_type type, const char *from);
struct sh_node *sh_node_str(const char *s, size_t len, const char *from);
struct sh_node *sh_node_copy(const struct sh_node *n);
void sh_node_free(struct sh_node *n);
int sh_node_equal(const struct sh_node *a, const struct sh_node *b);

int sh_arr_push(struct sh_node *arr, struct sh_node *v);

struct sh_pair *sh_obj_find(const struct sh_node *obj, const char *key);
struct sh_node *sh_obj_get(const struct sh_node *obj, const char *key);
int sh_obj_set(struct sh_node *obj, const char *key, struct sh_node *v,
    const char *from);
int sh_obj_del(struct sh_node *obj, const char *key);

/* "a string", "a number", ...: for messages. */
const char *sh_type_name(enum sh_type type);

/*
 * The UCL spelling shared by messages and config show: whether a key
 * may stand bare, and the escape a quoted string takes for byte C
 * (written into BUF, at least 7 bytes; NULL when C stands for itself).
 */
int sh_key_bare(const char *key);
const char *sh_escape(unsigned char c, char *buf);

#endif	/* SH_TREE_H */
