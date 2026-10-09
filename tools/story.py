#!/usr/bin/env python3
#
# story.py: generate docs/story.html, a frame-by-frame walk through one
# shtetl cycle: build a foundation, build a container on it, then update
# the container onto a new version of the foundation.
#
# The diagrams are drawn from a state model of the four spaces, so every
# frame shares one layout and a frame only differs from the one before
# it by what the story did. Edit this script, not the page.
#
# usage: python3 tools/story.py > docs/story.html
#
# ASCII only.

import copy
import sys
from html import escape as esc

# -- geometry --------------------------------------------------------------

ROW = 20            # one dataset row
HEAD = 48           # box header
CW = 7.25           # width of one 12px monospace character
PW = 6.3            # width of one pill character
W = 1110            # canvas
H = 716


def tree(prefix, name, depth, www=False):
	"""A foundation-shaped tree: name, tmp, usr (placeholder), usr/local, var."""
	rows = [
		(prefix, name, depth, "ds"),
		(prefix + "/tmp", "tmp", depth + 1, "ds"),
		(prefix + "/usr", "usr", depth + 1, "ds"),
		(prefix + "/usr/local", "local", depth + 2, "ds"),
		(prefix + "/var", "var", depth + 1, "ds"),
	]
	if www:
		rows.append((prefix + "/www", "www", depth + 1, "ds"))
	return rows


TREE = ["", "/tmp", "/usr", "/usr/local", "/var"]
TREEW = TREE + ["/www"]


class Space:
	def __init__(self, key, label, sub, x, y, w, rows):
		self.key = key
		self.label = label
		self.sub = sub
		self.x = x
		self.y = y
		self.w = w
		self.x2 = x + w
		self.rows = rows
		self.index = {r[0]: i for i, r in enumerate(rows)}
		self.h = HEAD + len(rows) * ROW + 10

	def cy(self, rid):
		return self.y + HEAD + self.index[rid] * ROW + ROW / 2


CONFIG = Space("config", "zconfig", "shtetl:space=config", 20, 20, 205, [
	("C", "fast/shtetl/config", 0, "root"),
	("C/sc", "shtetl.conf", 1, "file"),
	("C/dc", "default.conf", 1, "file"),
	("C/f", "foundations/", 1, "dir"),
	("C/fg", "generic.conf", 2, "file"),
	("C/c", "containers/", 1, "dir"),
	("C/cn", "nginx.conf", 2, "file"),
])

BUILD = Space("build", "zbuild", "shtetl:space=build", 20, 250, 205, [
	("B", "fast/shtetl/src", 0, "root"),
	("B/t", "freebsd-src", 1, "file"),
])

LAKE = Space("lake", "zlake", "shtetl:space=lake  pool tank (HDD)", 270, 20,
    360, [
	("L", "tank/shtetl", 0, "root"),
	("L/f", "foundations", 1, "ds"),
] + tree("L/f/G", "generic-15.1-releng", 2) + [
	("L/fb", "foundation-builds", 1, "ds"),
	("L/fb/G", "generic-15.1-releng", 2, "ds"),
	("L/c", "containers", 1, "ds"),
] + tree("L/c/n", "nginx", 2, True) + tree("L/c/ns", "nginx-staging", 2, True) + [
	("L/s", "systems", 1, "ds"),
	("L/a", "archive", 1, "ds"),
	("L/a/c", "containers", 2, "ds"),
] + tree("L/a/c/n", "nginx-2026-11-20", 3, True))

RUN = Space("run", "zrun", "shtetl:space=run  pool fast (NVMe, boot)", 700,
    20, 345, [
	("R", "fast/shtetl/run", 0, "root"),
	("R/f", "foundations", 1, "ds"),
] + tree("R/f/G", "generic-15.1-releng", 2) + [
	("R/c", "containers", 1, "ds"),
] + tree("R/c/n", "nginx", 2, True) + tree("R/c/ns", "nginx-staging", 2, True) + [
	("R/s", "systems", 1, "ds"),
])

SPACES = {"C": CONFIG, "B": BUILD, "L": LAKE, "R": RUN}


def space_of(rid):
	return SPACES[rid[0]]


# -- state -----------------------------------------------------------------

PILL_KIND = {
	"v1": "version", "v2": "version",
	"s1": "save", "s2": "from", "s3": "save", "s4": "save", "s5": "save",
}


class State:
	def __init__(self):
		self.present = set()
		self.mounted = set()
		self.pills = {}
		self.tags = {}

	def add(self, *rids):
		for r in rids:
			self.present.add(r)

	def drop(self, *rids):
		for r in rids:
			self.present.discard(r)
			self.mounted.discard(r)
			self.pills.pop(r, None)
			self.tags.pop(r, None)

	def snap(self, rids, name):
		for r in rids:
			self.pills.setdefault(r, []).append(name)

	def tag(self, rid, text):
		if text:
			self.tags[rid] = text
		else:
			self.tags.pop(rid, None)

	def rename(self, src, dst, suffixes):
		for s in suffixes:
			a, b = src + s, dst + s
			if a not in self.present:
				continue
			self.present.add(b)
			if a in self.mounted:
				self.mounted.add(b)
			if a in self.pills:
				self.pills[b] = self.pills[a]
			if a in self.tags:
				self.tags[b] = self.tags[a]
			self.drop(a)


def T(prefix, suffixes=TREE):
	return [prefix + s for s in suffixes]


# -- svg -------------------------------------------------------------------

ARROW_CLASS = {
	"build": "build", "install": "build", "checkout": "build",
	"send": "lake", "save": "lake",
	"clone": "run", "rebase": "rebase", "rename": "muted",
	"promote": "muted",
}


def bez_mid(p0, c1, c2, p3, t=0.5):
	u = 1 - t
	x = u**3 * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t**3 * p3[0]
	y = u**3 * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t**3 * p3[1]
	return x, y


def route(a, b, side=None, bulge=30):
	sa, sb = space_of(a), space_of(b)
	ya, yb = sa.cy(a), sb.cy(b)
	if sa is sb:
		if side is None:
			side = "R" if sa is RUN else "L"
		if side == "R":
			e = sa.x2
			return (e, ya), (e + bulge, ya), (e + bulge, yb), (e + 2, yb)
		e = sa.x
		return (e, ya), (e - bulge, ya), (e - bulge, yb), (e - 2, yb)
	if sa.x2 <= sb.x:
		p0, p3 = (sa.x2, ya), (sb.x - 2, yb)
		d = (p3[0] - p0[0]) * 0.45
		return p0, (p0[0] + d, ya), (p3[0] - d, yb), p3
	p0, p3 = (sa.x, ya), (sb.x2 + 2, yb)
	d = (p0[0] - p3[0]) * 0.45
	return p0, (p0[0] - d, ya), (p3[0] + d, yb), p3


def svg_arrow(uid, a, b, kind, num=None, side=None, bulge=30, t=0.5):
	p0, c1, c2, p3 = route(a, b, side, bulge)
	cls = ARROW_CLASS[kind]
	dash = ' stroke-dasharray="5 4"' if kind in ("rename", "promote") else ""
	out = ['<path class="ar ar-%s" d="M%.1f,%.1f C%.1f,%.1f %.1f,%.1f %.1f,%.1f"%s '
	    'marker-end="url(#m%s-%s)"/>' % (cls, p0[0], p0[1], c1[0], c1[1],
	    c2[0], c2[1], p3[0], p3[1], dash, uid, cls)]
	if num is not None:
		mx, my = bez_mid(p0, c1, c2, p3, t)
		out.append('<circle class="badge badge-%s" cx="%.1f" cy="%.1f" r="9"/>'
		    % (cls, mx, my))
		out.append('<text class="badgetext" x="%.1f" y="%.1f" '
		    'text-anchor="middle">%s</text>' % (mx, my + 4, num))
	return "\n".join(out)


def svg_space(sp, st, hl):
	out = []
	out.append('<rect class="box box-%s" x="%d" y="%d" width="%d" height="%d" '
	    'rx="8"/>' % (sp.key, sp.x, sp.y, sp.w, sp.h))
	out.append('<text class="slabel slabel-%s" x="%d" y="%d">%s</text>'
	    % (sp.key, sp.x + 14, sp.y + 22, sp.label))
	out.append('<text class="ssub" x="%d" y="%d">%s</text>'
	    % (sp.x + 14, sp.y + 38, esc(sp.sub)))
	for rid, label, depth, kind in sp.rows:
		if rid not in st.present:
			continue
		top = sp.y + HEAD + sp.index[rid] * ROW
		cy = top + ROW / 2
		h = hl.get(rid)
		if h:
			out.append('<rect class="hl hl-%s" x="%d" y="%.1f" width="%d" '
			    'height="%d" rx="4"/>' % (h, sp.x + 4, top + 1, sp.w - 8,
			    ROW - 2))
		if rid in st.mounted:
			out.append('<circle class="mdot" cx="%d" cy="%.1f" r="3.5"/>'
			    % (sp.x + 12, cy))
		tx = sp.x + 22 + depth * 14
		cls = {"root": "rroot", "ds": "rds", "file": "rfile",
		    "dir": "rfile"}[kind]
		out.append('<text class="%s" x="%d" y="%.1f">%s</text>'
		    % (cls, tx, cy + 4, esc(label)))
		end = tx + len(label) * CW
		if h == "gone":
			out.append('<line class="strike" x1="%d" y1="%.1f" x2="%.1f" '
			    'y2="%.1f"/>' % (tx - 2, cy, end + 2, cy))
		tag = st.tags.get(rid)
		if tag:
			out.append('<text class="rtag" x="%.1f" y="%.1f">%s</text>'
			    % (end + 8, cy + 4, esc(tag)))
		px = sp.x2 - 8
		for p in reversed(st.pills.get(rid, [])[-3:]):
			pw = len(p) * PW + 10
			px -= pw
			out.append('<rect class="pill pill-%s" x="%.1f" y="%.1f" '
			    'width="%.1f" height="15" rx="7.5"/>'
			    % (PILL_KIND[p], px, cy - 7.5, pw))
			out.append('<text class="pilltext" x="%.1f" y="%.1f" '
			    'text-anchor="middle">%s</text>' % (px + pw / 2, cy + 3.5, p))
			px -= 4
	return "\n".join(out)


