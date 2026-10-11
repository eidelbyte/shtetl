#!/usr/bin/env python3
#
# slides.py: generate docs/slides.html, the story of docs/story.html as
# slides beside one diagram that changes in place: datasets come and go,
# snapshots land on their rows, each step's arrows draw themselves, and
# the view moves to where the step happens. It imports story.py, which
# runs the story and keeps every frame's state, and reuses its geometry,
# so the diagram is the same one story.html draws. Edit the story in
# story.py; this script only stages it.
#
# usage: python3 tools/slides.py > docs/slides.html
#
# ASCII only. No libraries: SVG and the Web Animations API.

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import story  # noqa: E402  runs the story; writes nothing


def state_json(st):
	return {
		"present": sorted(st.present),
		"mounted": sorted(st.mounted & st.present),
		"pills": {k: v for k, v in st.pills.items() if k in st.present},
		"tags": {k: v for k, v in st.tags.items() if k in st.present},
	}


EMPTY = {"present": [], "mounted": [], "pills": {}, "tags": {}}

TITLE_HTML = story.p(
	"One machine, one foundation, one container. The diagram is the "
	"machine's four ZFS spaces, and every slide changes it in place, the "
	"way the cycle changes the datasets: a foundation is built, a "
	"container is made from it, and the container is moved onto the "
	"foundation's next version.",
	"Move with the buttons below, the arrow keys, or the marks along the "
	"bottom of the diagram. The view follows each step; drag or scroll "
	"the diagram to look around, <b>Whole map</b> pulls back, and "
	"<b>Follow the story</b> hands the view back.") + \
	'<div class="tablewrap"><table class="cast"><tr><th>name</th>' \
	'<th>what it is in this story</th></tr>' + "".join(
	"<tr><td><code>%s</code></td><td>%s</td></tr>" % (k, v)
	for k, v in story.CAST) + "</table></div>" + story.p(
	"The same story as one long page of diagrams is "
	"<a href=\"story.html\">story.html</a>.")


def build_slides():
	slides = [dict(kind="title", where="", title="shtetl, one cycle",
	    html=TITLE_HTML, state=EMPTY, hl={}, arrows=[], frame=0)]
	prev = EMPTY
	act = ""
	n = 0
	for f in story.frames:
		if f["act"]:
			act = story.actname(f["num"])
			slides.append(dict(kind="act", where=act, title=f["title"],
			    html=f["intro"], state=prev, hl={}, arrows=[], frame=0))
			continue
		n += 1
		st = state_json(f["st"])
		slides.append(dict(kind="frame",
		    where="%s, frame %d" % (act, n), title=f["title"],
		    html=f["before"] + f["panel"] + f["after"], state=st,
		    hl=f["hl"], frame=n,
		    arrows=[dict(a=a["a"], b=a["b"], kind=a["kind"],
		    num=a["num"], side=a["side"], bulge=a["bulge"], t=a["t"])
		    for a in f["arrows"]]))
		prev = st
	where = {}
	for i, s in enumerate(slides):
		if s["kind"] == "frame":
			where[s["title"]] = (s["frame"], i + 1)

	def link(m):
		num, slide = where[m.group(1)]
		return '<a href="#slide-%d" data-slide="%d">frame %d</a>' % (
		    slide, slide, num)
	for s in slides:
		s["html"] = re.sub(r"\{\{f:(.*?)\}\}", link, s["html"])
	return slides


def geometry():
	spaces = []
	rows = []
	for key in ("C", "B", "L", "R"):
		sp = story.SPACES[key]
		spaces.append(dict(key=key, label=sp.label, sub=sp.sub, x=sp.x,
		    y=sp.y, w=sp.w, h=sp.h))
		for rid, label, depth, kind in sp.rows:
			top = sp.y + story.HEAD + sp.index[rid] * story.ROW
			rows.append(dict(id=rid, label=label, depth=depth, kind=kind,
			    key=key, top=top, cy=top + story.ROW / 2,
			    tx=sp.x + 22 + depth * 14))
	return dict(W=story.W, H=story.H, ROW=story.ROW, CW=story.CW,
	    PW=story.PW, spaces=spaces, rows=rows)


def data():
	return dict(geom=geometry(), slides=build_slides(),
	    pillKind=story.PILL_KIND, arrowClass=story.ARROW_CLASS)


