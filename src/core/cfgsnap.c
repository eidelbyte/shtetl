/*
 * The config root's snapshots (planning.md 7.1, 7.4, 7.6): taken only
 * when the root has changed, named three words.
 *
 * Decided here, beyond the plan (ruled 2026-10-14): a build, create or
 * update records the config root's newest shtetl:kind config snapshot
 * when ZFS says nothing was written to the root since it (written@),
 * and takes one otherwise. Its name is three words drawn from a hash
 * of the time and a try count, so a run is repeatable and a name taken
 * is passed over; its shtetl:commit says what took it.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "core/cfgsnap.h"
#include "core/err.h"
#include "core/zfs.h"
#include "core/zwalk.h"

#define	CS_TRIES	100

static const char *const cs_words[SH_CFGSNAP_WORDS] = {
	"acorn", "alder", "amber", "anchor", "anvil", "apple", "apron",
	"arbor", "aspen", "atlas", "attic", "badge", "badger", "bagel",
	"banjo", "barley", "barn", "basin", "basket", "beacon", "beam", "bean",
	"beetle", "bell", "bench", "berry", "birch", "bison", "blade", "bloom",
	"boat", "bobbin", "bonnet", "book", "boot", "bottle", "boulder",
	"bowl", "branch", "bread", "brick", "bridge", "brook", "broom",
	"bucket", "bugle", "button", "cabin", "cactus", "camel", "candle",
	"canoe", "canyon", "cargo", "carrot", "castle", "cedar", "cello",
	"chalk", "cherry", "chimney", "cider", "cinder", "clay", "cliff",
	"clock", "cloud", "clover", "coast", "cobalt", "cobble", "comet",
	"compass", "copper", "coral", "cotton", "cradle", "crane", "crater",
	"creek", "cricket", "crown", "dahlia", "daisy", "delta", "desk",
	"dove", "dune", "eagle", "easel", "elm", "ember", "falcon", "feather",
	"fern", "ferry", "fiddle", "field", "finch", "fjord", "flag", "flint",
	"flute", "forest", "fossil", "fox", "garden", "garnet", "gate",
	"gecko", "geyser", "ginger", "glacier", "globe", "goose", "granite",
	"grape", "gravel", "grove", "guitar", "gull", "hammer", "harbor",
	"hare", "harp", "hazel", "heron", "hill", "hive", "honey", "horizon",
	"iris", "island", "ivy", "jar", "jasper", "juniper", "kelp", "kettle",
	"kite", "ladder", "lagoon", "lake", "lamp", "lantern", "larch", "lark",
	"leaf", "lemon", "lichen", "lily", "linen", "lotus", "lynx", "magnet",
	"mango", "maple", "marble", "meadow", "melon", "mesa", "mill", "mint",
	"mirror", "moss", "moth", "needle", "nest", "newt", "nutmeg", "oak",
	"oar", "oasis", "olive", "onion", "orchid", "oriole", "otter", "owl",
	"paddle", "palm", "panda", "paper", "parcel", "peach", "pear",
	"pebble", "pepper", "piano", "pine", "planet", "plum", "pond",
	"poplar", "prairie", "puffin", "quartz", "quill", "quilt", "rabbit",
	"radish", "raven", "reed", "reef", "ribbon", "ridge", "river", "robin",
	"rocket", "rope", "rose", "saddle", "sage", "sail", "salmon", "sand",
	"seal", "shell", "shore", "silver", "slate", "sled", "sparrow",
	"spoon", "spruce", "star", "stone", "stream", "summit", "swan",
	"table", "teapot", "tern", "thistle", "thread", "thrush", "thyme",
	"tiger", "timber", "torch", "tower", "trail", "tulip", "tunnel",
	"turnip", "valley", "velvet", "violet", "walnut", "walrus", "whale",
	"willow", "window", "wren", "yarrow", "zebra"
};

/* splitmix64's step: every bit of X stirred into every bit out. */
static uint64_t
cs_mix(uint64_t x)
{
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return (x ^ (x >> 31));
}

void
sh_cfgsnap_name(time_t now, unsigned try, char *buf, size_t len)
{
	uint64_t x;
	unsigned a, b, c;

	x = cs_mix((uint64_t)now ^ cs_mix(try));
	a = x & 0xff;
	b = (x >> 8) & 0xff;
	c = (x >> 16) & 0xff;
	/* three different words */
	while (b == a)
		b = (b + 1) & 0xff;
	while (c == a || c == b)
		c = (c + 1) & 0xff;
	(void) snprintf(buf, len, "%s-%s-%s", cs_words[a], cs_words[b],
	    cs_words[c]);
}

/* ROOT's newest shtetl:kind config snapshot, after the @, into LAST. */
static int
cs_last(struct sh_zfs *z, const char *root, char *last, size_t len,
    struct sh_err *e)
{
	char name[SH_CFGSNAP_NAME * 2], val[SH_ZFS_VALMAX], **snaps;
	size_t i;
	int r;

	last[0] = '\0';
	if ((snaps = sh_zw_snaps(z, root, e)) == NULL)
		return (-1);
	for (i = 0, r = 0; r == 0 && snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", root, snaps[i]);
		if (sh_zfs_get(z, name, "shtetl:kind", val, sizeof (val), NULL,
		    e) != 0)
			r = -1;
		else if (strcmp(val, "config") == 0)
			(void) snprintf(last, len, "%s", snaps[i]);
	}
	sh_zw_free(snaps);
	return (r);
}

int
sh_cfgsnap(struct sh_zfs *z, const char *root, const char *msg,
    time_t now, char *snap, size_t len, FILE *out, struct sh_err *e)
{
	char last[SH_CFGSNAP_NAME], words[SH_CFGSNAP_NAME];
	char val[SH_ZFS_VALMAX], prop[SH_CFGSNAP_NAME + 16];
	struct sh_zprop zp[2];
	const char *one;
	unsigned try;
	int r;

	if (cs_last(z, root, last, sizeof (last), e) != 0)
		return (-1);
	if (last[0] != '\0') {
		(void) snprintf(prop, sizeof (prop), "written@%s", last);
		if (sh_zfs_get(z, root, prop, val, sizeof (val), NULL, e) != 0)
			return (-1);
		if (strcmp(val, "0") == 0) {
			(void) snprintf(snap, len, "%s@%s", root, last);
			return (0);
		}
	}
	for (try = 0; try < CS_TRIES; try++) {
		sh_cfgsnap_name(now, try, words, sizeof (words));
		(void) snprintf(snap, len, "%s@%s", root, words);
		if ((r = sh_zfs_get(z, snap, "type", val, sizeof (val), NULL,
		    e)) < 0)
			return (-1);
		if (r == 1)
			break;
	}
	if (try == CS_TRIES) {
		sh_errf(e, "%s: no free name for a config snapshot", root);
		return (-1);
	}
	zp[0].zp_name = "shtetl:kind";
	zp[0].zp_value = "config";
	zp[1].zp_name = "shtetl:commit";
	zp[1].zp_value = msg;
	one = snap;
	if (sh_zfs_snapshot(z, &one, 1, zp, 2, e) != 0)
		return (-1);
	(void) fprintf(out, "snapshot %s\n", snap);
	return (0);
}