def svg_frame(st, hl, arrows, title, uid):
	out = ['<svg viewBox="0 0 %d %d" role="img" aria-label="%s">'
	    % (W, H, esc(title))]
	out.append("<defs>")
	for c in ("build", "lake", "run", "rebase", "muted"):
		out.append('<marker id="m%s-%s" viewBox="0 0 10 10" refX="9" refY="5" '
		    'markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
		    '<path class="mk mk-%s" d="M0,0 L10,5 L0,10 z"/></marker>'
		    % (uid, c, c))
	out.append("</defs>")
	for sp in (CONFIG, BUILD, LAKE, RUN):
		out.append(svg_space(sp, st, hl))
	for a in arrows:
		out.append(svg_arrow(uid, **a))
	out.append("</svg>")
	return "\n".join(out)


def A(a, b, kind, num=None, side=None, bulge=30, t=0.5):
	return dict(a=a, b=b, kind=kind, num=num, side=side, bulge=bulge, t=t)


# -- page pieces -----------------------------------------------------------

def code(text, cls=""):
	return '<pre class="%s"><code>%s</code></pre>' % (cls, esc(text.strip("\n")))


def codes(*pairs):
	out = ['<div class="codes">']
	for name, text in pairs:
		out.append('<div class="codebox"><div class="fname">%s</div>%s</div>'
		    % (esc(name), code(text)))
	out.append("</div>")
	return "\n".join(out)


def checks(title, items):
	out = ['<div class="checks"><div class="ctitle">%s</div><ul>' % esc(title)]
	for status, text in items:
		out.append('<li><span class="cs cs-%s">%s</span><span>%s</span></li>'
		    % (status, {"ok": "ok", "warn": "warn", "fail": "fail",
		    "note": "note", "prop": "prop"}[status], text))
	out.append("</ul></div>")
	return "\n".join(out)


def callout(kind, title, body):
	return ('<div class="callout callout-%s"><div class="ctitle">%s</div>%s'
	    '</div>' % (kind, esc(title), body))


def p(*paras):
	return "\n".join("<p>%s</p>" % x for x in paras)


# -- the story -------------------------------------------------------------

frames = []
st = State()


def act(num, title, intro):
	frames.append(dict(act=True, num=num, title=title, intro=intro))


def frame(title, before="", after="", hl=None, arrows=None, diagram=True,
    panel=""):
	frames.append(dict(act=False, title=title, before=before, after=after,
	    panel=panel, diagram=diagram, st=copy.deepcopy(st), hl=hl or {},
	    arrows=arrows or []))


# Act 0 -------------------------------------------------------------------

act("0", "The stage", p(
	"One machine, <code>kfar</code>, with two pools: <code>fast</code>, an "
	"NVMe mirror it boots from, and <code>tank</code>, a raidz of large "
	"disks. Its admin installed shtetl and ran <code>shtetl bootstrap</code>, "
	"naming one existing dataset for each of the four spaces."))

st.add("C", "C/sc", "C/dc", "C/f", "C/c", "B", "B/t",
    "L", "L/f", "L/fb", "L/c", "L/s", "L/a", "L/a/c",
    "R", "R/f", "R/c", "R/s")
st.mounted |= {"C", "R/c"}
st.tag("B/t", "git checkout")
st.tag("R/c", "/containers")

frame("After bootstrap",
    before=p(
	"Bootstrap touched no device. It set a local user property, "
	"<code>shtetl:space</code>, on each root, created the empty parents "
	"under zlake and zrun with parasa's defaults (<code>atime=off</code>, "
	"<code>compression=zstd</code>, <code>aclmode=passthrough</code>, "
	"<code>mountpoint=none</code>, <code>canmount=noauto</code>), and made "
	"<code>zrun/containers</code> the one parent that mounts by itself, at "
	"<code>/containers</code>.",
	"zconfig is a clone of the zshtetl-recipes repo: <code>default.conf</code> "
	"came from upstream, <code>shtetl.conf</code> was written by bootstrap. "
	"zbuild is a FreeBSD src checkout the admin already had. From here on "
	"every frame shows the same four boxes; only what the story does "
	"changes."),
    after=p(
	"How to read a frame: a dot before a name means mounted. Pills on the "
	"right are snapshots (blue a foundation version, grey a save, orange "
	"the snapshot an update starts from). A tinted row was created, "
	"written, read or removed in that frame, as the legend at the top "
	"says. Numbered arrows are the operations, listed under the "
	"diagram."))

# Act I -------------------------------------------------------------------

act("I", "A foundation", p(
	"A foundation is FreeBSD built from source by a recipe. Every "
	"container and system is a clone of one, so it is built first."))

st.add("C/fg")

DEFAULT_CONF = r'''
# default.conf: the base layer of every recipe (from upstream)

datasets {
    tmp {}
    var {}
    "usr/local" {}
}

derivations {
    "etc/passwd" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";
    }
    "etc/pwd.db" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";
    }
    "etc/spwd.db" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";
    }
    "etc/login.conf.db" {
        source = "etc/login.conf";
        command = "cap_mkdb /etc/login.conf";
    }
}
'''

GENERIC_CONF = r'''
# foundations/generic.conf: a GENERIC kernel and world
ver = ">=15";
src_branch = "releng/15.1";
kernconf = "GENERIC";
'''

SHTETL_CONF = r'''
# shtetl.conf: about this machine (bootstrap's starter)
update {
    suffix = "staging";
}
retention {
    run = 10;
    lake = 0;
    archive = "1y";
}
'''

frame("The admin writes a foundation recipe", diagram=False,
    before=p(
	"The admin adds <code>foundations/generic.conf</code> to the recipes "
	"clone. It is three lines: everything else comes from "
	"<code>default.conf</code>, the base layer every recipe is built on. "
	"<code>shtetl.conf</code> is not part of any recipe; it holds what is "
	"about this machine."),
    panel=codes(("foundations/generic.conf (new)", GENERIC_CONF),
	("default.conf", DEFAULT_CONF), ("shtetl.conf", SHTETL_CONF)),
    after=callout("question", "Open question Q-A",
	p("<code>shtetl.conf</code> and <code>generic.conf</code> are site "
	"files, which upstream's default <code>.gitignore</code> would ignore. "
	"An ignored file has no git history at all. Are zconfig's snapshots "
	"enough history for them, or should site files be committed in the "
	"local clone and only kept out of what is pushed upstream?")))

FAILS = r'''
foundations/generic.conf:2: ver must be a string; write "15.10"
    (UCL read the float 15.1)
default.conf:1: macros are not allowed; use import
foundations/generic.conf:5: jailconf is a container key; a foundation
    cannot set it
'''

frame("shtetl checks the recipe", diagram=False,
    before=p(
	"<code>shtetl build generic</code> reads the recipe before it touches "
	"anything. The checks run in three rounds; any failure stops the "
	"command with the file and line."),
    panel=checks("parse (each file alone)", [
	("ok", "default.conf, foundations/generic.conf: UCL, ASCII only"),
	("ok", "no key written twice in one file (parsed with "
	    "<code>UCL_DUPLICATE_ERROR</code>)"),
	("ok", "no macros (<code>.include</code>, <code>.priority</code>, ...)"
	    " and no <code>$variables</code>"),
    ]) + checks("schema", [
	("ok", "every key is known, and allowed for a foundation "
	    "(<code>kernconf</code> yes; <code>jailconf</code>, "
	    "<code>requires</code>, <code>foundation</code> no)"),
	("ok", "types: <code>ver</code> and <code>src_branch</code> are "
	    "strings; <code>datasets</code> and <code>derivations</code> are "
	    "keyed collections"),
	("ok", "dataset names are valid ZFS components; <code>usr/local</code> "
	    "implies a placeholder <code>usr</code>"),
	("ok", "every derivation has a <code>source</code> and a "
	    "<code>command</code>"),
    ]) + checks("meaning", [
	("ok", "<code>releng/15.1</code> is version 15.1, branch type releng; "
	    "it satisfies <code>ver &gt;= 15</code>"),
	("ok", "the foundation name is <code>generic-15.1-releng</code>; no "
	    "dataset of that name exists in any lake root, so this is a new "
	    "foundation"),
	("ok", "spaces: one root each for config, build, lake and run"),
	("ok", "after checkout: <code>sys/amd64/conf/GENERIC</code> exists in "
	    "the src tree"),
    ]),
    after=p("What a failure looks like:") + code(FAILS))

