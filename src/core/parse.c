/*
 * The parse: libucl under the rules of docs/configuration.md 2, and
 * what it built turned into the format-free tree.
 *
 * The flags, and what each is for:
 *
 * - UCL_DUPLICATE_ERROR, per chunk: a key written twice in one file
 *   is an error, never an implicit array and never a merge of two
 *   objects.
 * - UCL_PARSER_DISABLE_MACRO: in libucl 0.9.4 a macro is then a key
 *   that does not begin with a letter, refused at its '.', and
 *   variable expansion is off altogether, so $ is literal. libucl's
 *   header says such a macro is read as a comment instead, which
 *   would drop a recipe's .include without a word. So the parse
 *   does not trust either reading: sh_parse_selfcheck tries the
 *   linked library before the first file, and shtetl reads nothing
 *   with a libucl that takes a macro, expands a variable or takes a
 *   key twice.
 * - UCL_PARSER_NO_FILEVARS: libucl sets no FILENAME or CURDIR of its
 *   own. The file's name is given with ucl_parser_set_filevars so
 *   that libucl's messages name it; that registers the same two
 *   variables, which the self-check shows are never expanded.
 *
 * Times are libucl's (no UCL_PARSER_NO_TIME): an unquoted 30d
 * arrives as a number of seconds, and the schema says to quote it.
 *
 * libucl keeps no line on the objects it builds, only on an error,
 * so a later refusal names its file and key path rather than a line.
 * Keys are unique within a file, so the path names one place.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ucl.h>

#include "core/err.h"
#include "core/mem.h"
#include "core/parse.h"
#include "core/tree.h"

#define	PA_FLAGS	(UCL_PARSER_NO_FILEVARS | UCL_PARSER_DISABLE_MACRO)
#define	PA_NONE		((size_t)-1)

/* The self-check: 0 not yet run, 1 passed, -1 failed. */
static int pa_checked;

/*
 * One libucl parse of BUF under the flags above: the parser, for the
 * caller to free, and in *OKP whether libucl took the input. libucl
 * fails the first two calls only for want of memory.
 */
static struct ucl_parser *
pa_ucl(const char *buf, size_t len, const char *name, int *okp)
{
	struct ucl_parser *p;

	if ((p = ucl_parser_new(PA_FLAGS)) == NULL ||
	    !ucl_parser_set_filevars(p, name, false))
		sh_nomem();
	*okp = ucl_parser_add_chunk_full(p, (const unsigned char *)buf, len,
	    0, UCL_DUPLICATE_ERROR, UCL_PARSE_UCL);
	return (p);
}

/* The line, from 1, that byte OFF of BUF is on. */
static unsigned
pa_line(const char *buf, size_t off)
{
	unsigned line;
	size_t i;

	line = 1;
	for (i = 0; i < off; i++) {
		if (buf[i] == '\n')
			line++;
	}
	return (line);
}

/*
 * The offset of LINE (from 1) and COL (from 0) in BUF, as libucl
 * counts them, or PA_NONE if that is not a byte of that line.
 */
static size_t
pa_offset(const char *buf, size_t len, unsigned line, unsigned col)
{
	const char *nl;
	size_t i;
	unsigned l;

	i = 0;
	for (l = 1; l < line; l++) {
		nl = memchr(buf + i, '\n', len - i);
		if (nl == NULL)
			return (PA_NONE);
		i = (size_t)(nl - buf) + 1;
	}
	if (col >= len - i || memchr(buf + i, '\n', col) != NULL)
		return (PA_NONE);
	return (i + col);
}

static enum sh_type
pa_type(const ucl_object_t *o)
{
	switch (ucl_object_type(o)) {
	case UCL_OBJECT:
		return (SH_OBJECT);
	case UCL_ARRAY:
		return (SH_ARRAY);
	case UCL_INT:
		return (SH_INT);
	case UCL_FLOAT:
	case UCL_TIME:
		return (SH_NUM);
	case UCL_STRING:
		return (SH_STR);
	case UCL_BOOLEAN:
		return (SH_BOOL);
	default:
		return (SH_NULL);
	}
}

/*
 * Tries one rule on the linked libucl: TEXT parsed, and whether the
 * outcome is the one 5.3 needs. WANT_OK: taken, with key a holding
 * WANT_A; or refused, and with AT_START refused at its first byte,
 * where a macro's '.' is. 1 kept, 0 broken.
 */
