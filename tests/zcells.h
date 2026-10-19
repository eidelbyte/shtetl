/*
 * Family Z of tests/MATRIX.md: the cells of the ZFS layer, written once
 * and run twice, over the fake by tests/check_zfs.c in make check and
 * over libzfs on real pools by tests/box/zcheck.c, so that the fake
 * cannot disagree with the box unseen. A refusal is read by its words,
 * "NAME: why", as a person would read it.
 */

#ifndef	SH_ZCELLS_H
#define	SH_ZCELLS_H

#include <stdio.h>

/*
 * The driver's: WORD written into the mounted filesystem DS, as a
 * file; and whether DS holds it, 1 or 0. Either -1 when DS is not
 * mounted.
 */
extern int (*zc_put)(const char *ds, const char *word);
extern int (*zc_has)(const char *ds, const char *word);

/*
 * The cells, under TOP, an empty filesystem with no mountpoint of its
 * own to inherit, each printed to OUT as "ok   Zn what" or "FAIL Zn
 * what". Nothing else on the system may set the user property
 * shtetl:zcheck. Returns how many failed.
 */
int zcells(FILE *out, const char *top);

#endif	/* SH_ZCELLS_H */