CONFIG_SHOW = r'''
$ shtetl config show foundations/generic
kind         foundation
name         generic-15.1-releng            (computed)
ver          ">=15"                         foundations/generic.conf:2
src_branch   "releng/15.1"                  foundations/generic.conf:3
kernconf     "GENERIC"                      foundations/generic.conf:4
datasets     tmp                            default.conf:4
             usr        placeholder         implied by usr/local
             usr/local                      default.conf:6
             var                            default.conf:5
derivations  etc/passwd        <- etc/master.passwd   default.conf:10
             etc/pwd.db        <- etc/master.passwd   default.conf:14
             etc/spwd.db       <- etc/master.passwd   default.conf:18
             etc/login.conf.db <- etc/login.conf      default.conf:22
'''

PLAN1 = r'''
$ shtetl build generic
foundation generic-15.1-releng (new), version @2026-10-08-0f691888dc56

  create   tank/shtetl/foundations/generic-15.1-releng
  create     .../tmp
  create     .../usr                    canmount=off (placeholder)
  create     .../usr/local
  create     .../var
  create   tank/shtetl/foundation-builds/generic-15.1-releng
  src      fast/shtetl/src: checkout releng/15.1 (0f691888dc56)
  build    buildworld buildkernel KERNCONF=GENERIC
           MAKEOBJDIRPREFIX=<foundation-builds/generic-15.1-releng>
  install  installworld installkernel distribution
           DESTDIR=<foundations/generic-15.1-releng>
  snapshot -r @2026-10-08-0f691888dc56  shtetl:kind=version

proceed? [y/N] y
'''

frame("The merged recipe, and the plan", diagram=False,
    before=p(
	"The layers merge (default.conf, then the target) and shtetl can show "
	"where every value came from. Then it prints the plan and asks."),
    panel=code(CONFIG_SHOW) + code(PLAN1),
    after=callout("finding", "Finding: usr/local is two datasets",
	p("Children mount by inheritance, and an inherited mountpoint is the "
	"parent's plus the child's name. A dataset called "
	"<code>usr-local</code> (parasa's name) would mount at "
	"<code>.../usr-local</code>, not <code>/usr/local</code>. So "
	"<code>usr/local</code> becomes <code>usr</code> with "
	"<code>canmount=off</code>, holding nothing, and <code>usr/local</code> "
	"under it, the way FreeBSD's own zroot does it. planning.md and the "
	"architecture page are corrected to match.")))

st.add(*T("L/f/G"), "L/fb/G")
st.tag("L/f/G/usr", "canmount=off")
frame("Pre-split: the datasets exist before any file does",
    before=p(
	"shtetl creates the foundation and one child per recipe dataset, all "
	"empty, and an obj cache for this foundation."),
    hl=dict({r: "new" for r in T("L/f/G") + ["L/fb/G"]},
	**{"C/dc": "read", "C/fg": "read", "C/sc": "read"}),
    after=p(
	"Creating the children first is what makes everything later work. "
	"A container cloned from this foundation will have its own "
	"<code>var</code>, <code>usr/local</code> and <code>tmp</code>, each a "
	"clone of the foundation's, and so each one will have a base to rebase "
	"from when the foundation moves. A directory split into a dataset "
	"after the fact would have no such lineage."))

st.mounted |= set(["B", "L/fb/G"] + T("L/f/G", ["", "/tmp", "/usr/local", "/var"]))
st.tag("B/t", "0f691888dc56")
frame("Build",
    before=p(
	"The src checkout, the obj cache and the foundation tree are mounted "
	"(the foundation's children follow by inheritance). The checkout moves "
	"to <code>releng/15.1</code>."),
    hl={"B/t": "write", "L/fb/G": "write"},
    arrows=[A("B/t", "L/fb/G", "build", 1)],
    after=p(
	"<b>1</b> <code>make buildworld buildkernel KERNCONF=GENERIC</code> "
	"with <code>MAKEOBJDIRPREFIX</code> on "
	"<code>foundation-builds/generic-15.1-releng</code>. The obj tree "
	"stays there after the build, so the next build of this foundation "
	"starts warm (best with <code>WITH_META_MODE</code>)."))

frame("Install into the foundation",
    hl={"L/f/G": "write", "L/f/G/var": "write", "L/f/G/tmp": "write",
	"L/f/G/usr/local": "write"},
    arrows=[A("L/fb/G", "L/f/G", "install", 2, side="R", bulge=34)],
    before=p(
	"There is no separate DESTDIR: the build installs straight into the "
	"mounted foundation."),
    after=p(
	"<b>2</b> <code>make installworld installkernel distribution "
	"DESTDIR=</code>the foundation's mountpoint. Files land in whichever "
	"dataset owns their path: <code>/var</code> in <code>var</code>, the "
	"<code>/usr/local</code> skeleton in <code>usr/local</code>, "
	"everything else (<code>/usr</code> included, since <code>usr</code> "
	"is a placeholder) in the foundation's own dataset. A foundation "
	"recipe with <code>pkg</code> or <code>commands</code> would run them "
	"now, in a chroot of the tree; this one has neither."))

st.mounted -= set(["B", "L/fb/G"] + T("L/f/G"))
st.snap(T("L/f/G"), "v1")
frame("Snapshot: version v1",
    hl={r: "snap" for r in T("L/f/G")},
    before=p(
	"Everything is unmounted again and the foundation is snapshotted "
	"recursively, in one atomic step, with its properties set at "
	"creation."),
    panel=code(r'''
zfs snapshot -r \
    -o shtetl:kind=version \
    -o shtetl:commit="build generic-15.1-releng from releng/15.1 0f691888dc56" \
    tank/shtetl/foundations/generic-15.1-releng@2026-10-08-0f691888dc56
'''),
    after=p(
	"From here on this page calls that snapshot <b>v1</b>. The name is "
	"for people (<code>{recipe}-{ver}-{branch-type}</code>, then "
	"<code>@&lt;ISO-date&gt;-&lt;short-sha&gt;</code>, so a listing sorts "
	"by recipe, version, then date); shtetl itself reads "
	"<code>shtetl:kind</code>. The foundation lives only in zlake so far. "
	"Nothing runs yet."))

# Act II ------------------------------------------------------------------

act("II", "A container", p(
	"The admin wants a web server: nginx, in a jail, on this "
	"foundation."))

st.add("C/cn")

NGINX_CONF = r'''
# containers/nginx.conf
foundation = "generic";

pkg = [ "nginx" ];

datasets {
    www {}
}

commands {
    nginx = <<EOD
sysrc nginx_enable=YES
EOD
}

jailconf = <<EOD
host.hostname = "nginx";
ip4.addr = "10.0.0.5";
EOD
'''

NGINX_SHOW = r'''
$ shtetl config show containers/nginx
kind         container
foundation   generic -> generic-15.1-releng, newest version v1
datasets     tmp         from the foundation    default.conf:4
             usr         from the foundation    (placeholder)
             usr/local   from the foundation    default.conf:6
             var         from the foundation    default.conf:5
             www         container-only         containers/nginx.conf:7
pkg          nginx                              containers/nginx.conf:4
commands     nginx                              containers/nginx.conf:11
derivations  (4, as the foundation)             default.conf
jailconf     2 lines                            containers/nginx.conf:16
'''

frame("The container recipe, checked", diagram=False,
    before=p(
	"<code>containers/nginx.conf</code> names its foundation, one package, "
	"one dataset of its own, one command block and its jail settings. "
	"<code>shtetl create nginx</code> checks it the same three ways, plus "
	"the checks that only make sense for a container."),
    panel=codes(("containers/nginx.conf (new)", NGINX_CONF)) + checks(
	"container checks", [
	("ok", "parse and schema as before; <code>jailconf</code> and "
	    "<code>foundation</code> are container keys"),
	("ok", "<code>foundation = \"generic\"</code> names a recipe whose "
	    "foundation exists: <code>generic-15.1-releng</code>, newest "
	    "version v1"),
	("ok", "<code>requires</code>: none, so no other container must "
	    "exist first"),
	("ok", "datasets: <code>tmp</code>, <code>usr</code>, "
	    "<code>usr/local</code>, <code>var</code> exist in the foundation "
	    "and will be clones; <code>www</code> is container-only"),
	("ok", "derivation sources exist in v1 (<code>etc/master.passwd"
	    "</code>, <code>etc/login.conf</code>)"),
	("ok", "the name <code>nginx</code> is free on zrun and in the zlake "
	    "mirror"),
	("prop", "a container recipe may not drop a dataset its foundation "
	    "splits (<code>tmp = false</code> would leave <code>/tmp</code> a "
	    "plain directory with nothing to rebase), nor a package its "
	    "foundation installed: both are errors"),
    ]) + code(NGINX_SHOW),
    after=callout("finding", "Finding: two keys the schema lacked",
	p("The plan's recipe keys had no way for a container to name its "
	"foundation; <code>foundation</code> is now in planning.md 5.5. The "
	"proposed rule above (no dropping what the foundation provides) is "
	"open question Q-F.")))

st.add(*T("R/f/G"))
for r in T("R/f/G"):
	st.tag(r, "ro")
