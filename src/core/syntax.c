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
		return ("longer than 255 bytes");
	if (!syn_alnum(*s))
		return ("it must begin with a letter or a digit");
	for (p = s; *p != '\0'; p++) {
		if (!syn_alnum(*p) && *p != '-' && *p != '_')
			return ("it takes only letters, digits, '-' and '_'");
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
		return ("it begins with '/'");
	start = s;
	for (p = s; ; p++) {
		if (*p != '/' && *p != '\0') {
			if (!ok(*p))
				return (badchar);
			continue;
		}
		if (p == start)
			return (*p == '\0' ? "it ends with '/'" :
			    "an empty component");
		if ((p - start == 1 && start[0] == '.') ||
		    (p - start == 2 && start[0] == '.' && start[1] == '.'))
			return ("a '.' or '..' component");
		if (*p == '\0')
			return (NULL);
		start = p + 1;
	}
}

const char *
sh_syn_dataset(const char *s)
{
	if (strlen(s) > SYN_MAX)
		return ("longer than 255 bytes");
	return (syn_components(s, syn_zfs_char,
	    "a character ZFS names do not take"));
}

const char *
sh_syn_dsname(const char *s)
{
	const char *why;

	if ((why = sh_syn_dataset(s)) != NULL)
		return (why);
	if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')))
		return ("a pool's name begins with a letter");
	return (NULL);
}

const char *
sh_syn_pkg(const char *s)
{
	const char *p;

	if (*s == '\0')
		return ("empty");
	if (strlen(s) > SYN_MAX)
		return ("longer than 255 bytes");
	if (*s == '-')
		return ("it begins with '-'");
	for (p = s; *p != '\0'; p++) {
		if (!syn_alnum(*p) && strchr("-_.+@/", *p) == NULL)
			return ("a character a package's name does not take");
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
		return ("longer than 255 bytes");
	if (!syn_alnum(*s))
		return ("it must begin with a letter or a digit");
	for (p = s; *p != '\0'; p++) {
		if (!syn_alnum(*p) && strchr("-_.", *p) == NULL)
			return ("it takes only letters, digits, '-', '_' and "
			    "'.'");
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
		return ("longer than 1024 bytes");
	if (*s == '/')
		return ("it is absolute: name a path in the configuration "
		    "root");
	return (syn_components(s, syn_path_char, "a control character"));
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
		return ("longer than 255 bytes");
	if (strcmp(s, "mountpoint") == 0 || strcmp(s, "canmount") == 0 ||
	    strncmp(s, "shtetl:", 7) == 0)
		return ("shtetl sets it itself");
	if (strchr(s, ':') != NULL) {
		/* a user property: zfs_prop_user()'s characters */
		for (p = s; *p != '\0'; p++) {
			if (!syn_lower(*p) && strchr("_.:-", *p) == NULL)
				return ("a user property takes only lowercase "
				    "letters, digits, '_', '.', ':' and '-'");
		}
		return (NULL);
	}
	if (!(*s >= 'a' && *s <= 'z'))
		return ("not a ZFS property's name");
	for (p = s; *p != '\0' && *p != '@'; p++) {
		if (!syn_lower(*p) && *p != '_')
			return ("not a ZFS property's name");
	}
	if (*p == '@' && p[1] == '\0')
		return ("not a ZFS property's name");
	return (NULL);
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
		return ("it begins with none of >=, <=, >, < and =");
	if (!syn_digit(*p))
		return ("no version after the comparison: >=15, =15.1");
	while (syn_digit(*p))
		p++;
	if (*p == '.') {
		p++;
		if (!syn_digit(*p))
			return ("a version is N or N.N: >=15, =15.1");
		while (syn_digit(*p))
			p++;
	}
	if (*p != '\0')
		return ("a version is N or N.N: >=15, =15.1");
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
		return ("an interval is a count above 0 and one of y, m, w, "
		    "d, H, M, S: \"7d\", \"6m\", \"1y\"");
	return (NULL);
}

const char *
sh_syn_location(const char *s, enum sh_loc *kind)
{
	const char *p, *slash, *sep;

	if (*s == '\0')
		return ("empty");
	if (strlen(s) > 1024)
		return ("longer than 1024 bytes");
	for (p = s; *p != '\0'; p++) {
		if ((unsigned char)*p <= 0x20 || *p == 0x7f || *p == '"')
			return ("a space, a quote or a control character");
	}
	if ((sep = strstr(s, "://")) != NULL) {
		if (!((*s >= 'a' && *s <= 'z')))
			return ("a URL's scheme begins with a letter");
		for (p = s; p < sep; p++) {
			if (!syn_lower(*p) && strchr("+.-", *p) == NULL)
				return ("not a URL's scheme");
		}
		if (sep[3] == '\0')
			return ("nothing after the scheme");
		*kind = SH_LOC_URL;
		return (NULL);
	}
	slash = strchr(s, '/');
	if (slash == NULL)
		slash = s + strlen(s);
	for (p = s; p < slash; p++) {
		if (*p == '@' || *p == ':')
			return ("an scp-style address: write it as a URL, "
			    "ssh://...");
	}
	if (memchr(s, '.', (size_t)(slash - s)) == NULL) {
		*kind = SH_LOC_DIR;
		return (sh_syn_relpath(s));
	}
	for (p = s; p < slash; p++) {
		if (!syn_alnum(*p) && *p != '.' && *p != '-')
			return ("not a host's name");
	}
	if (*slash != '/' || slash[1] == '\0')
		return ("a host with no path: host/path, as "
		    "github.com/eidelbyte/freebsd-patches");
	*kind = SH_LOC_HOST;
	return (NULL);
}