static int
pa_try(const char *text, int want_ok, const char *want_a, int at_start)
{
	struct ucl_parser *p;
	ucl_object_t *top;
	const ucl_object_t *a;
	const char *s;
	int ok, kept;

	p = pa_ucl(text, strlen(text), "(self-check)", &ok);
	kept = 0;
	if (!want_ok) {
		kept = !ok && (!at_start ||
		    (ucl_parser_get_linenum(p) == 1 &&
		    ucl_parser_get_column(p) == 0));
	} else if (ok && (top = ucl_parser_get_object(p)) != NULL) {
		a = ucl_object_lookup(top, "a");
		s = a != NULL ? ucl_object_tostring(a) : NULL;
		kept = s != NULL && strcmp(s, want_a) == 0;
		ucl_object_unref(top);
	}
	ucl_parser_free(p);
	return (kept);
}

/*
 * Whether the linked libucl marks a heredoc, and only a heredoc, as
 * one: kernconf reads its lines from a heredoc and its file from a
 * string. 1 if it does, 0 if not.
 */
static int
pa_try_block(void)
{
	static const char text[] = "a = <<EOD\nx\nEOD\nb = \"x\";\n";
	struct ucl_parser *p;
	ucl_object_t *top;
	const ucl_object_t *a, *b;
	int ok, kept;

	p = pa_ucl(text, sizeof (text) - 1, "(self-check)", &ok);
	kept = 0;
	if (ok && (top = ucl_parser_get_object(p)) != NULL) {
		a = ucl_object_lookup(top, "a");
		b = ucl_object_lookup(top, "b");
		kept = a != NULL && b != NULL &&
		    (a->flags & UCL_OBJECT_MULTILINE) != 0 &&
		    (b->flags & UCL_OBJECT_MULTILINE) == 0;
		ucl_object_unref(top);
	}
	ucl_parser_free(p);
	return (kept);
}

int
sh_parse_selfcheck(struct sh_err *e)
{
	static const struct {
		const char	*t_text;
		int		t_ok;
		const char	*t_a;
		int		t_at_start;
		const char	*t_broken;
	} tries[] = {
		{ ".include \"/nonexistent/shtetl\"\n", 0, NULL, 1,
		    "macros enabled" },
		{ "a = \"$FILENAME ${CURDIR} $X\";\n", 1,
		    "$FILENAME ${CURDIR} $X", 0, "variables expanded" },
		{ "a = 1;\na = 2;\n", 0, NULL, 0, "duplicate keys accepted" }
	};
	size_t i;

	if (pa_checked != 0)
		return (pa_checked > 0 ? 0 : -1);
	for (i = 0; i < sizeof (tries) / sizeof (tries[0]); i++) {
		if (!pa_try(tries[i].t_text, tries[i].t_ok, tries[i].t_a,
		    tries[i].t_at_start)) {
			sh_errf(e, "unusable libucl: %s", tries[i].t_broken);
			pa_checked = -1;
			return (-1);
		}
	}
	if (!pa_try_block()) {
		sh_errf(e, "unusable libucl: heredocs not marked");
		pa_checked = -1;
		return (-1);
	}
	pa_checked = 1;
	return (0);
}

/*
 * What libucl built, as a tree whose every node is from NAME. PATH is
 * where O sits, for messages; DEPTH counts from 1 at the top.
 */
static struct sh_node *
pa_conv(const ucl_object_t *o, const char *name, struct sh_path *path,
    int depth, struct sh_err *e)
{
	struct sh_node *n, *c;
	const ucl_object_t *cur;
	ucl_object_iter_t it;
	const char *s;
	char *key;
	size_t len, was, i;

	if (depth > SH_PARSE_DEPTH) {
		sh_err_at(e, name, path, "nested deeper than %d",
		    SH_PARSE_DEPTH);
		return (NULL);
	}
	/*
	 * libucl's safe iterator, freed on every way out: the plain one
	 * allocates its state on the first step of an object and frees
	 * it only at the end, so a walk left early on an error leaked it.
	 */
	it = NULL;
	n = sh_node_new(pa_type(o), name);
	if ((ucl_object_type(o) == UCL_OBJECT ||
	    ucl_object_type(o) == UCL_ARRAY) &&
	    (it = ucl_object_iterate_new(o)) == NULL)
		sh_nomem();
	switch (ucl_object_type(o)) {
	case UCL_OBJECT:
		while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
			s = ucl_object_keyl(cur, &len);
			if (s == NULL || memchr(s, '\0', len) != NULL) {
				sh_err_at(e, name, path, "NUL byte in key");
				goto fail;
			}
			key = sh_calloc(1, len + 1);
			(void) memcpy(key, s, len);
			was = sh_path_push(path, key);
			/* UCL_DUPLICATE_ERROR leaves no implicit array */
			if (cur->next != NULL || sh_obj_find(n, key) != NULL) {
				sh_err_at(e, name, path, "duplicate key");
				free(key);
				goto fail;
			}
			c = pa_conv(cur, name, path, depth + 1, e);
			sh_path_pop(path, was);
			if (c == NULL) {
				free(key);
				goto fail;
			}
			sh_obj_set(n, key, c, name);
			free(key);
		}
		break;
	case UCL_ARRAY:
		i = 0;
		while ((cur = ucl_object_iterate_safe(it, true)) != NULL) {
			was = sh_path_index(path, i++);
			c = pa_conv(cur, name, path, depth + 1, e);
			sh_path_pop(path, was);
			if (c == NULL)
				goto fail;
			sh_arr_push(n, c);
		}
		break;
	case UCL_INT:
		n->sn_int = ucl_object_toint(o);
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		n->sn_num = ucl_object_todouble(o);
		break;
	case UCL_STRING:
		s = ucl_object_tolstring(o, &len);
		if (s == NULL || memchr(s, '\0', len) != NULL) {
			sh_err_at(e, name, path, "NUL byte in string");
			goto fail;
		}
		sh_node_free(n);
		n = sh_node_str(s, len, name);
		n->sn_block = (o->flags & UCL_OBJECT_MULTILINE) != 0;
		break;
	case UCL_BOOLEAN:
		n->sn_bool = ucl_object_toboolean(o);
		break;
	case UCL_NULL:
		break;
	default:
		sh_err_at(e, name, path, "unsupported value");
		goto fail;
	}
	if (it != NULL)
		ucl_object_iterate_free(it);
	return (n);