st.tag("R/f/G/usr", "ro, canmount=off")
st.snap(T("R/f/G"), "v1")
frame("Bring the foundation to zrun",
    before=p(
	"Clones cannot cross pools, so the foundation first goes where "
	"containers run."),
    hl=dict({r: "new" for r in T("R/f/G")}, **{"C/cn": "read"}),
    arrows=[A(a, b, "send", 1 if i == 0 else None)
	for i, (a, b) in enumerate(zip(T("L/f/G"), T("R/f/G")))],
    after=p(
	"<b>1</b> One <code>zfs send</code> per dataset of v1, received under "
	"<code>fast/shtetl/run/foundations</code> with <code>readonly=on</code> "
	"and <code>mountpoint=none</code>. A full stream this first time; "
	"every later version goes as an incremental."))

st.add(*T("R/c/n", TREEW))
st.mounted |= set(T("R/c/n", ["", "/tmp", "/usr/local", "/var", "/www"]))
st.tag("R/c/n", "/containers/nginx")
st.tag("R/c/n/usr", "canmount=off")
st.tag("R/c/n/www", "container-only")
frame("Clone, dataset by dataset",
    before=p(
	"<code>zfs clone</code> is not recursive, so shtetl clones each "
	"dataset of v1 to the matching name under "
	"<code>containers/nginx</code>, then creates the recipe's own "
	"<code>www</code>."),
    hl={r: "new" for r in T("R/c/n", TREEW)},
    arrows=[A(a, b, "clone", 1 if i == 0 else None, side="R",
	bulge=22 + 9 * i) for i, (a, b) in enumerate(zip(T("R/f/G"),
	T("R/c/n")))],
    after=p(
	"<b>1</b> <code>zfs clone</code> of each <code>@v1</code>. Nothing is "
	"copied: the clones share every block with the foundation until "
	"something writes. Mountpoints are inherited, so the tree mounts at "
	"<code>/containers/nginx</code>, <code>/containers/nginx/var</code>, "
	"and so on, with no fstab. <code>www</code> has no origin: its "
	"history will start at the first save."))

st.tag("R/c/n", "/containers/nginx  jail")
frame("Start the jail, install, run the commands",
    hl={"R/c/n": "write", "R/c/n/usr/local": "write", "R/c/n/var": "write"},
    before=p(
	"shtetl writes the jail's block to <code>/etc/jail.conf.d/nginx.conf"
	"</code> on the host, from the recipe's <code>jailconf</code> and the "
	"path, and starts it."),
    panel=code(r'''
nginx {
    path = "/containers/nginx";
    host.hostname = "nginx";
    ip4.addr = "10.0.0.5";
    exec.start = "/bin/sh /etc/rc";
    exec.stop = "/bin/sh /etc/rc.shutdown";
}

service jail start nginx
pkg -j nginx install -y nginx            # usr/local, and var/db/pkg in var
jexec nginx /bin/sh -c 'sysrc nginx_enable=YES'   # etc/rc.conf in nginx
'''),
    after=p(
	"Commands run once, here, at creation. They never run again: an "
	"update carries what they did along with everything else."))

frame("Life happens",
    hl={"R/c/n": "write", "R/c/n/usr/local": "write", "R/c/n/var": "write",
	"R/c/n/www": "write"},
    before=p(
	"Weeks of ordinary administration. Each change lands in whichever "
	"dataset owns its path:"),
    panel=code(r'''
pw useradd ploni -m             etc/master.passwd etc/passwd etc/group
                                etc/pwd.db etc/spwd.db     -> nginx
vi /etc/ssh/sshd_config         PermitRootLogin no         -> nginx
vi /usr/local/etc/nginx/nginx.conf                         -> nginx/usr/local
site content                                               -> nginx/www
logs, pkg database                                         -> nginx/var
'''),
    after=p(
	"None of this is in any recipe, and none of it needs to be. There is "
	"no drift tracking and nothing to classify: the datasets are the "
	"record."))

st.snap(T("R/c/n", TREEW), "s1")
st.add(*T("L/c/n", TREEW))
st.snap(T("L/c/n", TREEW), "s1")
frame("Save: s1",
    before=p("<code>shtetl save nginx -m \"site live, ploni added\"</code>"),
    hl=dict({r: "snap" for r in T("R/c/n", TREEW)},
	**{r: "new" for r in T("L/c/n", TREEW)}),
    arrows=[A("R/c/n", "L/c/n", "save", 1),
	A("R/c/n/usr/local", "L/c/n/usr/local", "save"),
	A("R/c/n/www", "L/c/n/www", "save", 2)],
    panel=code(r'''
zfs snapshot -r -o shtetl:kind=save -o shtetl:commit="site live, ploni added" \
    fast/shtetl/run/containers/nginx@save-2026-10-09-101500
'''),
    after=p(
	"<b>1</b> Each foundation-inherited dataset goes as a clone stream "
	"(<code>zfs send -i &lt;run foundation&gt;@v1 nginx@s1</code>) and is "
	"received in zlake as a clone of zlake's own foundation "
	"<code>@v1</code>, so zlake stores only what nginx changed. One arrow "
	"per kind is drawn; every dataset of the tree goes the same way. "
	"<b>2</b> <code>www</code> has no origin, so its first save is a full "
	"stream. From now on zlake mirrors nginx as of its last save: same "
	"names, same clone relationships, one pool over. Whether the clone "
	"streams land as clones without help is probe P1.",
	"This snapshot is <b>s1</b>; its message travels in "
	"<code>shtetl:commit</code> (whether <code>zfs send</code> carries it "
	"is probe P10)."))

# Act III -----------------------------------------------------------------

act("III", "A new foundation version", p(
	"Five weeks later FreeBSD publishes an errata notice for 15.1. The fix "
	"lands on <code>releng/15.1</code>. For this story, suppose it also "
	"changed <code>/etc/ssh/sshd_config</code> and added a system user to "
	"<code>/etc/master.passwd</code> and <code>/etc/group</code>: exactly "
	"the files the admin edited too."))

PLAN2 = r'''
$ shtetl build generic
foundation generic-15.1-releng, update: v1 -> @2026-11-12-5b2c7e01a9d4

  recipe   unchanged checks pass (parse, schema, meaning)
  datasets same as v1: tmp usr usr/local var (none added or dropped)
  src      fast/shtetl/src: pull releng/15.1 (0f691888dc56 -> 5b2c7e01a9d4)
  build    buildworld buildkernel, obj reused from foundation-builds
  install  into the same datasets, then delete-old delete-old-libs
  snapshot -r @2026-11-12-5b2c7e01a9d4  shtetl:kind=version

proceed? [y/N] y
'''

frame("Rebuild the same foundation", diagram=False,
    before=p(
	"The admin runs the same command. Because "
	"<code>generic-15.1-releng</code> exists, this is an update of it, and "
	"there is one more check: the recipe's datasets must match the ones "
	"v1 has, because every container's datasets are clones of those."),
    panel=code(PLAN2),
    after=callout("note", "Why the same datasets",
	p("Building v2 into the datasets v1 lives in keeps every version on "
	"one snapshot chain. zfs_rebase needs that to find a base, and "
	"zlake can then send zrun an incremental instead of a whole world.")) +
	callout("finding", "Finding: a new release is a new foundation",
	p("If the recipe moved to <code>releng/15.2</code>, the name would "
	"become <code>generic-15.2-releng</code>: a new dataset with no "
	"lineage shared with 15.1, so no container could be rebased onto it. "
	"Moving between releases needs its own answer (perhaps building the "
	"new release into a clone of the old one's newest version). Not part "
	"of this story; it is open question Q-G.")))

st.mounted |= set(["B", "L/fb/G"] + T("L/f/G", ["", "/tmp", "/usr/local", "/var"]))
st.tag("B/t", "5b2c7e01a9d4")
frame("Build and install over v1",
    hl={"B/t": "write", "L/fb/G": "write", "L/f/G": "write",
	"L/f/G/var": "write"},
    arrows=[A("B/t", "L/fb/G", "build", 1),
	A("L/fb/G", "L/f/G", "install", 2, side="R", bulge=34)],
    before=p("The same mounts as before; the obj cache is already warm."),
    after=p(
	"<b>1</b> <code>buildworld buildkernel</code>, reusing the obj tree. "
	"<b>2</b> <code>installworld installkernel distribution</code> over "
	"v1, then <code>make delete-old delete-old-libs DESTDIR=...</code> to "
	"remove anything the new build no longer installs.",
	"Known cost: installworld rewrites every file, so to zfs_rebase every "
	"file of the foundation looks changed between v1 and v2 and is "
	"decided on content. <code>install -C</code> leaves a byte-identical "
	"file untouched (FreeBSD already installs libraries that way); using "
	"it for everything would let the rebase skip unchanged files without "
	"reading them (probe P12)."))

st.mounted -= set(["B", "L/fb/G"] + T("L/f/G"))
st.snap(T("L/f/G"), "v2")
frame("Snapshot: version v2",
    hl={r: "snap" for r in T("L/f/G")},
    after=p(
	"<code>@2026-11-12-5b2c7e01a9d4</code>, called <b>v2</b> from here on. "
	"zlake's foundation now carries both versions on one chain."))

