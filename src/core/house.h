/*
 * The houses (plan 7, 9): one actor for each value of shtetl:house on
 * the children of <run>/containers, each child a version of it, a
 * clone of a foundation version on the run with its own datasets
 * beside. The active version, the newest complete one, is mounted at
 * <containers>/<house>; one being made, at its own path,
 * <containers>/<version>. m5-create makes a house's first version;
 * m5-jail starts a container's jail from its active version;
 * m5-save-pkg takes its saves and upgrades its packages; m5-roll rolls
 * it onto the next version of its foundation; m5-queue rolls it so
 * whenever one waits and it is idle.
 */

#ifndef	SH_HOUSE_H
#define	SH_HOUSE_H

#include <stddef.h>
#include <stdio.h>

#include "core/err.h"
#include "core/job.h"
#include "core/zfs.h"

enum sh_hstate {
	SH_H_ACTIVE,
	SH_H_CREATING,
	SH_H_STARTING,
	SH_H_FAILED,		/* left out at the start, ZFS refusing */
	SH_H_UPGRADING,
	SH_H_ROLLING
};

struct sh_hmake;
struct sh_hroll;
struct sh_houseenv;

struct sh_house {
	char				*h_name;	/* its shtetl:house */
	enum sh_hstate			h_state;
	char				*h_active;	/* the active root */
	char				*h_stall;	/* its shtetl:stall */
	const struct sh_houseenv	*h_env;
	struct sh_hmake			*h_make;	/* a create */
	struct sh_hroll			*h_roll;	/* a roll */
	char				**h_clean;	/* incomplete, */
	size_t				h_nclean;	/* to destroy */
	struct sh_job			h_job;		/* the act's */
	FILE				*h_log;		/* H.log, in one */
	struct sh_err			h_lerr;		/* to it, "# " */
};

/* What every house is given: the daemon's, in a test the test's. */
struct sh_houseenv {
	struct sh_zfs	*he_z;
	int		he_kq;
	const char	*he_lake;	/* the spaces' roots */
	const char	*he_run;
	const char	*he_resolv;	/* the host's resolv.conf */
	const char	*he_logdir;	/* each house's log, H.log */
	const char	*he_jails;	/* the jails' blocks, H.conf */
	struct sh_err	*he_err;	/* an act's failure, said */
	unsigned long	he_flags;	/* house.conf's: schg */
	/* sh_beam_pull, with HE_PARG for the beam: the daemon's */
	void		(*he_pull)(void *, const char *,
			    void (*)(void *, const char *, const char *),
			    void *);
	/* the beam told a version or a snapshot is made, HE_PARG's */
	void		(*he_beam)(void *);
	/*
	 * a house's act over, the house active, the last thing its end
	 * does: the daemon asks it to take the next (sh_house_next)
	 */
	void		(*he_idle)(void *, struct sh_house *);
	void		*he_parg;
};

/*
 * The houses among ENV's run's containers, sorted by name into *HP and
 * *NP, each started from ZFS. First every copy of a foundation on the
 * run is unmounted, a roll a crash cut short having left it mounted.
 * The newest complete version, by its origin's createtxg, is active:
 * every other version placed at <containers>/<house>, a swap a crash
 * left undone, unmounted and given back its own path (m5-roll), and
 * the active one mounted at <containers>/<house> where it is not; its
 * shtetl:stall read; then its jail started (sh_house_create's last
 * steps), the house starting until it is, and a start that fails said
 * to ENV's sink, "house H: why". A version that is not complete is
 * destroyed: a house's, after its start, by its job, every open
 * zfs_rebase run on it aborted first, a roll a crash cut short; one of
 * a house with no complete version, a create cut short, at once. A
 * house ZFS refuses a step of is said to the sink once, "house H:
 * why", and left out, failed, until the next start; the rest go on
 * (ruled 2026-11-05, Q-H1). -1 when ZFS refuses the run's containers
 * themselves, said to E. ENV outlives the houses.
 */
int sh_houses_start(const struct sh_houseenv *env, struct sh_house ***hp,
    size_t *np, struct sh_err *e);
void sh_houses_free(struct sh_house **h, size_t n);