PAGE = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>shtetl, one cycle in slides</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Archivo:wght@400;600;700&family=IBM+Plex+Mono:wght@400;500;600&family=Newsreader:opsz,wght@6..72,400;6..72,500&display=swap" rel="stylesheet">
<style>
:root {
	--paper: #f6f7f6;
	--ink: #1d2126;
	--muted: #5e656d;
	--line: #d3d7db;
	--code-bg: #e9ecee;
	--stage: #e3e6e9;
	--box: #fbfcfc;
	--accent: #2f4fc9;
	--sp-config: #6a52b0;
	--sp-build: #a8670c;
	--sp-lake: #2f6690;
	--sp-run: #2f7a4f;
	--version: #2f4fc9;
	--save: #5e646c;
	--from: #d9a300;
	--from-ink: #1d2126;
	--a-build: #b8740f;
	--a-lake: #2f4fc9;
	--a-run: #2f7a4f;
	--a-rebase: #b3246b;
	--a-muted: #7d848c;
	--hl-new: #d3ecd9;
	--hl-write: #f7e3b5;
	--hl-read: #e5ddf5;
	--hl-snap: #d9e1f7;
	--hl-gone: #f4d2cf;
	--hl-move: #e3e5e8;
	--strike: #b3241c;
	--ok: #2f7a4f;
	--warn: #b8740f;
	--fail: #b3246b;
	--prop: #6a52b0;
}
@media (prefers-color-scheme: dark) {
	:root:not([data-theme="light"]) {
		--paper: #1f2328;
		--ink: #e5e8eb;
		--muted: #a2a9b0;
		--line: #353b42;
		--code-bg: #2a2f35;
		--stage: #171a1e;
		--box: #20252b;
		--accent: #8aa0ff;
		--sp-config: #b6a4f0;
		--sp-build: #e5a14a;
		--sp-lake: #7fb3d9;
		--sp-run: #6cc495;
		--version: #6f8cff;
		--save: #9aa1a9;
		--from: #f0c02a;
		--from-ink: #1d2126;
		--a-build: #e5a14a;
		--a-lake: #6f8cff;
		--a-run: #6cc495;
		--a-rebase: #ef6aa6;
		--a-muted: #8b9097;
		--hl-new: #1f3a28;
		--hl-write: #43361a;
		--hl-read: #2f2840;
		--hl-snap: #1c2d48;
		--hl-gone: #46231f;
		--hl-move: #2a2e33;
		--strike: #ef6a5d;
		--ok: #6cc495;
		--warn: #e5a14a;
		--fail: #ef6aa6;
		--prop: #b6a4f0;
	}
}
:root[data-theme="dark"] {
	--paper: #1f2328;
	--ink: #e5e8eb;
	--muted: #a2a9b0;
	--line: #353b42;
	--code-bg: #2a2f35;
	--stage: #171a1e;
	--box: #20252b;
	--accent: #8aa0ff;
	--sp-config: #b6a4f0;
	--sp-build: #e5a14a;
	--sp-lake: #7fb3d9;
	--sp-run: #6cc495;
	--version: #6f8cff;
	--save: #9aa1a9;
	--from: #f0c02a;
	--from-ink: #1d2126;
	--a-build: #e5a14a;
	--a-lake: #6f8cff;
	--a-run: #6cc495;
	--a-rebase: #ef6aa6;
	--a-muted: #8b9097;
	--hl-new: #1f3a28;
	--hl-write: #43361a;
	--hl-read: #2f2840;
	--hl-snap: #1c2d48;
	--hl-gone: #46231f;
	--hl-move: #2a2e33;
	--strike: #ef6a5d;
	--ok: #6cc495;
	--warn: #e5a14a;
	--fail: #ef6aa6;
	--prop: #b6a4f0;
}
* { box-sizing: border-box; }
html, body { height: 100%; }
body {
	margin: 0;
	background: var(--stage);
	color: var(--ink);
	font: 17px/1.55 "Newsreader", Georgia, serif;
	display: grid;
	grid-template-columns: minmax(330px, 31rem) minmax(0, 1fr);
	overflow: hidden;
}
code, pre { font-family: "IBM Plex Mono", Menlo, monospace; }
code { font-size: 0.8em; background: var(--code-bg); padding: 1px 4px;
	border-radius: 3px; }
pre { font-size: 12px; line-height: 1.5; background: var(--code-bg);
	padding: 10px 12px; border-radius: 4px; overflow-x: auto;
	margin: 10px 0 14px; }
pre code { background: none; padding: 0; font-size: inherit; }
pre.sketch { outline: 1px dashed var(--line); }
a { color: var(--accent); }

/* the slide text */
.placard {
	background: var(--paper);
	border-right: 1px solid var(--line);
	display: flex;
	flex-direction: column;
	height: 100vh;
	min-width: 0;
}
.placard header { padding: 22px 26px 10px; }
.where {
	font: 600 13px/1.3 "Archivo", "Helvetica Neue", Arial, sans-serif;
	color: var(--muted);
	margin: 0 0 6px;
	min-height: 1em;
}
.placard h1 {
	font: 700 26px/1.15 "Archivo", "Helvetica Neue", Arial, sans-serif;
	letter-spacing: -0.01em;
	margin: 0;
}
.slide-body { flex: 1; overflow-y: auto; padding: 4px 26px 20px; }
.slide-body p { margin: 0 0 12px; max-width: 62ch; }
.slide-body p code { overflow-wrap: anywhere; }
.placard footer {
	display: flex; align-items: center; gap: 10px;
	padding: 12px 26px 16px; border-top: 1px solid var(--line);
	font-family: "Archivo", "Helvetica Neue", Arial, sans-serif;
}
.counter { flex: 1; text-align: center; color: var(--muted); font-size: 14px;
	font-variant-numeric: tabular-nums; }