st.snap(T("R/f/G"), "v2")
frame("Send v2 to zrun",
    hl={r: "snap" for r in T("R/f/G")},
    arrows=[A(a, b, "send", 1 if i == 0 else None)
	for i, (a, b) in enumerate(zip(T("L/f/G"), T("R/f/G")))],
    after=p(
	"<b>1</b> <code>zfs send -i @v1 @v2</code> per dataset: only the "
	"blocks that changed. nginx is untouched; it is still a clone of "
	"<code>@v1</code> and still running."))

# Act IV ------------------------------------------------------------------

act("IV", "The update", p(
	"The admin moves nginx onto v2. Nothing in this act writes to the "
	"running nginx: the update is built beside it, tried, and only then "
	"swapped in, by the admin's decision."))

PREFLIGHT = r'''
$ shtetl update nginx
container    nginx on generic-15.1-releng@v1 -> v2 (2026-11-12-5b2c7e01a9d4)
recipe       containers/nginx.conf: checks pass
foundation   v2 is on zrun; its datasets match v1's
staging      fast/shtetl/run/containers/nginx-staging: free
rebases      none open for nginx
plan         rebase  nginx, tmp, usr, usr/local, var  (parent first)
             clone   www                               (container-only)

WARNING: nginx is running. The update starts from a snapshot taken now;
anything nginx writes after it will not be in nginx-staging.

proceed? [y/N] y
'''

frame("Preflight", diagram=False,
    before=p(
	"An update needs nothing from nginx but a snapshot to start from. "
	"nginx may be running, saved or not."),
    panel=code(PREFLIGHT))

st.snap(T("R/c/n", TREEW), "s2")
frame("The from-snapshot: s2",
    hl={r: "snap" for r in T("R/c/n", TREEW)},
    panel=code(r'''
zfs snapshot -r -o shtetl:kind=save \
    -o shtetl:commit="update to generic-15.1-releng@2026-11-12-5b2c7e01a9d4" \
    fast/shtetl/run/containers/nginx@save-2026-11-12-090000
'''),
    after=p(
	"An ordinary save snapshot, atomic across the whole tree, called "
	"<b>s2</b> (orange: the update starts here). nginx keeps serving."))


def lineage_svg():
	return r'''<svg viewBox="0 0 760 300" role="img" aria-label="How zfs_rebase finds the base">
<defs><marker id="m-lin" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path class="mk mk-muted" d="M0,0 L10,5 L0,10 z"/></marker></defs>
<text class="rds" x="20" y="44">foundations/generic-15.1-releng</text>
<line class="lin" x1="40" y1="70" x2="700" y2="70"/>
<circle class="ldot ldot-version" cx="180" cy="70" r="9"/>
<text class="ltext" x="180" y="100" text-anchor="middle">v1</text>
<circle class="ldot ldot-version" cx="560" cy="70" r="9"/>
<text class="ltext" x="560" y="100" text-anchor="middle">v2</text>
<path class="lin" d="M180,79 C180,150 220,170 260,170 L470,170"/>
<text class="rds" x="20" y="174">containers/nginx</text>
<circle class="ldot ldot-save" cx="320" cy="170" r="8"/>
<text class="ltext" x="320" y="198" text-anchor="middle">s1</text>
<circle class="ldot ldot-from" cx="440" cy="170" r="8"/>
<text class="ltext" x="440" y="198" text-anchor="middle">s2</text>
<path class="lin lin-new" d="M560,79 C560,200 600,250 640,250 L700,250"/>
<text class="rds" x="20" y="254">containers/nginx-staging (the result)</text>
<circle class="ring ring-base" cx="180" cy="70" r="15"/>
<text class="ltag ltag-base" x="180" y="38" text-anchor="middle">BASE</text>
<circle class="ring ring-onto" cx="560" cy="70" r="15"/>
<text class="ltag ltag-onto" x="560" y="38" text-anchor="middle">ONTO</text>
<circle class="ring ring-from" cx="440" cy="170" r="14"/>
<text class="ltag ltag-from" x="440" y="142" text-anchor="middle">FROM</text>
</svg>'''


frame("How zfs_rebase finds the base", diagram=False,
    before=p(
	"shtetl names two snapshots and never the base. For the top dataset "
	"of the tree:"),
    panel=code(r'''
zfs_rebase --from fast/shtetl/run/containers/nginx@save-2026-11-12-090000 \
    --onto fast/shtetl/run/foundations/generic-15.1-releng@2026-11-12-5b2c7e01a9d4 \
    --result fast/shtetl/run/containers/nginx-staging
''') + '<div class="diagram">%s</div>' % lineage_svg(),
    after=p(
	"The tool walks each side's origin chain. From's: <code>nginx@s2"
	"</code>, then nginx's origin, <code>generic-15.1-releng@v1</code>, "
	"then that dataset. Onto's: <code>generic-15.1-releng@v2</code>, then "
	"that same dataset. The nearest dataset both descend from is the "
	"foundation, and of the two snapshots the walks name there, v1 and "
	"v2, the older is the base: <b>v1</b>. So the rebase replays "
	"everything nginx changed since v1 onto v2, and nothing else. This is "
	"why the pre-split in Act I mattered: <code>nginx/var</code> finds its "
	"base the same way, through <code>generic-15.1-releng/var</code>."))

st.add("R/c/ns")
st.tag("R/c/ns", "private mount, exit 1")
frame("Rebase the top dataset",
    hl={"R/c/ns": "new"},
    arrows=[A("R/f/G", "R/c/ns", "rebase", 1, side="R", bulge=48, t=0.3),
	A("R/c/n", "R/c/ns", "rebase", 2, side="R", bulge=24, t=0.5)],
    before=p(
	"Parent first. zfs_rebase clones <b>1</b> onto (v2) as "
	"<code>nginx-staging</code>, read-only to the world and mounted only "
	"at its own private directory, and replays <b>2</b> from's changes "
	"(s2 against the base v1) into it."),
    after=p(
	"Every clean action is applied first: the admin's nginx.conf, rc.conf "
	"line, home directory and so on, and every file the errata changed "
	"that the admin did not. Then the tool stops at its conflicts gate "
	"and exits 1. Six names were changed on both sides: "
	"<code>etc/group</code>, <code>etc/master.passwd</code>, "
	"<code>etc/passwd</code>, <code>etc/pwd.db</code>, "
	"<code>etc/spwd.db</code>, <code>etc/ssh/sshd_config</code>. The tool "
	"merges nothing by itself, however far apart the two edits are."))

RES0 = r'''
#rebase-resolution 5
#base fast/shtetl/run/foundations/generic-15.1-releng@2026-10-08-0f691888dc56 ...
#from fast/shtetl/run/containers/nginx@save-2026-11-12-090000 ...
#onto fast/shtetl/run/foundations/generic-15.1-releng@2026-11-12-5b2c7e01a9d4 ...
#mode strict
#names 6
#unanswered 6
/
    etc/
        group conflict 1 -
        master.passwd conflict 2 -
        passwd conflict 3 -
        pwd.db conflict 4 -
        spwd.db conflict 5 -
        ssh/
            sshd_config conflict 6 -
            ..
        ..
    ..
'''

RES1 = r'''
#unanswered 3
...
        group conflict 1 -
        master.passwd conflict 2 -
        passwd conflict 3 keep        <- derivation, regenerated later
        pwd.db conflict 4 keep        <- derivation, regenerated later
        spwd.db conflict 5 keep       <- derivation, regenerated later
        ...
            sshd_config conflict 6 -
'''

frame("At the gate: the derived files step aside", diagram=False,
    before=p(
	"The tool has written a resolution document beside its manifest: one "
	"line per conflicted name, each waiting for "
	"<code>keep</code>, <code>onto</code> or <code>from</code>. (Shown "
	"schematically; the grammar is v4-manifest.md section 8.)"),
    panel=codes(("the resolution, as the tool wrote it", RES0),
	("after shtetl's pass", RES1)),
    after=p(
	"shtetl matches the conflicted names against the recipe's "
	"<code>derivations</code>. <code>passwd</code>, <code>pwd.db</code> and "
	"<code>spwd.db</code> are all generated from "
	"<code>master.passwd</code>, so nobody should have to choose between "
	"two versions of them: shtetl answers them <code>keep</code> and "
	"remembers to regenerate them. But it cannot regenerate them yet, "
	"because their source is itself still in conflict.") +
	callout("finding", "Finding: the text has to be resolved first",
	p("A derived file almost never conflicts alone: whoever changed the "
	"binary changed its source too, and a source changed on both sides "
	"is itself a conflict, since zfs_rebase never merges text on its "
	"own. So the derivation can only run after the source has been "
	"merged. Today that takes two passes at the gate (this frame and the "
	"next two). With the merge drivers already designed in "
	"future-features.md, a headless text driver that accepts only "
	"conflict-free merges, followed by the derivation rules in the "
	"order written, would do it all before the picker opens.")))

PICKER = r'''
 nginx-staging                          3 of 6 unanswered
   etc/group               conflict   -
   etc/master.passwd       conflict   -
   etc/passwd              conflict   keep
   etc/pwd.db              conflict   keep
   etc/spwd.db             conflict   keep
   etc/ssh/sshd_config     conflict   -
'''

