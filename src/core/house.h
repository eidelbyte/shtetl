/*
 * The houses (plan 7, 9): one actor for each value of shtetl:house on
 * the children of <run>/containers, each child a version of it, a
 * clone of a foundation version on the run with its own datasets
 * beside. The active version, the newest complete one, is mounted at
 * <containers>/<house>; one being made, at its own path,
 * <containers>/<version>. m5-create makes a house's first version.
 */

#ifndef	SH_HOUSE_H
#define	SH_HOUSE_H

#include <stddef.h>
#include <stdio.h>

#include "core/err.h"
#include "core/zfs.h"

enum sh_hstate {
	SH_H_ACTIVE,
	SH_H_CREATING
};

struct sh_hmake;

struct sh_house {
	char		*h_name;	/* its shtetl:house */
	enum sh_hstate	h_state;
	char		*h_active;	/* the active version's root, or NULL */
	struct sh_hmake	*h_make;	/* the create under way */
};

/* What every house is given: the daemon's, in a test the test's. */
struct sh_houseenv {
	struct sh_zfs	*he_z;
	int		he_kq;
	const char	*he_lake;	/* the spaces' roots */
	const char	*he_run;
	const char	*he_resolv;	/* the host's resolv.conf */
	const char	*he_logdir;	/* each house's log, H.log */
	struct sh_err	*he_err;	/* a create's failure, said */
	unsigned long	he_flags;	/* house.conf's: schg */
	/* sh_beam_pull, with HE_PARG for the beam: the daemon's */
	void		(*he_pull)(void *, const char *,
			    void (*)(void *, const char *, const char *),
			    void *);
	void		*he_parg;
};

/*
 * The houses among RUN's containers, sorted by name into *HP and *NP,
 * each started from ZFS: a version that is not complete, a create a
 * crash cut short, destroyed; the newest complete one, by its origin's
 * createtxg, active, and mounted at <containers>/<house> where it is
 * not. -1 when ZFS refuses a step, said to E.
 */
int sh_houses_start(struct sh_zfs *z, const char *run,
    struct sh_house ***hp, size_t *np, struct sh_err *e);
void sh_houses_free(struct sh_house **h, size_t n);

/*
 * The container NAME made on the foundation whose lake dataset is FDS,
 * at its version NEWEST, the snapshot's name after the @, as shtetl
 * create NAME --on F asks (tests/MATRIX.md family H). Refused, said to
 * E: "NAME: exists", "NAME: creating", the recipe's words; CONFIG is
 * where zconfig is mounted. The house is added to *HP in its place by
 * name, creating, and its log, NAME.log, made anew; the version is
 * pulled, cloned, made and placed as the steps go, and a failure
 * destroys it, takes the house away again, and is said to ENV's sink,
 * "house NAME: why". ENV outlives the create.
 */
int sh_house_create(struct sh_house ***hp, size_t *np, const char *name,
    const char *fds, const char *newest, const struct sh_houseenv *env,
    const char *config, struct sh_err *e);

/* H's line of shtetl status. */
void sh_house_status(const struct sh_house *h, FILE *out);

/*
 * H's create stopped where it stands, as when the daemon stops: its
 * child killed; the version, not complete, is destroyed by the next
 * start. A pull it waits for is the beam's to drop.
 */
void sh_house_stop(struct sh_house *h);

#endif	/* SH_HOUSE_H */