/*
 * The container NAME made on the foundation whose lake dataset is FDS,
 * at its version NEWEST, the snapshot's name after the @, as shtetl
 * create NAME --on F asks (tests/MATRIX.md family H). Refused, said to
 * E: "NAME: exists", "NAME: creating", the recipe's words; CONFIG is
 * where zconfig is mounted. The house is added to *HP in its place by
 * name, creating, and its log, NAME.log, made anew; the version is
 * pulled, cloned, made and placed as the steps go, and its jail
 * started last (family N): the jail's block written from the shaken
 * recipe in the version's create snapshot, then jail -cm. A failure
 * destroys the version and any block it wrote, takes the house away
 * again, and is said to ENV's sink, "house NAME: why". ENV outlives
 * the create.
 */
int sh_house_create(struct sh_house ***hp, size_t *np, const char *name,
    const char *fds, const char *newest, const struct sh_houseenv *env,
    const char *config, struct sh_err *e);

/*
 * H's active version snapshotted at once, every dataset of it,
 * <date>-save, kind save, and the beam told (m5-save-pkg). Refused,
 * said to E: "H: creating", "H: starting", "H: upgrading", "H:
 * failed"; what ZFS says.
 */
int sh_house_save(struct sh_house *h, struct sh_err *e);

/*
 * H's own packages upgraded in its running jail, pkg -j H upgrade,
 * then its active version snapshotted as a save is, kind pkg, and the
 * beam told. Its own are those its house.conf lists less those its
 * foundation.conf lists (ruled 2026-11-05), both read from the
 * version's create or update snapshot; with none, no pkg runs. The
 * act is H's job, "house H upgrading STEP" meanwhile, a failure said
 * to ENV's sink, "house H: why". Refused, said to E, as a save is.
 */
int sh_house_pkg(struct sh_house *h, struct sh_err *e);

/*
 * H rolled onto the next version of its foundation the run's copy
 * holds (plan 7.2, tests/MATRIX.md families W and Q): the oldest newer
 * than its active version's origin that has arrived, the copy's top
 * snapshot holding shtetl:kind, and whose shtetl:branch H's ver allows,
 * its recipe resolved from CONFIG as it is now. Its stall lifted first,
 * a freeze among them, whether one waits or not; then a job of the
 * steps: the jail stopped; the final save; the copy mounted; the
 * replay into the new version, named as a create names one; its own
 * datasets cloned from the final save; what its recipe gained, read
 * against the old version's house.conf; house.conf; the update
 * snapshot; the swap; the start; the copy unmounted where no other
 * house's roll is on it; the beam told. "house H rolling STEP"
 * meanwhile. A failure before the update snapshot aborts the open
 * zfs_rebase runs on the new version, destroys it, starts the old
 * version again and stalls H: shtetl:stall on it, the step and why,
 * said once to ENV's sink. CONFIG is where zconfig is mounted; *HP the
 * houses, another's roll being on the same copy. Refused, said to E:
 * "H: up to date", or as a save is.
 */
int sh_house_roll(struct sh_house ***hp, size_t *np, struct sh_house *h,
    const char *config, struct sh_err *e);

/*
 * H asked to take the next version waiting, as the daemon asks every
 * house when a convergence of the beam is over and a house when its
 * act is (plan 7.2): where H is active, not stalled, and one waits, an
 * automatic roll of it, as sh_house_roll's, which a conflict stalls.
 * Nothing waiting, nothing is said; any other refusal stalls H, "next:
 * why", said once to ENV's sink, "house H: next: why", so that it is
 * not asked again until it is rolled by hand.
 */
void sh_house_next(struct sh_house ***hp, size_t *np, struct sh_house *h,
    const char *config);

/*
 * H pinned to the version it is on, as shtetl freeze H asks (plan 7.2,
 * ruled 2026-10-19): a stall the person sets, shtetl:stall "frozen" on
 * its active version, a stall it had replaced; not taken by the queue
 * until shtetl roll H lifts it. Refused, said to E, as a save is.
 */
int sh_house_freeze(struct sh_house *h, struct sh_err *e);

/* H's line of shtetl status. */
void sh_house_status(const struct sh_house *h, FILE *out);

/*
 * H's act stopped where it stands, as when the daemon stops: its child
 * killed. A create's version, not complete, is destroyed by the next
 * start, and a pull it waits for is the beam's to drop; a start's
 * jail, by -cm, is made or left at the next; an upgrade's packages
 * stay as far as pkg took them, with no snapshot of them.
 */
void sh_house_stop(struct sh_house *h);

#endif	/* SH_HOUSE_H */