frame("At the gate: the admin merges the text", diagram=False,
    before=p("shtetl opens the picker and holds the rebase at the gate:"),
    panel=code("zfs_rebase --continue -i --no-merge "
	"fast/shtetl/run/containers/nginx-staging") +
	code(PICKER, "sketch") + p("(a sketch of the list screen, not the "
	"tool's exact drawing)"),
    after=p(
	"<code>etc/group</code> and <code>etc/master.passwd</code>: the admin "
	"opens each into the merge screen. The errata added a system user "
	"near the top, the admin added <code>ploni</code> at the end; the "
	"three-way merge has no conflict chunk, so <code>w</code> writes the "
	"merged file into the tree and the line becomes <code>keep</code>.",
	"<code>etc/ssh/sshd_config</code>: the errata rewrote the commented "
	"<code>#PermitRootLogin</code> default, the very line the admin had "
	"replaced with <code>PermitRootLogin no</code>. That is one conflict "
	"chunk; the admin picks their own side for it, the errata's other "
	"changes to the file come along as onto-only chunks, <code>w</code> "
	"writes, and the line becomes <code>keep</code>.",
	"The picker exits. <code>--no-merge</code> means the gate stands even "
	"though the document is complete, which is what shtetl needs for the "
	"next step."))

frame("At the gate: regenerate, then finish", diagram=False,
    before=p(
	"Now the source is merged in the tree, so the derivation can run. "
	"The three conflicted outputs share one command, so it runs once, in "
	"a chroot of the result's private mount:"),
    panel=code(r'''
chroot <private mount of nginx-staging> pwd_mkdb -p -d /etc /etc/master.passwd

zfs_rebase --continue fast/shtetl/run/containers/nginx-staging
    applying2 ... done, verified: exit 0
'''),
    after=p(
	"<code>passwd</code>, <code>pwd.db</code> and <code>spwd.db</code> now "
	"agree with the merged <code>master.passwd</code>, and their lines "
	"already say <code>keep</code>, which is exactly \"leave what is in "
	"the tree\". <code>--continue</code> applies the rest, runs the final "
	"check and reaches done. The tool hands the result back the way it "
	"always does: unmounted, read-only, <code>mountpoint=none</code>. "
	"Where it goes is shtetl's business, not the tool's."))

st.tag("R/c/ns", "")
st.add(*T("R/c/ns", TREEW))
st.mounted |= set(T("R/c/ns", ["", "/tmp", "/usr/local", "/var", "/www"]))
st.tag("R/c/ns", "")
st.tag("R/c/ns/usr", "canmount=off")
st.tag("R/c/ns/www", "clone of nginx/www@s2")
st.snap(T("R/c/ns", TREEW), "v2")
frame("The children, www, and placing the tree",
    hl={r: "new" for r in T("R/c/ns", TREEW)},
    arrows=[A("R/f/G/var", "R/c/ns/var", "rebase", 1, side="R", bulge=56,
	t=0.25),
	A("R/c/n/var", "R/c/ns/var", "rebase", None, side="R", bulge=34),
	A("R/c/n/www", "R/c/ns/www", "clone", 2, side="R", bulge=20)],
    before=p(
	"<b>1</b> The children rebase the same way, each into the matching "
	"name under <code>nginx-staging</code>: <code>tmp</code>, "
	"<code>usr</code>, <code>usr/local</code> and <code>var</code> (one "
	"drawn; each derives its own base through the foundation's matching "
	"dataset). None conflicts: the errata did not touch what nginx "
	"changed there, so each exits 0. Logs and the pkg database in "
	"<code>var</code> are replayed onto the new base's <code>var</code>."),
    after=p(
	"<b>2</b> <code>www</code> has no foundation counterpart and nothing "
	"in a foundation update touches it, so its whole \"rebase\" is "
	"<code>zfs clone nginx/www@s2 nginx-staging/www</code>.",
	"Then shtetl places the tree (<code>zfs inherit mountpoint</code>, "
	"<code>readonly=off</code>) so it mounts at "
	"<code>/containers/nginx-staging</code>, and snapshots it with the "
	"version it now sits on: <code>@2026-11-12-5b2c7e01a9d4</code> "
	"(<b>v2</b>, <code>shtetl:kind=version</code>). Whether a rebase may "
	"create its result under another rebase's result is probe P4."))

st.tag("R/c/ns", "jail")
st.snap(T("L/c/n", TREEW), "s2")
st.add(*T("L/c/ns", TREEW))
st.snap(T("L/c/ns", TREEW[:-1]), "v2")
st.snap(T("L/c/ns", TREEW), "s3")
st.snap(T("R/c/ns", TREEW), "s3")
frame("Soak: old and new side by side",
    hl=dict({r: "new" for r in T("L/c/ns", TREEW)},
	**{"R/c/ns": "write"}),
    arrows=[A("R/c/n", "L/c/n", "save", 1),
	A("R/c/ns", "L/c/ns", "save", 2),
	A("L/c/n/www", "L/c/ns/www", "clone", 3, side="L", bulge=26)],
    before=p(
	"shtetl writes a jail block for <code>nginx-staging</code> and starts "
	"it. Both jails run. The admin tries the new one for as long as they "
	"like; a <code>shtetl pkg-upgrade nginx-staging</code> here would add "
	"a <code>-pkg</code> snapshot, so the plain and upgraded states would "
	"both be there to pick from."),
    after=p(
	"Saving the staging tree mirrors it into zlake. <b>1</b> First "
	"nginx's from-snapshot s2 goes to zlake if it is not there yet "
	"(<code>send -i s1 s2</code>), because <b>3</b> the mirror of "
	"<code>nginx-staging/www</code> is a clone of zlake's "
	"<code>nginx/www@s2</code>, exactly as on zrun. <b>2</b> The rebased "
	"datasets go as clone streams of zlake's foundation <code>@v2</code>. "
	"The staging save is <b>s3</b>, \"soak start\".") +
	callout("question", "Open question Q-C",
	p("Both jails want <code>10.0.0.5</code>. How does the staging jail "
	"reach the network while the old one holds the address and ports: "
	"an address from a pool in shtetl.conf, its own vnet, or not started "
	"automatically?")))

CUTWARN = r'''
$ shtetl cutover nginx
nginx was written after the update started (written@s2):
    nginx/var     1.4M     (logs)
    nginx/www       0
    nginx           0
These changes are not in nginx-staging. They will be kept in the archive.
cut over? [y/N] y
'''

st.snap(T("R/c/n", TREEW), "s4")
st.snap(T("L/c/n", TREEW), "s4")
st.tag("R/c/n", "/containers/nginx  stopped")
st.tag("R/c/ns", "stopped")
frame("Cutover on zrun",
    before=p(
	"The soak went well. Cutover is the admin's decision, made by "
	"command."),
    panel=code(CUTWARN),
    hl=dict({r: "gone" for r in T("R/c/n", TREEW)},
	**{"R/c/ns/www": "write"}),
    arrows=[A("R/c/n", "L/c/n", "save", 1),
	A("R/c/n/www", "R/c/ns/www", "promote", 2, side="R", bulge=18),
	A("R/c/ns", "R/c/n", "rename", 3, side="R", bulge=40)],
    after=p(
	"shtetl stops both jails. <b>1</b> A final save of the old nginx, "
	"<b>s4</b>, \"final before cutover\", goes to zlake, so the archive "
	"will hold what it wrote after s2. <b>2</b> "
	"<code>zfs promote nginx-staging/www</code>: the clone takes over "
	"<code>www</code>'s snapshots up to s2, so the site's history moves "
	"with it, and the old <code>www</code> is left holding only s4 "
	"(probe P11). Then the old tree is destroyed on zrun; it lives on in "
	"zlake. <b>3</b> <code>zfs rename nginx-staging nginx</code>. "
	"Mountpoints follow by inheritance, so the new tree is now at "
	"<code>/containers/nginx</code>; the jail block is renamed and the "
	"jail started."))

promoted = ["s1", "s2"] + st.pills.get("R/c/ns/www", [])
st.drop(*T("R/c/n", TREEW))
st.rename("R/c/ns", "R/c/n", TREEW)
st.pills["R/c/n/www"] = promoted
st.tag("R/c/n", "/containers/nginx  jail")
st.tag("R/c/n/www", "promoted")

# zlake, first move: the mirror's www is promoted the same way, then the
# old mirror becomes the archive.
st.pills["L/c/ns/www"] = ["s1", "s2"] + st.pills.get("L/c/ns/www", [])
st.pills["L/c/n/www"] = ["s4"]
st.add(*T("L/a/c/n", TREEW))
for s in TREEW:
	st.pills["L/a/c/n" + s] = list(st.pills.get("L/c/n" + s, []))
frame("Cutover in zlake: the old mirror is archived",
    hl=dict({r: "move" for r in T("L/c/n", TREEW)},
	**dict({r: "new" for r in T("L/a/c/n", TREEW)},
	**{"L/c/ns/www": "write"})),
    arrows=[A("L/c/n", "L/a/c/n", "rename", 1, side="L", bulge=40)],
    before=p(
	"zlake follows, so its mirror matches zrun again. First the same "
	"promote of <code>www</code> in the mirror, so zlake's "
	"<code>nginx-staging/www</code> now carries s1 and s2 as well."),
    after=p(
	"<b>1</b> <code>zfs rename</code> of the old mirror to "
	"<code>archive/containers/nginx-2026-11-20</code>. Same pool, so it "
	"is a rename and copies nothing. The archive keeps the old tree "
	"whole, s4 and its 1.4M of logs included, until "
	"<code>retention.archive</code> (one year here) prunes it."))