button {
	font: 600 15px "Archivo", "Helvetica Neue", Arial, sans-serif;
	color: var(--ink); background: transparent;
	border: 1.5px solid var(--ink); border-radius: 999px;
	padding: 7px 16px; cursor: pointer;
}
button.primary { background: var(--ink); color: var(--paper); }
button:disabled { opacity: 0.3; cursor: default; }
button:focus-visible, a:focus-visible, .tick:focus-visible {
	outline: 3px solid var(--accent); outline-offset: 2px; }

/* the story's own panels */
.tablewrap { overflow-x: auto; }
.cast { width: 100%; border-collapse: collapse; font-size: 14px; margin: 6px 0 14px; }
.cast td, .cast th { text-align: left; vertical-align: top; padding: 4px 6px;
	border-bottom: 1px solid var(--line); }
.cast th { font: 600 12px "Archivo", sans-serif; color: var(--muted); }
.codes { display: grid; gap: 4px; }
.fname { font: 600 12px "Archivo", sans-serif; color: var(--muted); margin-top: 8px; }
.checks { border-top: 1px solid var(--line); padding: 8px 0; margin: 8px 0; }
.checks .ctitle { font: 600 13px "Archivo", sans-serif; color: var(--muted);
	margin-bottom: 4px; }
.checks ul { list-style: none; margin: 0; padding: 0; }
.checks li { display: grid; grid-template-columns: 44px 1fr; gap: 8px;
	padding: 3px 0; font-size: 15px; }
.cs { font: 600 10.5px "IBM Plex Mono", monospace; color: var(--paper);
	border-radius: 3px; text-align: center; height: 17px; line-height: 17px;
	margin-top: 4px; }
.cs-ok { background: var(--ok); } .cs-warn { background: var(--warn); }
.cs-fail { background: var(--fail); } .cs-note { background: var(--muted); }
.cs-prop { background: var(--prop); }
.callout { border-left: 3px solid var(--c); padding: 4px 0 4px 12px; margin: 12px 0; }
.callout .ctitle { font: 700 14px "Archivo", sans-serif; color: var(--c); }
.callout p { margin: 4px 0; font-size: 15.5px; }
.callout-finding { --c: var(--a-build); }
.callout-question { --c: var(--prop); }
.callout-note { --c: var(--accent); }
.diagram { margin: 10px 0; overflow-x: auto; }
.diagram svg { width: 100%; min-width: 520px; height: auto; display: block; }
.diagram svg text { font-family: "Archivo", sans-serif; fill: var(--ink); }
.diagram svg .rds, .diagram svg .ltext { font-family: "IBM Plex Mono", monospace; font-size: 12px; }
.diagram svg .lin { fill: none; stroke: var(--muted); stroke-width: 3; }
.diagram svg .lin-new { stroke: var(--a-run); stroke-dasharray: 6 4; }
.diagram svg .ldot { stroke: var(--paper); stroke-width: 2; }
.diagram svg .ldot-version { fill: var(--version); }
.diagram svg .ldot-save { fill: var(--save); }
.diagram svg .ldot-from { fill: var(--from); }
.diagram svg .ar { fill: none; stroke-width: 2.4; }
.diagram svg .ar-rebase { stroke: var(--a-rebase); }
.diagram svg .mk-rebase { fill: var(--a-rebase); }
.diagram svg .badge-rebase { fill: var(--a-rebase); }
.diagram svg .badgetext { font-size: 11px; font-weight: 700; fill: var(--paper); }

/* the map */
#stage { position: relative; overflow: hidden; min-width: 0; }
#map { position: absolute; top: 0; left: 0; right: 0; bottom: 46px; width: 100%; height: calc(100% - 46px);
	touch-action: none; cursor: grab; }
