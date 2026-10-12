/*
 * The spellings of names and paths in shtetl's configuration.
 */

#include <stdlib.h>
#include <string.h>

#include "core/syntax.h"

/* ZFS_MAX_DATASET_NAME_LEN less its NUL; a name never needs more. */
#define	SYN_MAX	255

static int
syn_alnum(char c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	    (c >= '0' && c <= '9'));
}

static int
syn_digit(char c)
{
	return (c >= '0' && c <= '9');
}

/* zfs_namecheck.c, valid_char: what a dataset name may hold. */
static int
syn_zfs_char(char c)
{
	return (syn_alnum(c) || c == '-' || c == '_' || c == '.' ||
	    c == ':' || c == ' ');
}

const char *
sh_syn_name(const char *s)
{
	const char *p;

	if (*s == '\0')
		return ("empty");
	if (strlen(s) > SYN_MAX)
		return ("too long");
	if (!syn_alnum(*s))
		return ("must begin with a letter or digit");
	for (p = s; *p != '\0'; p++) {
		if (!syn_alnum(*p) && *p != '-' && *p != '_')
			return ("only letters, digits, - and _ allowed");
	}
	return (NULL);
}

/*
 * The components of S, '/' between them, each byte checked by OK and
 * refused as BADCHAR; what is wrong, or NULL.
 */
static const char *
syn_components(const char *s, int (*ok)(char), const char *badchar)
{
	const char *p, *start;

	if (*s == '\0')
		return ("empty");
	if (*s == '/')
		return ("leading /");
	start = s;
	for (p = s; ; p++) {
		if (*p != '/' && *p != '\0') {
			if (!ok(*p))
				return (badchar);
			continue;
		}
		if (p == start)
			return (*p == '\0' ? "trailing /" :
			    "empty path component");
		if ((p - start == 1 && start[0] == '.') ||
		    (p - start == 2 && start[0] == '.' && start[1] == '.'))
			return (". or .. component");
		if (*p == '\0')
			return (NULL);
		start = p + 1;
	}
}

const char *
sh_syn_dataset(const char *s)
{
	if (strlen(s) > SYN_MAX)
		return ("too long");
	return (syn_components(s, syn_zfs_char, "invalid character"));
}

const char *
sh_syn_dsname(const char *s)
{
	const char *why;

	if ((why = sh_syn_dataset(s)) != NULL)
		return (why);
	if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')))
		return ("pool name must begin with a letter");
	return (NULL);
}

const char *
sh_syn_pkg(const char *s)
{
	const char *p;

	if (*s == '\0')
		return ("empty");
	if (strlen(s) > SYN_MAX)
		return ("too long");
	if (*s == '-')
		return ("leading -");
	for (p = s; *p != '\0'; p++) {
		if (!syn_alnum(*p) && strchr("-_.+@/", *p) == NULL)
			return ("invalid character");
	}
	return (NULL);
}

const char *
sh_syn_label(const char *s)
{
	const char *p;

	if (*s == '\0')
		return ("empty");
	if (strlen(s) > SYN_MAX)
		return ("too long");
	if (!syn_alnum(*s))
		return ("must begin with a letter or digit");
	for (p = s; *p != '\0'; p++) {
		if (!syn_alnum(*p) && strchr("-_.", *p) == NULL)
			return ("only letters, digits, -, _ and . allowed");
	}
	return (NULL);
}

static int
syn_path_char(char c)
{
	return ((unsigned char)c >= 0x20 && c != 0x7f);
}

const char *
sh_syn_relpath(const char *s)
{
	if (strlen(s) > 1024)
		return ("too long");
	if (*s == '/')
		return ("absolute path");
	return (syn_components(s, syn_path_char, "control character"));
}

static int
syn_lower(char c)
{
	return ((c >= 'a' && c <= 'z') || syn_digit(c));
}

const char *
sh_syn_prop(const char *s)
{
	const char *p;

	if (*s == '\0')
		return ("empty");
	if (strlen(s) > SYN_MAX)
		return ("too long");
	if (strcmp(s, "mountpoint") == 0 || strcmp(s, "canmount") == 0 ||
	    strncmp(s, "shtetl:", 7) == 0)
		return ("reserved");
	if (strchr(s, ':') != NULL) {
		/* a user property: zfs_prop_user()'s characters */
		for (p = s; *p != '\0'; p++) {
			if (!syn_lower(*p) && strchr("_.:-", *p) == NULL)
				return ("only lowercase letters, digits, _, "
				    "., : and - allowed");
		}
		return (NULL);
	}
	if (!(*s >= 'a' && *s <= 'z'))
		return ("malformed name");
	for (p = s; *p != '\0' && *p != '@'; p++) {
		if (!syn_lower(*p) && *p != '_')
			return ("malformed name");
	}
	if (*p == '@' && p[1] == '\0')
		return ("malformed name");
	return (NULL);
}

/* Digits at *P, a number below 1000, read into *N; *P moved past. */
static int
syn_number(const char **p, long *n)
{
	const char *q = *p;

	*n = 0;
	if (!syn_digit(*q))
		return (0);
	while (syn_digit(*q) && *n < 1000)
		*n = *n * 10 + (*q++ - '0');
	if (syn_digit(*q) || *n >= 1000)
		return (0);
	*p = q;
	return (1);
}