st.drop(*T("L/c/n", TREEW))
st.rename("L/c/ns", "L/c/n", TREEW)
for s in TREEW:
	st.present.add("L/c/ns" + s)
	st.pills["L/c/ns" + s] = list(st.pills.get("L/c/n" + s, []))
frame("Cutover in zlake: the staging mirror becomes nginx",
    hl=dict({r: "move" for r in T("L/c/ns", TREEW)},
	**{r: "new" for r in T("L/c/n", TREEW)}),
    arrows=[A("L/c/ns", "L/c/n", "rename", 1, side="L", bulge=30)],
    after=p(
	"<b>1</b> <code>zfs rename</code> of <code>containers/nginx-staging"
	"</code> to <code>containers/nginx</code>, matching zrun. zlake's "
	"<code>nginx</code> is now a clone of zlake's foundation "
	"<code>@v2</code>, and its <code>www</code> carries the site's whole "
	"history.") +
	callout("note", "Drawn as done, still to be proven",
	p("These moves rest on ZFS behaviour this page assumes: that a clone "
	"stream lands as a clone (probe P1), that a child keeps receiving "
	"incremental sends after its parent changes lineage or name (P2), "
	"and that a promote can be mirrored across pools (P11). The probes "
	"run on the box before the design is relied on.")))
st.drop(*T("L/c/ns", TREEW))

st.tag("R/c/n/www", "")
st.snap(T("R/c/n", TREEW), "s5")
st.snap(T("L/c/n", TREEW), "s5")
LOG = r'''
$ shtetl log nginx
nginx                     on generic-15.1-releng@2026-11-12-5b2c7e01a9d4
  @save-2026-11-20-181500  cut over to 2026-11-12-5b2c7e01a9d4
  @save-2026-11-12-094500  soak start
  @2026-11-12-5b2c7e01a9d4 version
  www also carries         @save-2026-11-12-090000, @save-2026-10-09-101500
archive/containers/nginx-2026-11-20
                          on generic-15.1-releng@2026-10-08-0f691888dc56
  @save-2026-11-20-180000  final before cutover
  @save-2026-11-12-090000  update to generic-15.1-releng@2026-11-12-5b2c7e01a9d4
  @save-2026-10-09-101500  site live, ploni added
'''
frame("Where everything stands",
    hl={r: "snap" for r in T("R/c/n", TREEW) + T("L/c/n", TREEW)},
    arrows=[A("R/c/n", "L/c/n", "save", 1)],
    before=p(
	"The end of one cycle. nginx runs on v2 with everything the admin "
	"did, merged with everything the errata did. <b>1</b> Its first save "
	"after cutover, <b>s5</b>, \"cut over to 2026-11-12-5b2c7e01a9d4\", "
	"goes to the renamed mirror as an ordinary incremental. zlake holds "
	"the foundation's two versions, a mirror of nginx as of that save, "
	"and the old tree in the archive."),
    after=p(
	"The next errata starts again at Act III: build into the same "
	"foundation, send the increment, update into staging, soak, cut over. "
	"Nothing in the cycle needed git beyond the recipes repo, and nothing "
	"was written into a running tree except by the admin and the jail's "
	"own <code>pkg</code> and <code>commands</code> at creation.",
	"A proposal from planning.md section 8, for following a container "
	"across its updates:") + code(LOG) +
	callout("question", "Open question Q-E",
	p("How much of this history to keep: every archive forever (the "
	"retention default of 0), archives pruned by age as here, or only "
	"the live tree and the foundation chain?")))


# -- page ------------------------------------------------------------------

CSS = r'''
:root {
	--bg: #fbfaf7; --fg: #1d1c1a; --muted: #6b675f; --line: #d9d4ca;
	--panel: #f2efe8; --accent: #b4561a; --lake: #2f6690; --config: #6a4c93;
	--build: #8a4b14; --run: #2d6a4f; --rebase: #a3245e; --code-bg: #efebe2;
	--hl-new: #d5ecd9; --hl-write: #f6e2b8; --hl-read: #e6dcf2;
	--hl-snap: #d6e6f3; --hl-gone: #f3d3d0; --hl-move: #e4e1da;
	--ok: #2d6a4f; --warn: #a06000; --fail: #a3245e; --prop: #6a4c93;
}
@media (prefers-color-scheme: dark) {
	:root:not([data-theme="light"]) {
		--bg: #161513; --fg: #ebe7df; --muted: #a39e94; --line: #3a3732;
		--panel: #201f1c; --accent: #e8955a; --lake: #7fb3d9;
		--config: #b9a0dc; --build: #e0a060; --run: #7cc4a0;
		--rebase: #e57fae; --code-bg: #24221f;
		--hl-new: #1f3a28; --hl-write: #43361a; --hl-read: #2f2840;
		--hl-snap: #1c3044; --hl-gone: #46231f; --hl-move: #2e2c28;
		--ok: #7cc4a0; --warn: #e0a060; --fail: #e57fae; --prop: #b9a0dc;
	}
}
:root[data-theme="dark"] {
	--bg: #161513; --fg: #ebe7df; --muted: #a39e94; --line: #3a3732;
	--panel: #201f1c; --accent: #e8955a; --lake: #7fb3d9;
	--config: #b9a0dc; --build: #e0a060; --run: #7cc4a0;
	--rebase: #e57fae; --code-bg: #24221f;
	--hl-new: #1f3a28; --hl-write: #43361a; --hl-read: #2f2840;
	--hl-snap: #1c3044; --hl-gone: #46231f; --hl-move: #2e2c28;
	--ok: #7cc4a0; --warn: #e0a060; --fail: #e57fae; --prop: #b9a0dc;
}
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { margin: 0; background: var(--bg); color: var(--fg);
	font: 16px/1.6 Georgia, "Iowan Old Style", "Palatino Linotype", serif; }
main { max-width: 1060px; margin: 0 auto; padding: 32px 16px 96px; }
h1, h2, h3, .fnum, .ctitle, .fname, th {
	font-family: "Helvetica Neue", Arial, sans-serif; }
h1 { font-size: 2.2rem; margin: 0 0 4px; }
.sub { color: var(--muted); margin: 0 0 20px; }
p, li { max-width: 74ch; }
code, pre { font-family: "SF Mono", Menlo, Consolas, monospace;
	font-size: 0.85em; }
code { background: var(--code-bg); padding: 1px 4px; border-radius: 3px; }
pre { background: var(--code-bg); padding: 12px 14px; border-radius: 6px;
	overflow-x: auto; line-height: 1.45; margin: 10px 0 14px; }
pre code { background: none; padding: 0; }
pre.sketch { border: 1px dashed var(--line); }
.toc { display: flex; flex-wrap: wrap; gap: 8px 18px; margin: 8px 0 24px;
	font-family: "Helvetica Neue", Arial, sans-serif; font-size: 0.95rem; }
.toc a { color: var(--accent); text-decoration: none; }
.legend { display: flex; flex-wrap: wrap; gap: 6px 14px; margin: 10px 0 18px;
	font-family: "Helvetica Neue", Arial, sans-serif; font-size: 0.85rem; }
.legend span { display: inline-flex; align-items: center; gap: 6px; }
.sw { display: inline-block; width: 22px; height: 12px; border-radius: 3px; }
.sw-new { background: var(--hl-new); } .sw-write { background: var(--hl-write); }
.sw-read { background: var(--hl-read); } .sw-snap { background: var(--hl-snap); }
.sw-gone { background: var(--hl-gone); } .sw-move { background: var(--hl-move); }
.dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%;
	background: var(--fg); }
.pillsw { display: inline-block; padding: 0 7px; border-radius: 8px;
	font: 11px "SF Mono", Menlo, monospace; color: var(--bg); }
.cast { width: 100%; border-collapse: collapse; font-size: 0.93rem;
	margin: 8px 0 20px; }
.cast td, .cast th { text-align: left; vertical-align: top; padding: 5px 8px;
	border-bottom: 1px solid var(--line); }
.cast th { color: var(--muted); font-size: 0.82rem; font-weight: 600; }
.tablewrap { overflow-x: auto; }
.act { margin: 72px 0 8px; padding-top: 20px; border-top: 3px solid var(--fg); }
.act h2 { font-size: 1.6rem; margin: 0 0 6px; }
.act .anum { font-family: "Helvetica Neue", Arial, sans-serif; color: var(--muted);
	font-size: 0.9rem; letter-spacing: 0.08em; text-transform: uppercase; }
.frame { margin: 44px 0 0; }
.fnum { color: var(--muted); font-size: 0.8rem; letter-spacing: 0.06em;
	text-transform: uppercase; }
.frame h3 { font-family: "Helvetica Neue", Arial, sans-serif; font-size: 1.2rem;
	margin: 2px 0 8px; }
.diagram { background: var(--panel); border: 1px solid var(--line);
	border-radius: 8px; padding: 10px; margin: 12px 0; overflow-x: auto; }
.diagram svg { display: block; width: 100%; min-width: 760px; height: auto; }
.codes { display: grid; grid-template-columns: repeat(auto-fit,
	minmax(min(100%, 320px), 1fr)); gap: 12px; margin: 10px 0; }
.codebox { min-width: 0; }
.fname { font-size: 0.8rem; color: var(--muted); margin: 0 0 -4px; }
.checks { border: 1px solid var(--line); border-radius: 6px; padding: 8px 12px;
	margin: 10px 0; background: var(--panel); }
.checks .ctitle { font-size: 0.82rem; color: var(--muted); text-transform: uppercase;
	letter-spacing: 0.05em; margin-bottom: 4px; }
.checks ul { list-style: none; padding: 0; margin: 0; }
.checks li { display: grid; grid-template-columns: 52px 1fr; gap: 8px;
	padding: 3px 0; max-width: none; font-size: 0.95rem; }
.cs { font: 600 11px "SF Mono", Menlo, monospace; text-transform: uppercase;
	border-radius: 4px; padding: 2px 0; text-align: center; height: 18px;
	color: var(--bg); margin-top: 3px; }
.cs-ok { background: var(--ok); } .cs-warn { background: var(--warn); }
.cs-fail { background: var(--fail); } .cs-note { background: var(--muted); }
.cs-prop { background: var(--prop); }
.callout { border-left: 4px solid var(--c); background: var(--panel);
	padding: 8px 14px; margin: 14px 0; border-radius: 0 6px 6px 0; }
.callout .ctitle { color: var(--c); font-size: 0.85rem; font-weight: 700;
	text-transform: uppercase; letter-spacing: 0.04em; }
.callout p { margin: 6px 0; }
.callout-finding { --c: var(--accent); }
.callout-question { --c: var(--config); }
.callout-note { --c: var(--lake); }
svg text { font-family: "Helvetica Neue", Arial, sans-serif; fill: var(--fg); }
svg .box { fill: var(--bg); stroke-width: 2; }
svg .box-config { stroke: var(--config); } svg .box-build { stroke: var(--build); }
svg .box-lake { stroke: var(--lake); } svg .box-run { stroke: var(--run); }
svg .slabel { font-size: 15px; font-weight: 700; }
svg .slabel-config { fill: var(--config); } svg .slabel-build { fill: var(--build); }
svg .slabel-lake { fill: var(--lake); } svg .slabel-run { fill: var(--run); }
svg .ssub { font-size: 11px; fill: var(--muted); }
svg .rroot, svg .rds, svg .rfile { font-family: "SF Mono", Menlo, monospace;
	font-size: 12px; }
svg .rroot { font-weight: 700; }
svg .rfile { fill: var(--muted); font-style: italic; }
svg .rtag { font-size: 10.5px; fill: var(--muted); }
svg .mdot { fill: var(--fg); }
svg .hl-new { fill: var(--hl-new); } svg .hl-write { fill: var(--hl-write); }
svg .hl-read { fill: var(--hl-read); } svg .hl-snap { fill: var(--hl-snap); }
svg .hl-gone { fill: var(--hl-gone); } svg .hl-move { fill: var(--hl-move); }
svg .strike { stroke: var(--fail); stroke-width: 1.5; }
svg .pill { stroke: none; }
svg .pill-version { fill: var(--lake); } svg .pill-save { fill: var(--muted); }
svg .pill-from { fill: var(--accent); }
svg .pilltext { font-family: "SF Mono", Menlo, monospace; font-size: 10.5px;
	fill: var(--bg); font-weight: 700; }
svg .ar { fill: none; stroke-width: 1.8; }
svg .ar-build { stroke: var(--build); } svg .ar-lake { stroke: var(--lake); }
svg .ar-run { stroke: var(--run); } svg .ar-rebase { stroke: var(--rebase);
	stroke-width: 2.4; }
svg .ar-muted { stroke: var(--muted); }
svg .mk-build { fill: var(--build); } svg .mk-lake { fill: var(--lake); }
svg .mk-run { fill: var(--run); } svg .mk-rebase { fill: var(--rebase); }
svg .mk-muted { fill: var(--muted); }
svg .badge-build { fill: var(--build); } svg .badge-lake { fill: var(--lake); }
svg .badge-run { fill: var(--run); } svg .badge-rebase { fill: var(--rebase); }
svg .badge-muted { fill: var(--muted); }
svg .badgetext { font-size: 11px; font-weight: 700; fill: var(--bg); }
svg .lin { fill: none; stroke: var(--muted); stroke-width: 3; }
svg .lin-new { stroke: var(--run); stroke-dasharray: 6 4; }
svg .ldot { stroke: var(--bg); stroke-width: 2; }
svg .ldot-version { fill: var(--lake); } svg .ldot-save { fill: var(--muted); }
svg .ldot-from { fill: var(--accent); }
svg .ltext { font-family: "SF Mono", Menlo, monospace; font-size: 12px; }
svg .ring { fill: none; stroke-width: 2.5; }
svg .ring-base { stroke: var(--rebase); } svg .ring-onto { stroke: var(--lake); }
svg .ring-from { stroke: var(--accent); }
svg .ltag { font-size: 11px; font-weight: 700; letter-spacing: 0.06em; }
svg .ltag-base { fill: var(--rebase); } svg .ltag-onto { fill: var(--lake); }
svg .ltag-from { fill: var(--accent); }
@media (max-width: 560px) { h1 { font-size: 1.8rem; } }
'''