#map.dragging { cursor: grabbing; }
#map text { font-family: "Archivo", "Helvetica Neue", Arial, sans-serif; fill: var(--ink); }
#map .box { fill: var(--box); stroke-width: 2; }
#map .box-C { stroke: var(--sp-config); } #map .box-B { stroke: var(--sp-build); }
#map .box-L { stroke: var(--sp-lake); } #map .box-R { stroke: var(--sp-run); }
#map .slabel { font-size: 15px; font-weight: 700; }
#map .slabel-C { fill: var(--sp-config); } #map .slabel-B { fill: var(--sp-build); }
#map .slabel-L { fill: var(--sp-lake); } #map .slabel-R { fill: var(--sp-run); }
#map .ssub { font-size: 11px; fill: var(--muted); }
#map .nm { font-family: "IBM Plex Mono", Menlo, monospace; font-size: 12px; }
#map .nm-root { font-weight: 600; }
#map .nm-file, #map .nm-dir { fill: var(--muted); font-style: italic; }
#map .tag { font-size: 10.5px; fill: var(--muted); }
#map .mdot { fill: var(--ink); }
#map .hl { fill: transparent; }
#map .hl-new { fill: var(--hl-new); } #map .hl-write { fill: var(--hl-write); }
#map .hl-read { fill: var(--hl-read); } #map .hl-snap { fill: var(--hl-snap); }
#map .hl-gone { fill: var(--hl-gone); } #map .hl-move { fill: var(--hl-move); }
#map .strike { stroke: var(--strike); stroke-width: 1.6; }
#map .pill rect { stroke: none; }
#map .pill-version rect { fill: var(--version); }
#map .pill-save rect { fill: var(--save); }
#map .pill-from rect { fill: var(--from); }
#map .pill text { font: 600 10.5px "IBM Plex Mono", monospace; fill: var(--box); }
#map .pill-from text { fill: var(--from-ink); }
#map .ar { fill: none; stroke-width: 2; stroke-linecap: round; }
#map .ar-build { stroke: var(--a-build); } #map .ar-lake { stroke: var(--a-lake); }
#map .ar-run { stroke: var(--a-run); } #map .ar-rebase { stroke: var(--a-rebase); stroke-width: 2.6; }
#map .ar-muted { stroke: var(--a-muted); stroke-dasharray: 5 4; }
#map .head-build, #map .bead-build, #map .badge-build { fill: var(--a-build); }
#map .head-lake, #map .bead-lake, #map .badge-lake { fill: var(--a-lake); }
#map .head-run, #map .bead-run, #map .badge-run { fill: var(--a-run); }
#map .head-rebase, #map .bead-rebase, #map .badge-rebase { fill: var(--a-rebase); }
#map .head-muted, #map .bead-muted, #map .badge-muted { fill: var(--a-muted); }
#map .badgetext { font-size: 11px; font-weight: 700; fill: var(--box); }
.tools { position: absolute; top: 14px; right: 16px; display: flex; gap: 8px; }
.tools button { background: var(--paper); border-color: var(--line);
	font-size: 13px; padding: 6px 12px; }
.tools button[aria-pressed="true"] { border-color: var(--ink); }
.timeline {
	position: absolute; left: 16px; right: 16px; bottom: 8px; height: 34px;
	display: flex; align-items: flex-end; gap: 2px; overflow-x: auto;
	padding: 4px 2px;
}
.tick { flex: 1 0 8px; height: 14px; border: 0; padding: 0; border-radius: 2px;
	background: var(--line); cursor: pointer; }
.tick.act { height: 22px; background: var(--muted); margin-left: 6px; }
.tick.act:first-child { margin-left: 0; }
.tick.done { background: var(--muted); opacity: 0.55; }
.tick.act.done { opacity: 0.8; }
.tick.current { background: var(--accent); opacity: 1; height: 24px; }
@media (max-width: 820px) {
	body { grid-template-columns: minmax(0, 1fr); grid-template-rows: 48vh 52vh; }
	#stage { grid-row: 1; }
	.placard { grid-row: 2; height: 52vh; border-right: 0;
		border-top: 1px solid var(--line); }
	.placard header { padding: 14px 16px 6px; }
	.slide-body { padding: 4px 16px 14px; }
	.placard footer { padding: 10px 16px 12px; }
	.placard h1 { font-size: 20px; }
}
</style>
</head>
<body>
<aside class="placard" aria-live="polite">
	<header>
		<p class="where" id="where"></p>
		<h1 id="title"></h1>
	</header>
	<div class="slide-body" id="text"></div>
	<footer>
		<button id="prev">Previous</button>
		<span class="counter" id="counter"></span>
		<button id="next" class="primary">Next</button>
	</footer>
</aside>
<main id="stage">
	<svg id="map" role="img" aria-label="The four ZFS spaces, changing with each slide"></svg>
	<div class="tools">
		<button id="follow" aria-pressed="true">Follow the story</button>
		<button id="whole">Whole map</button>
	</div>
	<nav class="timeline" id="timeline" aria-label="Slides"></nav>