fail:
	if (it != NULL)
		ucl_object_iterate_free(it);
	sh_node_free(n);
	return (NULL);
}

struct sh_node *
sh_parse_buf(const char *buf, size_t len, const char *name, struct sh_err *e)
{
	struct ucl_parser *p;
	ucl_object_t *top;
	struct sh_node *n;
	struct sh_path path;
	const char *nul;
	size_t off;
	unsigned line;
	int ok;

	if (sh_parse_selfcheck(e) != 0)
		return (NULL);
	if (len > SH_PARSE_MAX) {
		sh_errf(e, "%s: larger than %d MiB", name,
		    SH_PARSE_MAX / (1024 * 1024));
		return (NULL);
	}
	if ((nul = memchr(buf, '\0', len)) != NULL) {
		sh_errf(e, "%s:%u: NUL byte", name,
		    pa_line(buf, (size_t)(nul - buf)));
		return (NULL);
	}
	p = pa_ucl(buf, len, name, &ok);
	if (!ok) {
		line = ucl_parser_get_linenum(p);
		off = pa_offset(buf, len, line, ucl_parser_get_column(p));
		/* libucl reports a key written twice as a merge error */
		if (off != PA_NONE && buf[off] == '.')
			sh_errf(e, "%s:%u: macros are illegal", name, line);
		else if (ucl_parser_get_error_code(p) == UCL_EMERGE)
			sh_errf(e, "%s:%u: duplicate key", name, line);
		else
			sh_errf(e, "%s:%u: syntax error", name, line);
		ucl_parser_free(p);
		return (NULL);
	}
	top = ucl_parser_get_object(p);
	sh_path_init(&path);
	if (top == NULL) {
		n = sh_node_new(SH_OBJECT, name);
	} else if (ucl_object_type(top) != UCL_OBJECT) {
		sh_errf(e, "%s: must be an object", name);
		n = NULL;
	} else {
		n = pa_conv(top, name, &path, 1, e);
	}
	if (top != NULL)
		ucl_object_unref(top);
	ucl_parser_free(p);
	return (n);
}

struct sh_node *
sh_parse_file(const char *path, const char *name, struct sh_err *e)
{
	struct sh_node *n = NULL;
	struct stat st;
	char *buf = NULL;
	ssize_t r = 0;
	size_t got, size;
	int fd;

	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) == -1 ||
	    fstat(fd, &st) == -1) {
		sh_errf(e, "%s: %s", name, strerror(errno));
		goto out;
	}
	if (!S_ISREG(st.st_mode)) {
		sh_errf(e, "%s: not a regular file", name);
		goto out;
	}
	if (st.st_size > SH_PARSE_MAX) {
		sh_errf(e, "%s: larger than %d MiB", name,
		    SH_PARSE_MAX / (1024 * 1024));
		goto out;
	}
	size = (size_t)st.st_size;
	buf = sh_calloc(1, size + 1);
	for (got = 0; got < size; got += (size_t)r) {
		r = read(fd, buf + got, size - got);
		if (r == -1 && errno == EINTR) {
			r = 0;
			continue;
		}
		if (r <= 0)
			break;
	}
	if (got < size)
		sh_errf(e, "%s: %s", name, r == -1 ? strerror(errno) :
		    "changed while being read");
	else
		n = sh_parse_buf(buf, size, name, e);
out:
	if (fd >= 0)
		(void) close(fd);
	free(buf);
	return (n);
}