CAST = [
	("machine", "<code>kfar</code>: pools <code>fast</code> (NVMe, boot) "
	    "and <code>tank</code> (HDD)"),
	("zconfig", "<code>fast/shtetl/config</code>, the zshtetl-recipes "
	    "clone, at <code>/shtetl/config</code>"),
	("zbuild", "<code>fast/shtetl/src</code>, a freebsd-src checkout"),
	("zlake", "<code>tank/shtetl</code>"),
	("zrun", "<code>fast/shtetl/run</code>; containers at "
	    "<code>/containers</code>"),
	("F", "foundation <code>generic-15.1-releng</code>"),
	("v1", "<code>@2026-10-08-0f691888dc56</code>, first build"),
	("v2", "<code>@2026-11-12-5b2c7e01a9d4</code>, after an errata"),
	("s1", "<code>@save-2026-10-09-101500</code> \"site live, ploni "
	    "added\""),
	("s2", "<code>@save-2026-11-12-090000</code>, where the update starts"),
	("s3", "<code>@save-2026-11-12-094500</code> \"soak start\" "
	    "(staging)"),
	("s4", "<code>@save-2026-11-20-180000</code> \"final before "
	    "cutover\" (old)"),
	("s5", "<code>@save-2026-11-20-181500</code> \"cut over\" (new)"),
]


def page():
	out = ['<!doctype html>', '<html lang="en">', '<head>',
	    '<meta charset="utf-8">',
	    '<meta name="viewport" content="width=device-width, initial-scale=1">',
	    '<title>shtetl, one cycle</title>', '<style>', CSS, '</style>',
	    '</head>', '<body>', '<main>']
	out.append('<h1>shtetl, one cycle</h1>')
	out.append('<p class="sub">A foundation is built, a container is made '
	    'from it, and the container is moved onto the foundation\'s next '
	    'version: every operation, frame by frame. Draft of 2026-10-08, '
	    'following planning.md draft 2. Generated by '
	    '<code>tools/story.py</code>.</p>')
	toc = ['<nav class="toc">']
	for f in frames:
		if f["act"]:
			toc.append('<a href="#act-%s">Act %s: %s</a>'
			    % (f["num"], f["num"], esc(f["title"])))
	toc.append("</nav>")
	out.append("\n".join(toc))
	out.append('<div class="tablewrap"><table class="cast"><tr><th>name'
	    '</th><th>what it is in this story</th></tr>')
	for k, v in CAST:
		out.append("<tr><td><code>%s</code></td><td>%s</td></tr>" % (k, v))
	out.append("</table></div>")
	out.append('<div class="legend">'
	    '<span><span class="sw sw-new"></span>created</span>'
	    '<span><span class="sw sw-write"></span>written</span>'
	    '<span><span class="sw sw-read"></span>read</span>'
	    '<span><span class="sw sw-snap"></span>snapshotted</span>'
	    '<span><span class="sw sw-gone"></span>destroyed</span>'
	    '<span><span class="sw sw-move"></span>renamed away</span>'
	    '<span><span class="pillsw" style="background:var(--lake)">v1'
	    '</span>version</span>'
	    '<span><span class="pillsw" style="background:var(--muted)">s1'
	    '</span>save</span>'
	    '<span><span class="pillsw" style="background:var(--accent)">s2'
	    '</span>update starts here</span>'
	    '<span><span class="dot"></span>mounted</span>'
	    '</div>')
	n = 0
	for f in frames:
		if f["act"]:
			out.append('<section class="act" id="act-%s"><div class="anum">'
			    'Act %s</div><h2>%s</h2>%s</section>'
			    % (f["num"], f["num"], esc(f["title"]), f["intro"]))
			continue
		n += 1
		out.append('<section class="frame" id="f%d">' % n)
		out.append('<div class="fnum">Frame %d</div><h3>%s</h3>'
		    % (n, esc(f["title"])))
		out.append(f["before"])
		if f["panel"]:
			out.append(f["panel"])
		if f["diagram"]:
			out.append('<div class="diagram">%s</div>'
			    % svg_frame(f["st"], f["hl"], f["arrows"], f["title"], n))
		out.append(f["after"])
		out.append("</section>")
	out += ["</main>", "</body>", "</html>", ""]
	return "\n".join(out)


if __name__ == "__main__":
	sys.stdout.write(page())