</main>
<script type="application/json" id="story-data">@@DATA@@</script>
<script>
(function () {
'use strict';
const D = JSON.parse(document.getElementById('story-data').textContent);
const G = D.geom, SLIDES = D.slides;
const NS = 'http://www.w3.org/2000/svg';
const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
const $ = (id) => document.getElementById(id);
const svg = $('map');
const EASE = 'cubic-bezier(.2, .7, .2, 1)';

function S(tag, attrs, parent) {
	const e = document.createElementNS(NS, tag);
	for (const k in attrs) e.setAttribute(k, attrs[k]);
	if (parent) parent.appendChild(e);
	return e;
}

/* ---- running animations: finished at once when the reader moves on ---- */

let running = [];
let instant = false;
function anim(el, frames, opts, done) {
	if (instant || reduced || !el.animate) { if (done) done(); return; }
	const a = el.animate(frames, Object.assign({ easing: EASE, fill: 'backwards' }, opts));
	running.push(a);
	if (done) a.onfinish = () => { done(); a.cancel(); };
}
function settle() {
	const r = running;
	running = [];
	for (const a of r) if (a.playState !== 'finished') a.finish();
}

/* ---- the map, built once ---- */

const SP = {};
for (const s of G.spaces) SP[s.key] = Object.assign({ x2: s.x + s.w }, s);
const gBoxes = S('g', {}, svg);
for (const s of G.spaces) {
	S('rect', { class: 'box box-' + s.key, x: s.x, y: s.y, width: s.w, height: s.h, rx: 8 }, gBoxes);
	S('text', { class: 'slabel slabel-' + s.key, x: s.x + 14, y: s.y + 22 }, gBoxes).textContent = s.label;
	S('text', { class: 'ssub', x: s.x + 14, y: s.y + 38 }, gBoxes).textContent = s.sub;
}
const gRows = S('g', {}, svg);
const gArrows = S('g', {}, svg);
const ROWS = {};
const ROWLIST = [];
for (const r of G.rows) {
	const sp = SP[r.key];
	const g = S('g', { class: 'row' }, gRows);
	g.style.display = 'none';
	const hl = S('rect', { class: 'hl', x: sp.x + 4, y: r.top + 1, width: sp.w - 8, height: G.ROW - 2, rx: 4 }, g);
	const dot = S('circle', { class: 'mdot', cx: sp.x + 12, cy: r.cy, r: 3.5 }, g);
	dot.style.display = 'none';
	const nm = S('text', { class: 'nm nm-' + r.kind, x: r.tx, y: r.cy + 4 }, g);
	nm.textContent = r.label;
	const end = r.tx + r.label.length * G.CW;
	const strike = S('line', { class: 'strike', x1: r.tx - 2, y1: r.cy, x2: end + 2, y2: r.cy }, g);
	strike.style.display = 'none';
	const tag = S('text', { class: 'tag', x: end + 8, y: r.cy + 4 }, g);
	const pills = S('g', {}, g);
	const row = { id: r.id, r, sp, g, hl, dot, strike, tag, pills, pillEls: new Map(),
		on: false, hlKind: null, mounted: false, tagText: '' };
	ROWS[r.id] = row;
	ROWLIST.push(row);
}

/* ---- arrows: the same routes story.py draws ---- */

function cy(id) { return ROWS[id].r.cy; }
function route(a, b, side, bulge) {
	const sa = SP[a[0]], sb = SP[b[0]];
	const ya = cy(a), yb = cy(b);
	bulge = bulge == null ? 30 : bulge;
	if (sa === sb) {
		if (side == null) side = sa.key === 'R' ? 'R' : 'L';
		if (side === 'R') { const e = sa.x2; return [[e, ya], [e + bulge, ya], [e + bulge, yb], [e + 2, yb]]; }
		const e = sa.x; return [[e, ya], [e - bulge, ya], [e - bulge, yb], [e - 2, yb]];
	}
	if (sa.x2 <= sb.x) {
		const p0 = [sa.x2, ya], p3 = [sb.x - 2, yb], d = (p3[0] - p0[0]) * 0.45;
		return [p0, [p0[0] + d, ya], [p3[0] - d, yb], p3];
	}
	const p0 = [sa.x, ya], p3 = [sb.x2 + 2, yb], d = (p0[0] - p3[0]) * 0.45;
	return [p0, [p0[0] - d, ya], [p3[0] + d, yb], p3];
}
function bez(p, t) {
	const u = 1 - t;
	return [0, 1].map((i) => u * u * u * p[0][i] + 3 * u * u * t * p[1][i] + 3 * u * t * t * p[2][i] + t * t * t * p[3][i]);
}

let beads = [];
function drawArrows(slide, t0) {
	settleArrows();
	slide.arrows.forEach((a, i) => {
		const cls = D.arrowClass[a.kind];
		const p = route(a.a, a.b, a.side, a.bulge);
		const g = S('g', {}, gArrows);
		const path = S('path', { class: 'ar ar-' + cls,
			d: 'M' + p[0] + ' C' + p[1] + ' ' + p[2] + ' ' + p[3] }, g);
		const len = path.getTotalLength();
		const start = t0 + i * 180, dur = 650;
		if (cls === 'muted') {
			anim(path, [{ opacity: 0 }, { opacity: 1 }], { duration: dur, delay: start });
		} else {
			anim(path, [{ strokeDasharray: len + ' ' + len, strokeDashoffset: len },
				{ strokeDasharray: len + ' ' + len, strokeDashoffset: 0 }],
				{ duration: dur, delay: start, easing: 'ease-in-out' });
		}
		const dx = p[3][0] - p[2][0], dy = p[3][1] - p[2][1];
		const ang = Math.atan2(dy, dx) * 180 / Math.PI;
		const head = S('path', { class: 'head-' + cls, d: 'M0,0 L-9,-4.5 L-9,4.5 z',
			transform: 'translate(' + p[3][0] + ',' + p[3][1] + ') rotate(' + ang + ')' }, g);
		anim(head, [{ opacity: 0 }, { opacity: 1 }], { duration: 160, delay: start + dur - 80 });
		if (a.num != null) {
			const m = bez(p, a.t == null ? 0.5 : a.t);
			const bg = S('g', {}, g);
			S('circle', { class: 'badge-' + cls, cx: m[0], cy: m[1], r: 9 }, bg);
			S('text', { class: 'badgetext', x: m[0], y: m[1] + 4, 'text-anchor': 'middle' }, bg).textContent = String(a.num);
			anim(bg, [{ opacity: 0 }, { opacity: 1 }], { duration: 220, delay: start + dur * 0.5 });
		}
		if (!reduced && cls !== 'muted') {
			for (let k = 0; k < 2; k++) {
				const bead = S('circle', { class: 'bead-' + cls, r: 2.6, cx: p[0][0], cy: p[0][1] }, g);
				bead.style.opacity = '0';
				beads.push({ bead, path, len, phase: k / 2, at: performance.now() + (instant ? 0 : start + dur) });
			}
		}
	});
}
function settleArrows() {
	beads = [];
	while (gArrows.firstChild) gArrows.removeChild(gArrows.firstChild);
}
function beadLoop(now) {
	for (const b of beads) {
		if (now < b.at) continue;
		const t = ((now - b.at) / 2600 + b.phase) % 1;
		const pt = b.path.getPointAtLength(t * b.len);
		b.bead.setAttribute('cx', pt.x);
		b.bead.setAttribute('cy', pt.y);
		b.bead.style.opacity = '1';
	}
	requestAnimationFrame(beadLoop);
}
if (!reduced) requestAnimationFrame(beadLoop);

/* ---- one slide's state ---- */

function pillWidth(p) { return p.length * G.PW + 10; }

function setPills(row, list, t0) {
	const want = list.slice(-3);
	const xs = [];
	let px = row.sp.x2 - 8;
	for (let i = want.length - 1; i >= 0; i--) {
		px -= pillWidth(want[i]);
		xs[i] = px;
		px -= 4;
	}
	const y = row.r.cy - 7.5;
	for (const [name, el] of row.pillEls) {
		if (want.includes(name)) continue;
		row.pillEls.delete(name);
		anim(el, [{ opacity: 1 }, { opacity: 0 }], { duration: 220, fill: 'forwards' }, () => el.remove());
		if (instant || reduced) el.remove();
	}
	want.forEach((name, i) => {
		const to = 'translate(' + xs[i] + 'px, ' + y + 'px)';
		let el = row.pillEls.get(name);
		if (el) {
			const from = el.style.transform;
			el.style.transform = to;
			if (from !== to) anim(el, [{ transform: from }, { transform: to }], { duration: 380, delay: t0 });
			return;
		}
		el = S('g', { class: 'pill pill-' + D.pillKind[name] }, row.pills);
		const w = pillWidth(name);
		S('rect', { width: w, height: 15, rx: 7.5 }, el);
		S('text', { x: w / 2, y: 11, 'text-anchor': 'middle' }, el).textContent = name;
		el.style.transform = to;
		row.pillEls.set(name, el);
		anim(el, [{ opacity: 0, transform: 'translate(' + xs[i] + 'px, ' + (y - 12) + 'px)' },
			{ opacity: 1, transform: to }], { duration: 420, delay: t0 + 120 + i * 80 });
	});
}

function apply(slide) {
	const st = slide.state;
	const present = new Set(st.present), mounted = new Set(st.mounted);
	const t0 = (instant || reduced) ? 0 : 380;
	let enter = 0;
	for (const row of ROWLIST) {
		const on = present.has(row.id);
		if (on && !row.on) {
			row.g.style.display = '';
			anim(row.g, [{ opacity: 0, transform: 'translateX(-10px)' }, { opacity: 1, transform: 'none' }],
				{ duration: 420, delay: t0 + Math.min(enter++, 12) * 35 });
		} else if (!on && row.on) {
			const g = row.g;
			anim(g, [{ opacity: 1 }, { opacity: 0 }], { duration: 300, fill: 'forwards' },
				() => { if (!row.on) g.style.display = 'none'; });
			if (instant || reduced) g.style.display = 'none';
		}
		row.on = on;
		if (!on) continue;
		const hk = slide.hl[row.id] || null;
		if (hk !== row.hlKind) {
			row.hl.setAttribute('class', 'hl' + (hk ? ' hl-' + hk : ''));
			if (hk) anim(row.hl, [{ opacity: 0 }, { opacity: 1 }], { duration: 380, delay: t0 + 60 });
			row.hlKind = hk;
		}
		if (hk === 'write') anim(row.hl, [{ opacity: 1 }, { opacity: 0.45 }, { opacity: 1 }],
			{ duration: 900, delay: t0 + 500, iterations: 2, easing: 'ease-in-out', fill: 'none' });
		if (hk === 'gone') {
			row.strike.style.display = '';
			const len = Math.abs(row.strike.x2.baseVal.value - row.strike.x1.baseVal.value);
			anim(row.strike, [{ strokeDasharray: len + ' ' + len, strokeDashoffset: len },
				{ strokeDasharray: len + ' ' + len, strokeDashoffset: 0 }], { duration: 420, delay: t0 + 200 });
		} else {
			row.strike.style.display = 'none';
		}
		const m = mounted.has(row.id);
		if (m !== row.mounted) {
			row.dot.style.display = m ? '' : 'none';
			if (m) anim(row.dot, [{ opacity: 0 }, { opacity: 1 }], { duration: 300, delay: t0 + 150 });
			row.mounted = m;
		}
		const tag = st.tags[row.id] || '';
		if (tag !== row.tagText) {
			row.tag.textContent = tag;
			if (tag) anim(row.tag, [{ opacity: 0 }, { opacity: 1 }], { duration: 320, delay: t0 + 100 });
			row.tagText = tag;
		}
		setPills(row, st.pills[row.id] || [], t0);
	}
	drawArrows(slide, t0 + 260);
}

/* ---- the view ---- */

const WHOLE = { x: -10, y: -10, w: G.W + 20, h: G.H + 20 };
let vb = Object.assign({}, WHOLE);
let follow = true, camAnim = null;

function setVB(v) {
	vb = v;
	svg.setAttribute('viewBox', v.x + ' ' + v.y + ' ' + v.w + ' ' + v.h);
}
function aspect() {
	const r = svg.getBoundingClientRect();
	return r.width > 0 && r.height > 0 ? r.width / r.height : G.W / G.H;
}
function fit(box) {
	const ar = aspect();
	let w = Math.max(box.w, 560), h = Math.max(box.h, 280);
	if (w / h > ar) h = w / ar; else w = h * ar;
	if (w >= WHOLE.w && h >= WHOLE.h) return fitWhole();
	const cx = box.x + box.w / 2, cyy = box.y + box.h / 2;
	return { x: cx - w / 2, y: cyy - h / 2, w, h };
}
function fitWhole() {
	const ar = aspect();
	let w = WHOLE.w, h = WHOLE.h;
	if (w / h > ar) h = w / ar; else w = h * ar;
	return { x: WHOLE.x + WHOLE.w / 2 - w / 2, y: WHOLE.y + WHOLE.h / 2 - h / 2, w, h };
}
function focusBox(slide) {
	const ids = new Set(Object.keys(slide.hl));
	let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
	const add = (x, y) => { x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); };
	for (const a of slide.arrows) {
		ids.add(a.a); ids.add(a.b);
		for (const pt of route(a.a, a.b, a.side, a.bulge)) add(pt[0], pt[1]);
	}
	for (const id of ids) {
		const row = ROWS[id];
		if (!row) continue;
		add(row.sp.x, row.r.top - 30);
		add(row.sp.x2, row.r.top + G.ROW + 10);
	}
	if (!isFinite(x0)) return null;
	return { x: x0 - 30, y: y0 - 70, w: x1 - x0 + 60, h: y1 - y0 + 100 };
}
function moveView(to) {
	if (instant || reduced) { setVB(to); camAnim = null; return; }
	camAnim = { from: Object.assign({}, vb), to, t0: performance.now(), dur: 700 };
	requestAnimationFrame(camStep);
}
function camStep(now) {
	if (!camAnim) return;
	let t = Math.min(1, (now - camAnim.t0) / camAnim.dur);
	const e = t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
	const f = camAnim.from, to = camAnim.to;
	setVB({ x: f.x + (to.x - f.x) * e, y: f.y + (to.y - f.y) * e,
		w: f.w + (to.w - f.w) * e, h: f.h + (to.h - f.h) * e });
	if (t < 1) requestAnimationFrame(camStep); else camAnim = null;
}
function focus(slide) {
	if (!follow) return;
	const box = focusBox(slide);
	moveView(box ? fit(box) : fitWhole());
}
function setFollow(on) {
	follow = on;
	$('follow').setAttribute('aria-pressed', String(on));
}
$('follow').addEventListener('click', () => {
	setFollow(!follow);
	if (follow && current >= 0) focus(SLIDES[current]);
});
$('whole').addEventListener('click', () => { setFollow(false); moveView(fitWhole()); });

/* drag to pan, wheel to zoom */
let drag = null;
svg.addEventListener('pointerdown', (e) => {
	drag = { x: e.clientX, y: e.clientY, vb: Object.assign({}, vb) };
	svg.setPointerCapture(e.pointerId);
	svg.classList.add('dragging');
});
svg.addEventListener('pointermove', (e) => {
	if (!drag) return;
	const r = svg.getBoundingClientRect();
	const s = drag.vb.w / r.width;
	if (Math.abs(e.clientX - drag.x) + Math.abs(e.clientY - drag.y) > 3) { setFollow(false); camAnim = null; }
	setVB({ x: drag.vb.x - (e.clientX - drag.x) * s, y: drag.vb.y - (e.clientY - drag.y) * s, w: drag.vb.w, h: drag.vb.h });
});
const endDrag = () => { drag = null; svg.classList.remove('dragging'); };
svg.addEventListener('pointerup', endDrag);
svg.addEventListener('pointercancel', endDrag);
svg.addEventListener('wheel', (e) => {
	e.preventDefault();
	setFollow(false);
	camAnim = null;
	const r = svg.getBoundingClientRect();
	const k = Math.exp(e.deltaY * 0.0015);
	const w = Math.min(Math.max(vb.w * k, 200), WHOLE.w * 2.5);
	const f = w / vb.w;
	const mx = vb.x + (e.clientX - r.left) / r.width * vb.w;
	const my = vb.y + (e.clientY - r.top) / r.height * vb.h;
	setVB({ x: mx - (mx - vb.x) * f, y: my - (my - vb.y) * f, w, h: vb.h * f });
}, { passive: false });
window.addEventListener('resize', () => { if (follow && current >= 0) { instant = true; focus(SLIDES[current]); instant = false; } });

/* ---- the slide text ---- */

let current = -1;
function slideFromHash() {
	const m = /^#slide-(\d+)$/.exec(location.hash);
	const n = m ? parseInt(m[1], 10) - 1 : 0;
	return Math.min(Math.max(n, 0), SLIDES.length - 1);
}
function show(k, now) {
	if (k < 0 || k >= SLIDES.length) return;
	settle();
	instant = !!now;
	current = k;
	const s = SLIDES[k];
	$('where').textContent = s.where;
	$('title').textContent = s.title;
	$('text').innerHTML = s.html;
	$('text').scrollTop = 0;
	$('counter').textContent = (k + 1) + ' of ' + SLIDES.length;
	$('prev').disabled = k === 0;
	$('next').disabled = k === SLIDES.length - 1;
	$('next').textContent = k === 0 ? 'Begin' : 'Next';
	document.querySelectorAll('.tick').forEach((t, i) => {
		t.classList.toggle('current', i === k);
		t.classList.toggle('done', i < k);
		if (i === k) t.setAttribute('aria-current', 'step'); else t.removeAttribute('aria-current');
	});
	history.replaceState(null, '', '#slide-' + (k + 1));
	focus(s);
	apply(s);
	instant = false;
}
const tl = $('timeline');
SLIDES.forEach((s, i) => {
	const b = document.createElement('button');
	b.className = 'tick' + (s.kind !== 'frame' ? ' act' : '');
	const label = (s.where ? s.where + ': ' : '') + s.title;
	b.title = label;
	b.setAttribute('aria-label', label);
	b.addEventListener('click', () => show(i));
	tl.appendChild(b);
});
$('prev').addEventListener('click', () => show(current - 1));
$('next').addEventListener('click', () => show(current + 1));
$('text').addEventListener('click', (e) => {
	const a = e.target.closest('a[data-slide]');
	if (!a) return;
	e.preventDefault();
	show(parseInt(a.dataset.slide, 10) - 1);
});
document.addEventListener('keydown', (e) => {
	if (e.altKey || e.ctrlKey || e.metaKey) return;
	if (e.key === 'ArrowRight' || e.key === 'PageDown') { show(current + 1); e.preventDefault(); }
	else if (e.key === 'ArrowLeft' || e.key === 'PageUp') { show(current - 1); e.preventDefault(); }
	else if (e.key === 'Home') { show(0); e.preventDefault(); }
	else if (e.key === 'End') { show(SLIDES.length - 1); e.preventDefault(); }
});
window.addEventListener('hashchange', () => {
	const k = slideFromHash();
	if (k !== current) show(k);
});

setVB(fitWhole());
show(slideFromHash(), true);
})();
</script>
</body>
</html>
'''


def page():
	blob = json.dumps(data(), ensure_ascii=True, separators=(",", ":"))
	blob = blob.replace("</", "<\\/")
	return PAGE.replace("@@DATA@@", blob)


if __name__ == "__main__":
	sys.stdout.write(page())
