/*
 * config show: a merged recipe, or the site settings, printed as UCL
 * that parses back to the same keys, values and order, each value
 * with a comment naming its layer.
 */

#ifndef	SH_SHOW_H
#define	SH_SHOW_H

#include <stdio.h>

#include "core/cascade.h"
#include "core/site.h"
#include "core/tree.h"

void sh_show_recipe(FILE *out, const struct sh_recipe *r);
void sh_show_site(FILE *out, const struct sh_site *s);

#endif	/* SH_SHOW_H */