const char *
sh_syn_branch(const char *s, struct sh_branch *b)
{
	const char *p;

	b->br_major = b->br_minor = -1;
	if (strcmp(s, "main") == 0) {
		b->br_type = SH_BR_MAIN;
		return (NULL);
	}
	if (strncmp(s, "stable/", 7) == 0) {
		b->br_type = SH_BR_STABLE;
		p = s + 7;
		if (syn_number(&p, &b->br_major) && *p == '\0')
			return (NULL);
	} else if (strncmp(s, "releng/", 7) == 0) {
		b->br_type = SH_BR_RELENG;
		p = s + 7;
		if (syn_number(&p, &b->br_major) && *p++ == '.' &&
		    syn_number(&p, &b->br_minor) && *p == '\0')
			return (NULL);
	}
	return ("not main, stable/N or releng/N.M");
}

const char *
sh_syn_ver(const char *s)
{
	static const char *const ops[] = { ">=", "<=", ">", "<", "=" };
	const char *p;
	size_t i, n;

	p = NULL;
	for (i = 0; i < sizeof (ops) / sizeof (ops[0]); i++) {
		n = strlen(ops[i]);
		if (strncmp(s, ops[i], n) == 0) {
			p = s + n;
			break;
		}
	}
	if (p == NULL)
		return ("must begin with >=, <=, >, < or =");
	if (!syn_digit(*p))
		return ("missing version");
	while (syn_digit(*p))
		p++;
	if (*p == '.') {
		p++;
		if (!syn_digit(*p))
			return ("version must be N or N.N");
		while (syn_digit(*p))
			p++;
	}
	if (*p != '\0')
		return ("version must be N or N.N");
	return (NULL);
}

void
sh_ver_range(const char *s, long *lo, long *hi)
{
	const char *p;
	long major, minor, first, last;
	int op;

	/* op: 0 =, 1 >=, 2 <=, 3 >, 4 < */
	if (strncmp(s, ">=", 2) == 0) {
		op = 1;
		p = s + 2;
	} else if (strncmp(s, "<=", 2) == 0) {
		op = 2;
		p = s + 2;
	} else if (*s == '>') {
		op = 3;
		p = s + 1;
	} else if (*s == '<') {
		op = 4;
		p = s + 1;
	} else {
		op = 0;
		p = s + 1;
	}
	major = strtol(p, (char **)NULL, 10);
	while (syn_digit(*p))
		p++;
	if (major > 999)
		major = 999;
	/* the versions S names: N.M alone, or N, every N.x */
	if (*p == '.') {
		minor = strtol(p + 1, (char **)NULL, 10);
		if (minor >= SH_VER_MINORS)
			minor = SH_VER_MINORS - 1;
		first = last = major * SH_VER_MINORS + minor;
	} else {
		first = major * SH_VER_MINORS;
		last = first + SH_VER_MINORS - 1;
	}
	*lo = SH_VER_LOW;
	*hi = SH_VER_HIGH;
	switch (op) {
	case 0:
		*lo = first;
		*hi = last;
		break;
	case 1:
		*lo = first;
		break;
	case 2:
		*hi = last;
		break;
	case 3:
		*lo = last + 1;
		break;
	default:
		*hi = first - 1;
		break;
	}
}

const char *
sh_syn_interval(const char *s)
{
	const char *p;
	int nonzero;

	nonzero = 0;
	for (p = s; syn_digit(*p); p++) {
		if (*p != '0')
			nonzero = 1;
	}
	if (p == s || p - s > 9 || !nonzero || p[0] == '\0' ||
	    strchr("ymwdHMS", p[0]) == NULL || p[1] != '\0')
		return ("must be a count above 0 and one of y, m, w, d, H, "
		    "M, S");
	return (NULL);
}

const char *
sh_syn_location(const char *s, enum sh_loc *kind)
{
	const char *p, *slash, *sep;

	if (*s == '\0')
		return ("empty");
	if (strlen(s) > 1024)
		return ("too long");
	for (p = s; *p != '\0'; p++) {
		if ((unsigned char)*p <= 0x20 || *p == 0x7f || *p == '"')
			return ("space, quote or control character");
	}
	if ((sep = strstr(s, "://")) != NULL) {
		if (!((*s >= 'a' && *s <= 'z')))
			return ("scheme must begin with a letter");
		for (p = s; p < sep; p++) {
			if (!syn_lower(*p) && strchr("+.-", *p) == NULL)
				return ("invalid scheme");
		}
		if (sep[3] == '\0')
			return ("missing path");
		*kind = SH_LOC_URL;
		return (NULL);
	}
	slash = strchr(s, '/');
	if (slash == NULL)
		slash = s + strlen(s);
	for (p = s; p < slash; p++) {
		if (*p == '@' || *p == ':')
			return ("scp-style address: use ssh://");
	}
	if (memchr(s, '.', (size_t)(slash - s)) == NULL) {
		*kind = SH_LOC_DIR;
		return (sh_syn_relpath(s));
	}
	for (p = s; p < slash; p++) {
		if (!syn_alnum(*p) && *p != '.' && *p != '-')
			return ("invalid host");
	}
	if (*slash != '/' || slash[1] == '\0')
		return ("missing path after host");
	*kind = SH_LOC_HOST;
	return (NULL);
}
