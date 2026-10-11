#!/usr/bin/env python3
#
# story3d.py: generate docs/story3d.html, the three.js version of
# docs/story.html. It imports story.py, which runs the story and keeps
# the state of every frame, and hands those states to a page that draws
# the four spaces as a model and changes it in place from slide to slide.
# Edit the story in story.py; this script only stages it.
#
# usage: python3 tools/story3d.py > docs/story3d.html
#
# ASCII only.

import json
import os
import re
import sys
from html import escape as esc

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import story  # noqa: E402  runs the story; writes nothing

THREE_VERSION = "0.160.0"


def state_json(st):
	return {
		"present": sorted(st.present),
		"mounted": sorted(st.mounted & st.present),
		"pills": {k: v for k, v in st.pills.items() if k in st.present},
		"tags": {k: v for k, v in st.tags.items() if k in st.present},
	}


EMPTY = {"present": [], "mounted": [], "pills": {}, "tags": {}}

TITLE_HTML = story.p(
	"One machine, one foundation, one container. The model on the right "
	"is the machine's four ZFS spaces; every slide changes it in place, "
	"the way the cycle changes the datasets: a foundation is built, a "
	"container is made from it, and the container is moved onto the "
	"foundation's next version.",
	"Move with the buttons below, the arrow keys, or the marks along the "
	"bottom of the model. Drag to turn the model, scroll to zoom; the "
	"view follows the story until you move it, and <b>Follow the "
	"story</b> hands it back.") + \
	'<div class="tablewrap"><table class="cast"><tr><th>name</th>' \
	'<th>what it is in this story</th></tr>' + "".join(
	"<tr><td><code>%s</code></td><td>%s</td></tr>" % (k, v)
	for k, v in story.CAST) + "</table></div>" + story.p(
	"The same story, drawn as a long page of flat diagrams, is "
	"<code>story.html</code>.")


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
		    num=a["num"]) for a in f["arrows"]]))
		prev = st
	# {{f:Title}} becomes a link to that frame's slide
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


def data():
	spaces = []
	for key in ("C", "B", "L", "R"):
		sp = story.SPACES[key]
		spaces.append(dict(key=key, label=sp.label, sub=sp.sub,
		    rows=[[r[0], r[1], r[2], r[3]] for r in sp.rows]))
	return dict(spaces=spaces, slides=build_slides(),
	    pillKind=story.PILL_KIND, arrowClass=story.ARROW_CLASS)


PAGE = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>shtetl, one cycle in 3D</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Archivo:wght@400;600;700&family=IBM+Plex+Mono:wght@400;500&family=Newsreader:opsz,wght@6..72,400;6..72,500&display=swap" rel="stylesheet">
<style>
:root {
	--paper: #f6f7f6;
	--ink: #1d2126;
	--muted: #5e656d;
	--line: #d3d7db;
	--code-bg: #e9ecee;
	--accent: #2f4fc9;
	--version: #2f4fc9;
	--save: #5e646c;
	--from: #d9a300;
	--build: #b8740f;
	--send: #2f4fc9;
	--clone: #2f7a4f;
	--rebase: #b3246b;
	--ok: #2f7a4f;
	--warn: #b8740f;
	--fail: #b3246b;
	--prop: #6a52b0;
	--stage: #d9dde1;
}
@media (prefers-color-scheme: dark) {
	:root:not([data-theme="light"]) {
		--paper: #1f2328;
		--ink: #e5e8eb;
		--muted: #a2a9b0;
		--line: #353b42;
		--code-bg: #2a2f35;
		--accent: #8aa0ff;
		--version: #6f8cff;
		--save: #9aa1a9;
		--from: #f0c02a;
		--build: #e5a14a;
		--send: #6f8cff;
		--clone: #6cc495;
		--rebase: #ef6aa6;
		--ok: #6cc495;
		--warn: #e5a14a;
		--fail: #ef6aa6;
		--prop: #b6a4f0;
		--stage: #171a1e;
	}
}
:root[data-theme="dark"] {
	--paper: #1f2328;
	--ink: #e5e8eb;
	--muted: #a2a9b0;
	--line: #353b42;
	--code-bg: #2a2f35;
	--accent: #8aa0ff;
	--version: #6f8cff;
	--save: #9aa1a9;
	--from: #f0c02a;
	--build: #e5a14a;
	--send: #6f8cff;
	--clone: #6cc495;
	--rebase: #ef6aa6;
	--ok: #6cc495;
	--warn: #e5a14a;
	--fail: #ef6aa6;
	--prop: #b6a4f0;
	--stage: #171a1e;
}
* { box-sizing: border-box; }
html, body { height: 100%; }
body {
	margin: 0;
	background: var(--stage);
	color: var(--ink);
	font: 17px/1.55 "Newsreader", Georgia, serif;
	display: grid;
	grid-template-columns: minmax(330px, 31rem) 1fr;
	overflow: hidden;
}
code, pre, .mono { font-family: "IBM Plex Mono", Menlo, monospace; }
code { font-size: 0.8em; background: var(--code-bg); padding: 1px 4px;
	border-radius: 3px; }
pre { font-size: 12px; line-height: 1.5; background: var(--code-bg);
	padding: 10px 12px; border-radius: 4px; overflow-x: auto;
	margin: 10px 0 14px; }
pre code { background: none; padding: 0; font-size: inherit; }
pre.sketch { outline: 1px dashed var(--line); }
a { color: var(--accent); }

/* the placard */
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
.placard h1, .placard h2 {
	font-family: "Archivo", "Helvetica Neue", Arial, sans-serif;
	font-weight: 700;
	letter-spacing: -0.01em;
	margin: 0;
	line-height: 1.15;
}
.placard h1 { font-size: 30px; }
.placard h2 { font-size: 23px; }
.slide-body {
	flex: 1;
	overflow-y: auto;
	padding: 4px 26px 20px;
}
.slide-body p { margin: 0 0 12px; max-width: 62ch; }
.slide-body p code { overflow-wrap: anywhere; }
.placard footer {
	display: flex;
	align-items: center;
	gap: 10px;
	padding: 12px 26px 16px;
	border-top: 1px solid var(--line);
	font-family: "Archivo", "Helvetica Neue", Arial, sans-serif;
}
.counter { flex: 1; text-align: center; color: var(--muted); font-size: 14px;
	font-variant-numeric: tabular-nums; }
button {
	font: 600 15px "Archivo", "Helvetica Neue", Arial, sans-serif;
	color: var(--ink);
	background: transparent;
	border: 1.5px solid var(--ink);
	border-radius: 999px;
	padding: 7px 16px;
	cursor: pointer;
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
.callout-finding { --c: var(--build); }
.callout-question { --c: var(--prop); }
.callout-note { --c: var(--accent); }
.diagram { margin: 10px 0; overflow-x: auto; }
.diagram svg { width: 100%; min-width: 520px; height: auto; display: block; }
.diagram svg text { font-family: "Archivo", sans-serif; fill: var(--ink); }
.diagram svg .rds, .diagram svg .ltext { font-family: "IBM Plex Mono", monospace; font-size: 12px; }
.diagram svg .lin { fill: none; stroke: var(--muted); stroke-width: 3; }
.diagram svg .lin-new { stroke: var(--clone); stroke-dasharray: 6 4; }
.diagram svg .ldot { stroke: var(--paper); stroke-width: 2; }
.diagram svg .ldot-version { fill: var(--version); }
.diagram svg .ldot-save { fill: var(--save); }
.diagram svg .ldot-from { fill: var(--from); }
.diagram svg .ar { fill: none; stroke-width: 2.4; }
.diagram svg .ar-rebase { stroke: var(--rebase); }
.diagram svg .mk-rebase { fill: var(--rebase); }
.diagram svg .badge-rebase { fill: var(--rebase); }
.diagram svg .badgetext { font-size: 11px; font-weight: 700; fill: var(--paper); }

/* the model */
#stage { position: relative; overflow: hidden; min-width: 0; }
#stage canvas { display: block; }
.labels { position: absolute; inset: 0; pointer-events: none; }
.lbl { width: 0; height: 0; }
.lbl > div {
	position: absolute; left: 0; top: -9px; white-space: nowrap;
	font: 500 11.5px/1.2 "IBM Plex Mono", Menlo, monospace;
	color: var(--ink);
	display: flex; gap: 5px; align-items: center;
	text-shadow: 0 0 3px var(--stage), 0 0 6px var(--stage);
}
.lbl .tg { font: 400 10.5px "Archivo", sans-serif; color: var(--muted); }
.lbl .ch { font: 600 9.5px "IBM Plex Mono", monospace; color: #fff;
	border-radius: 6px; padding: 1px 4px; text-shadow: none; }
.lbl .ch-version { background: var(--version); }
.lbl .ch-save { background: var(--save); }
.lbl .ch-from { background: var(--from); color: #1d2126; }
.lbl.file > div { color: var(--muted); font-style: italic; }
.plate-lbl > div {
	position: absolute; left: 0; top: -14px; white-space: nowrap;
	font: 700 17px "Archivo", sans-serif; color: var(--ink);
	text-shadow: 0 0 4px var(--stage);
}
.plate-lbl span { display: block; font: 400 11px "Archivo", sans-serif;
	color: var(--muted); }
.badge3 { width: 0; height: 0; }
.badge3 > div {
	position: absolute; left: -11px; top: -11px; width: 22px; height: 22px;
	border-radius: 50%; color: #fff;
	font: 700 12px/22px "Archivo", sans-serif; text-align: center;
	box-shadow: 0 1px 4px rgba(0, 0, 0, 0.25);
}
.tools { position: absolute; top: 14px; right: 16px; display: flex; gap: 8px; }
.tools button { background: var(--paper); border-color: var(--line); font-size: 13px;
	padding: 6px 12px; }
.tools button[aria-pressed="true"] { border-color: var(--ink); }
.key {
	position: absolute; right: 16px; top: 56px; width: 270px;
	background: var(--paper); border: 1px solid var(--line); border-radius: 6px;
	padding: 12px 14px; font: 13px/1.45 "Archivo", sans-serif; display: none;
}
.key.open { display: block; }
.key h3 { font-size: 13px; margin: 8px 0 4px; }
.key h3:first-child { margin-top: 0; }
.key ul { list-style: none; margin: 0; padding: 0; }
.key li { display: flex; align-items: center; gap: 8px; }
.sw { width: 14px; height: 10px; border-radius: 2px; flex: none; }
.timeline {
	position: absolute; left: 16px; right: 16px; bottom: 14px;
	display: flex; align-items: flex-end; gap: 2px; overflow-x: auto;
	padding: 4px 2px;
}
.tick {
	flex: 1 0 8px; height: 14px; border: 0; padding: 0; border-radius: 2px;
	background: var(--line); cursor: pointer;
}
.tick.act { height: 22px; background: var(--muted); margin-left: 6px; }
.tick.act:first-child { margin-left: 0; }
.tick.done { background: var(--muted); opacity: 0.55; }
.tick.act.done { opacity: 0.8; }
.tick.current { background: var(--accent); opacity: 1; height: 24px; }
.nogl {
	position: absolute; inset: 0; display: none; align-items: center;
	justify-content: center; padding: 40px; text-align: center;
	font: 16px/1.5 "Archivo", sans-serif; color: var(--muted);
}
.nogl.show { display: flex; }
@media (max-width: 820px) {
	body { grid-template-columns: minmax(0, 1fr); grid-template-rows: 50vh 50vh; }
	#stage { grid-row: 1; }
	.placard { grid-row: 2; height: 50vh; border-right: 0;
		border-top: 1px solid var(--line); }
	.placard header { padding: 14px 16px 6px; }
	.slide-body { padding: 4px 16px 14px; }
	.placard footer { padding: 10px 16px 12px; }
	.placard h1 { font-size: 24px; }
	.placard h2 { font-size: 19px; }
	.key { left: 16px; right: 16px; width: auto; }
}
</style>
<script type="importmap">
{ "imports": {
	"three": "https://cdn.jsdelivr.net/npm/three@@@THREE@@/build/three.module.js",
	"three/addons/": "https://cdn.jsdelivr.net/npm/three@@@THREE@@/examples/jsm/"
} }
</script>
</head>
<body>
<aside class="placard" aria-live="polite">
	<header>
		<p class="where" id="where"></p>
		<h1 id="title"></h1>
	</header>
	<div class="slide-body" id="body"></div>
	<footer>
		<button id="prev">Previous</button>
		<span class="counter" id="counter"></span>
		<button id="next" class="primary">Next</button>
	</footer>
</aside>
<main id="stage" aria-label="A model of the four ZFS spaces, changing with each slide">
	<div class="tools">
		<button id="follow" aria-pressed="true">Follow the story</button>
		<button id="keybtn" aria-expanded="false" aria-controls="key">Key</button>
	</div>
	<div class="key" id="key">
		<h3>The model</h3>
		<ul>
			<li><span class="sw" style="background:#f4f1ea;border:1px solid #9aa"></span>a building is a dataset; a sheet is a file</li>
			<li><span class="sw" style="background:#ffd27a"></span>a lit base means mounted</li>
			<li><span class="sw" style="border:1px solid #888"></span>a bare frame holds nothing (canmount=off)</li>
		</ul>
		<h3>Floors on a roof are snapshots</h3>
		<ul>
			<li><span class="sw" style="background:var(--version)"></span>a foundation version</li>
			<li><span class="sw" style="background:var(--save)"></span>a save</li>
			<li><span class="sw" style="background:var(--from)"></span>where an update starts</li>
		</ul>
		<h3>A glow says what this step did</h3>
		<ul>
			<li><span class="sw" style="background:#2f9e5a"></span>created</li>
			<li><span class="sw" style="background:#e39b16"></span>written</li>
			<li><span class="sw" style="background:#7b5cc4"></span>read</li>
			<li><span class="sw" style="background:#2f4fc9"></span>snapshotted</li>
			<li><span class="sw" style="background:#c8372d"></span>destroyed</li>
			<li><span class="sw" style="background:#9097a0"></span>renamed away</li>
		</ul>
		<h3>Threads are operations, numbered as in the text</h3>
		<ul>
			<li><span class="sw" style="background:var(--build)"></span>build, install</li>
			<li><span class="sw" style="background:var(--send)"></span>send, save</li>
			<li><span class="sw" style="background:var(--clone)"></span>clone</li>
			<li><span class="sw" style="background:var(--rebase)"></span>rebase</li>
			<li><span class="sw" style="background:#8b9097"></span>rename, promote</li>
		</ul>
	</div>
	<div class="nogl" id="nogl">This page draws its model with WebGL, which this browser
	did not provide. The slides still work; the same story, drawn flat, is story.html.</div>
	<nav class="timeline" id="timeline" aria-label="Slides"></nav>
</main>
<script type="application/json" id="story-data">@@DATA@@</script>
<script type="module">
const DATA = JSON.parse(document.getElementById('story-data').textContent);
const SLIDES = DATA.slides;
const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
const darkQuery = matchMedia('(prefers-color-scheme: dark)');

/* ---- the placard: works with or without WebGL ---- */

const el = (id) => document.getElementById(id);
let current = -1;
let model = null;

function slideFromHash() {
	const m = /^#slide-(\d+)$/.exec(location.hash);
	const n = m ? parseInt(m[1], 10) - 1 : 0;
	return Math.min(Math.max(n, 0), SLIDES.length - 1);
}

function render(k, instant) {
	if (k < 0 || k >= SLIDES.length) return;
	current = k;
	const s = SLIDES[k];
	el('where').textContent = s.where;
	const title = el('title');
	title.textContent = s.title;
	el('body').innerHTML = s.html;
	el('body').scrollTop = 0;
	el('counter').textContent = (k + 1) + ' of ' + SLIDES.length;
	el('prev').disabled = k === 0;
	el('next').disabled = k === SLIDES.length - 1;
	el('next').textContent = k === 0 ? 'Begin' : 'Next';
	document.querySelectorAll('.tick').forEach((t, i) => {
		t.classList.toggle('current', i === k);
		t.classList.toggle('done', i < k);
		if (i === k) t.setAttribute('aria-current', 'step');
		else t.removeAttribute('aria-current');
	});
	const tick = document.querySelectorAll('.tick')[k];
	if (tick && tick.scrollIntoView) tick.scrollIntoView({block: 'nearest', inline: 'nearest'});
	history.replaceState(null, '', '#slide-' + (k + 1));
	if (model) model.show(s, instant);
}

function buildTimeline() {
	const tl = el('timeline');
	SLIDES.forEach((s, i) => {
		const b = document.createElement('button');
		b.className = 'tick' + (s.kind !== 'frame' ? ' act' : '');
		const label = (s.where ? s.where + ': ' : '') + s.title;
		b.title = label;
		b.setAttribute('aria-label', label);
		b.addEventListener('click', () => render(i, false));
		tl.appendChild(b);
	});
}

el('prev').addEventListener('click', () => render(current - 1, false));
el('next').addEventListener('click', () => render(current + 1, false));
el('body').addEventListener('click', (e) => {
	const a = e.target.closest('a[data-slide]');
	if (!a) return;
	e.preventDefault();
	render(parseInt(a.dataset.slide, 10) - 1, false);
});
document.addEventListener('keydown', (e) => {
	if (e.altKey || e.ctrlKey || e.metaKey) return;
	if (e.key === 'ArrowRight' || e.key === 'PageDown') { render(current + 1, false); e.preventDefault(); }
	else if (e.key === 'ArrowLeft' || e.key === 'PageUp') { render(current - 1, false); e.preventDefault(); }
	else if (e.key === 'Home') { render(0, false); e.preventDefault(); }
	else if (e.key === 'End') { render(SLIDES.length - 1, false); e.preventDefault(); }
});
window.addEventListener('hashchange', () => {
	const k = slideFromHash();
	if (k !== current) render(k, false);
});
el('keybtn').addEventListener('click', () => {
	const open = el('key').classList.toggle('open');
	el('keybtn').setAttribute('aria-expanded', String(open));
});

buildTimeline();

/* ---- the model ---- */

async function start() {
	let THREE, OrbitControls, CSS2DRenderer, CSS2DObject;
	try {
		THREE = await import('three');
		({ OrbitControls } = await import('three/addons/controls/OrbitControls.js'));
		({ CSS2DRenderer, CSS2DObject } = await import('three/addons/renderers/CSS2DRenderer.js'));
	} catch (err) {
		el('nogl').classList.add('show');
		return null;
	}
	const stage = el('stage');
	let renderer;
	try {
		renderer = new THREE.WebGLRenderer({ antialias: true });
	} catch (err) {
		el('nogl').classList.add('show');
		return null;
	}
	renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
	renderer.shadowMap.enabled = true;
	renderer.shadowMap.type = THREE.PCFSoftShadowMap;
	stage.insertBefore(renderer.domElement, stage.firstChild);
	const labels = new CSS2DRenderer();
	labels.domElement.className = 'labels';
	stage.insertBefore(labels.domElement, renderer.domElement.nextSibling);

	const THEMES = {
		light: {
			bg: 0xd9dde1, table: 0xcdd2d7, building: 0xf6f3ec, root: 0x9aa3ad,
			paper: 0xfbfbfa, dir: 0xe2e5e8, wire: 0x6b727a, tree: 0x8e959d,
			lamp: 0xffc65a,
			plates: { C: 0xcdc6dc, B: 0xddd0bf, L: 0xc2cedb, R: 0xc5d6cb },
			hl: { new: 0x2f9e5a, write: 0xe39b16, read: 0x7b5cc4,
				snap: 0x2f4fc9, gone: 0xc8372d, move: 0x9097a0 },
			pill: { version: 0x2f4fc9, save: 0x5e646c, from: 0xe0aa00 },
			arrow: { build: 0xb8740f, lake: 0x2f4fc9, run: 0x2f7a4f,
				rebase: 0xb3246b, muted: 0x8b9097 },
			hemiSky: 0xffffff, hemiGround: 0x9aa3ad, hemi: 1.05, key: 1.6,
		},
		dark: {
			bg: 0x171a1e, table: 0x1e2227, building: 0xcfd2d4, root: 0x5c636b,
			paper: 0xb9bec3, dir: 0x6d747b, wire: 0xa2a9b0, tree: 0x59606a,
			lamp: 0xffc65a,
			plates: { C: 0x332c42, B: 0x3d3428, L: 0x24323f, R: 0x26362c },
			hl: { new: 0x41c27a, write: 0xf0a92a, read: 0x9b80e8,
				snap: 0x6f8cff, gone: 0xef5a4d, move: 0x7a828b },
			pill: { version: 0x5f7dff, save: 0x8d949c, from: 0xf0c02a },
			arrow: { build: 0xe5a14a, lake: 0x6f8cff, run: 0x6cc495,
				rebase: 0xef6aa6, muted: 0x8b9097 },
			hemiSky: 0xbfc8d6, hemiGround: 0x20252b, hemi: 0.75, key: 1.25,
		},
	};
	const forced = document.documentElement.getAttribute('data-theme');
	let theme = THEMES[forced || (darkQuery.matches ? 'dark' : 'light')];

	const scene = new THREE.Scene();
	scene.background = new THREE.Color(theme.bg);
	const camera = new THREE.PerspectiveCamera(32, 1, 0.5, 600);
	const controls = new OrbitControls(camera, renderer.domElement);
	controls.enableDamping = !reduced;
	controls.dampingFactor = 0.08;
	controls.maxPolarAngle = 1.32;
	controls.minDistance = 8;
	controls.maxDistance = 220;

	const hemi = new THREE.HemisphereLight(theme.hemiSky, theme.hemiGround, theme.hemi);
	scene.add(hemi);
	const key = new THREE.DirectionalLight(0xffffff, theme.key);
	key.position.set(-30, 60, 40);
	key.castShadow = true;
	key.shadow.mapSize.set(2048, 2048);
	Object.assign(key.shadow.camera, { left: -60, right: 60, top: 60, bottom: -60, near: 1, far: 200 });
	key.shadow.bias = -0.0005;
	scene.add(key);

	const table = new THREE.Mesh(new THREE.PlaneGeometry(400, 400),
		new THREE.MeshStandardMaterial({ color: theme.table, roughness: 1 }));
	table.rotation.x = -Math.PI / 2;
	table.position.y = -0.41;
	table.receiveShadow = true;
	scene.add(table);

	/* layout */
	const ROW_Z = 1.5, HEAD_Z = 3.0, PLATE_Y = 0.0, FH = 0.16, MAXF = 8;
	const LAYOUT = {
		C: { x0: -27, z0: -4, w: 12 },
		B: { x0: -27, z0: 13, w: 12 },
		L: { x0: -12, z0: -4, w: 20 },
		R: { x0: 11, z0: -4, w: 20 },
	};
	const SIZE = {
		root: { w: 2.0, h: 0.32, d: 1.0 },
		ds: { w: 1.4, h: 0.85, d: 0.85 },
		file: { w: 1.4, h: 0.06, d: 0.85 },
		dir: { w: 1.4, h: 0.025, d: 0.85 },
	};
	const unit = new THREE.BoxGeometry(1, 1, 1);
	const unitEdges = new THREE.EdgesGeometry(unit);
	const plates = {};
	const rows = {};
	const rowList = [];

	for (const sp of DATA.spaces) {
		const L = LAYOUT[sp.key];
		const depth = HEAD_Z + sp.rows.length * ROW_Z + 0.6;
		const plate = new THREE.Mesh(unit, new THREE.MeshStandardMaterial({
			color: theme.plates[sp.key], roughness: 0.95 }));
		plate.scale.set(L.w, 0.8, depth);
		plate.position.set(L.x0 + L.w / 2, PLATE_Y - 0.4, L.z0 + depth / 2);
		plate.receiveShadow = true;
		scene.add(plate);
		plates[sp.key] = plate;
		const pl = document.createElement('div');
		pl.className = 'plate-lbl';
		const inner = document.createElement('div');
		inner.innerHTML = '';
		inner.textContent = sp.label;
		const sub = document.createElement('span');
		sub.textContent = sp.sub;
		inner.appendChild(sub);
		pl.appendChild(inner);
		const plo = new CSS2DObject(pl);
		plo.position.set(L.x0 + 0.6, 0.05, L.z0 + 1.2);
		scene.add(plo);

		sp.rows.forEach(([id, label, depthLevel, kind], i) => {
			const size = SIZE[kind];
			const g = new THREE.Group();
			const x = L.x0 + 1.0 + depthLevel * 1.05 + size.w / 2;
			const z = L.z0 + HEAD_Z + i * ROW_Z;
			g.position.set(x, PLATE_Y, z);
			scene.add(g);
			const placeholder = /\/usr$/.test(id);
			const baseColor = kind === 'root' ? theme.root : kind === 'file' ? theme.paper
				: kind === 'dir' ? theme.dir : theme.building;
			const bodyMat = new THREE.MeshStandardMaterial({ color: baseColor,
				roughness: 0.85, transparent: true, opacity: 1 });
			const body = new THREE.Mesh(unit, bodyMat);
			body.castShadow = true;
			body.receiveShadow = true;
			g.add(body);
			const wire = new THREE.LineSegments(unitEdges,
				new THREE.LineBasicMaterial({ color: theme.wire, transparent: true }));
			wire.visible = false;
			g.add(wire);
			const band = new THREE.Mesh(unit, new THREE.MeshStandardMaterial({
				color: theme.lamp, emissive: theme.lamp, emissiveIntensity: 0.9 }));
			band.visible = false;
			g.add(band);
			const floors = [];
			for (let f = 0; f < MAXF; f++) {
				const m = new THREE.Mesh(unit, new THREE.MeshStandardMaterial({
					color: 0x888888, roughness: 0.6, transparent: true }));
				m.castShadow = true;
				m.visible = false;
				g.add(m);
				floors.push(m);
			}
			const outline = new THREE.LineSegments(unitEdges,
				new THREE.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0 }));
			g.add(outline);
			const lab = document.createElement('div');
			lab.className = 'lbl' + (kind === 'file' || kind === 'dir' ? ' file' : '');
			const labInner = document.createElement('div');
			lab.appendChild(labInner);
			const labObj = new CSS2DObject(lab);
			labObj.position.set(size.w / 2 + 0.3, 0.1, 0);
			g.add(labObj);
			const r = {
				id, label, kind, size, g, body, wire, band, floors, outline,
				lab: labObj, labInner, placeholder, baseColor,
				parent: null, conn: null, labelKey: '',
				cur: { appear: 0, floors: 0, mounted: 0, ghost: 0, red: 0, hl: 0 },
				tgt: { appear: 0, floors: 0, mounted: 0, ghost: 0, red: 0, hl: 0 },
				hlKind: null, pills: [],
			};
			rows[id] = r;
			rowList.push(r);
		});
	}
	/* tree connectors */
	const treeMat = new THREE.LineBasicMaterial({ color: theme.tree, transparent: true });
	for (const r of rowList) {
		const pid = r.id.includes('/') ? r.id.slice(0, r.id.lastIndexOf('/')) : null;
		const p = pid && rows[pid];
		if (!p) continue;
		r.parent = p;
		const px = p.g.position.x - p.size.w / 2 + 0.35;
		const y = PLATE_Y + 0.01;
		const pts = [
			new THREE.Vector3(px, y, p.g.position.z + p.size.d / 2),
			new THREE.Vector3(px, y, r.g.position.z),
			new THREE.Vector3(px, y, r.g.position.z),
			new THREE.Vector3(r.g.position.x - r.size.w / 2, y, r.g.position.z),
		];
		const conn = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(pts), treeMat.clone());
		conn.visible = false;
		scene.add(conn);
		r.conn = conn;
	}

	/* arrows */
	const arrowGroup = new THREE.Group();
	scene.add(arrowGroup);
	let beads = [];

	function clearArrows() {
		for (const o of [...arrowGroup.children]) {
			arrowGroup.remove(o);
			o.traverse((c) => {
				if (c.geometry && c.geometry !== unit) c.geometry.dispose();
				if (c.material) c.material.dispose();
				if (c.isCSS2DObject && c.element.parentNode) c.element.parentNode.removeChild(c.element);
			});
		}
		beads = [];
	}

	function topOf(r) {
		const a = r.tgt.appear;
		const n = Math.min(r.tgt.floors, MAXF);
		return new THREE.Vector3(r.g.position.x, PLATE_Y + r.size.h * a + FH * n + 0.25, r.g.position.z);
	}

	function cssColor(hex) {
		return '#' + hex.toString(16).padStart(6, '0');
	}

	function buildArrows(s) {
		clearArrows();
		for (const a of s.arrows) {
			const ra = rows[a.a], rb = rows[a.b];
			if (!ra || !rb) continue;
			const cls = DATA.arrowClass[a.kind];
			const color = theme.arrow[cls];
			const p0 = topOf(ra), p3 = topOf(rb);
			const dist = p0.distanceTo(p3);
			const lift = 2.0 + dist * 0.2;
			const c1 = p0.clone().add(new THREE.Vector3(0, lift, 0));
			const c2 = p3.clone().add(new THREE.Vector3(0, lift, 0));
			const curve = new THREE.CubicBezierCurve3(p0, c1, c2, p3);
			const dashed = cls === 'muted';
			const mat = new THREE.MeshStandardMaterial({ color, emissive: color,
				emissiveIntensity: 0.25, transparent: dashed, opacity: dashed ? 0.6 : 1 });
			const tube = new THREE.Mesh(new THREE.TubeGeometry(curve, 64, cls === 'rebase' ? 0.08 : 0.055, 6, false), mat);
			arrowGroup.add(tube);
			const tan = curve.getTangentAt(1).normalize();
			const head = new THREE.Mesh(new THREE.ConeGeometry(0.24, 0.65, 14), mat.clone());
			head.position.copy(p3).addScaledVector(tan, -0.3);
			head.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), tan);
			arrowGroup.add(head);
			if (!reduced) {
				for (let i = 0; i < 3; i++) {
					const bead = new THREE.Mesh(new THREE.SphereGeometry(0.13, 12, 8),
						new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: 0.6 }));
					arrowGroup.add(bead);
					beads.push({ bead, curve, phase: i / 3 });
				}
			}
			if (a.num != null) {
				const b = document.createElement('div');
				b.className = 'badge3';
				const inner = document.createElement('div');
				inner.textContent = String(a.num);
				inner.style.background = cssColor(color);
				b.appendChild(inner);
				const bo = new CSS2DObject(b);
				bo.position.copy(curve.getPoint(0.5));
				arrowGroup.add(bo);
			}
		}
	}

	/* camera */
	let follow = true;
	let camTween = null;
	camera.position.set(-40, 62, 92);
	controls.target.set(2, 0, 20);
	controls.update();
	controls.addEventListener('start', () => {
		if (!follow) return;
		follow = false;
		camTween = null;
		el('follow').setAttribute('aria-pressed', 'false');
	});
	el('follow').addEventListener('click', () => {
		follow = !follow;
		el('follow').setAttribute('aria-pressed', String(follow));
		if (follow && current >= 0) focus(SLIDES[current], false);
	});

	function focus(s, instant) {
		if (!follow) return;
		const pts = [];
		for (const id of Object.keys(s.hl)) if (rows[id]) pts.push(rows[id].g.position);
		for (const a of s.arrows) {
			if (rows[a.a]) pts.push(rows[a.a].g.position);
			if (rows[a.b]) pts.push(rows[a.b].g.position);
		}
		if (pts.length === 0) {
			for (const id of s.state.present) if (rows[id]) pts.push(rows[id].g.position);
		}
		const box = new THREE.Box3();
		if (pts.length === 0) {
			for (const k of Object.keys(plates)) box.expandByObject(plates[k]);
			box.expandByScalar(4);
		} else {
			box.setFromPoints(pts);
			box.expandByScalar(4);
		}
		const center = box.getCenter(new THREE.Vector3());
		const size = box.getSize(new THREE.Vector3());
		const radius = Math.max(size.length() / 2, 9);
		const fov = camera.fov * Math.PI / 180;
		const aspect = Math.max(camera.aspect, 0.6);
		const fit = radius / Math.tan(fov / 2) / Math.min(1, aspect);
		const dist = Math.min(Math.max(fit * 0.85, 24), 150);
		const dir = new THREE.Vector3(-0.38, 0.6, 0.7).normalize();
		const toT = center, toP = center.clone().addScaledVector(dir, dist);
		if (instant || reduced) {
			controls.target.copy(toT);
			camera.position.copy(toP);
			controls.update();
			camTween = null;
		} else {
			camTween = { fromT: controls.target.clone(), toT, fromP: camera.position.clone(), toP, t: 0 };
		}
	}

	/* a slide */
	function labelHTML(r, s) {
		const tag = s.state.tags[r.id];
		const pills = (s.state.pills[r.id] || []).slice(-3);
		let h = '<span class="nm">' + r.label + '</span>';
		if (tag) h += '<span class="tg">' + tag.replace(/&/g, '&amp;').replace(/</g, '&lt;') + '</span>';
		for (const p of pills) h += '<span class="ch ch-' + DATA.pillKind[p] + '">' + p + '</span>';
		return h;
	}

	function show(s, instant) {
		const present = new Set(s.state.present);
		const mounted = new Set(s.state.mounted);
		for (const r of rowList) {
			const on = present.has(r.id);
			const hl = s.hl[r.id] || null;
			r.hlKind = hl;
			r.pills = on ? (s.state.pills[r.id] || []).slice(-MAXF) : [];
			r.tgt.appear = on ? (hl === 'gone' ? 0.35 : 1) : 0;
			r.tgt.floors = r.pills.length;
			r.tgt.mounted = on && mounted.has(r.id) ? 1 : 0;
			r.tgt.ghost = hl === 'move' ? 1 : 0;
			r.tgt.red = hl === 'gone' ? 1 : 0;
			r.tgt.hl = hl ? 1 : 0;
			r.outline.material.color.setHex(hl ? theme.hl[hl] : 0xffffff);
			r.pills.forEach((p, i) => r.floors[i].material.color.setHex(theme.pill[DATA.pillKind[p]]));
			const html = on ? labelHTML(r, s) : '';
			if (html !== r.labelKey) { r.labInner.innerHTML = html; r.labelKey = html; }
			if (instant || reduced) Object.assign(r.cur, r.tgt);
		}
		buildArrows(s);
		focus(s, instant);
	}

	/* theme follows the system */
	function applyTheme() {
		theme = THEMES[darkQuery.matches ? 'dark' : 'light'];
		scene.background.setHex(theme.bg);
		table.material.color.setHex(theme.table);
		hemi.color.setHex(theme.hemiSky);
		hemi.groundColor.setHex(theme.hemiGround);
		hemi.intensity = theme.hemi;
		key.intensity = theme.key;
		for (const k of Object.keys(plates)) plates[k].material.color.setHex(theme.plates[k]);
		for (const r of rowList) {
			r.baseColor = r.kind === 'root' ? theme.root : r.kind === 'file' ? theme.paper
				: r.kind === 'dir' ? theme.dir : theme.building;
			r.wire.material.color.setHex(theme.wire);
			if (r.conn) r.conn.material.color.setHex(theme.tree);
		}
		if (current >= 0) show(SLIDES[current], true);
	}
	if (!forced) darkQuery.addEventListener('change', applyTheme);

	/* frame loop */
	const tmp = new THREE.Color(), red = new THREE.Color(), base = new THREE.Color();
	let last = performance.now();
	function frame(now) {
		const dt = Math.min((now - last) / 1000, 0.1);
		last = now;
		const k = reduced ? 1 : 1 - Math.exp(-dt * 6);
		const pulse = 0.65 + 0.35 * Math.sin(now / 260);
		for (const r of rowList) {
			const c = r.cur, t = r.tgt;
			for (const p in c) c[p] += (t[p] - c[p]) * k;
			const vis = c.appear > 0.01;
			const h = Math.max(r.size.h * c.appear, 0.001);
			const solid = vis && !r.placeholder;
			r.body.visible = solid;
			r.body.scale.set(r.size.w, h, r.size.d);
			r.body.position.y = PLATE_Y + h / 2;
			base.setHex(r.baseColor);
			red.setHex(theme.hl.gone);
			r.body.material.color.copy(base).lerp(red, c.red * 0.75);
			r.body.material.opacity = 1 - 0.72 * c.ghost;
			r.body.material.depthWrite = c.ghost < 0.5;
			const glow = r.hlKind && r.hlKind !== 'gone' ? theme.hl[r.hlKind] : 0x000000;
			tmp.setHex(glow);
			r.body.material.emissive.copy(tmp);
			r.body.material.emissiveIntensity = 0.32 * c.hl * (r.hlKind === 'write' ? pulse : 1);
			r.wire.visible = vis && r.placeholder;
			r.wire.scale.set(r.size.w, h, r.size.d);
			r.wire.position.y = PLATE_Y + h / 2;
			r.band.visible = vis && c.mounted > 0.03;
			r.band.scale.set(r.size.w + 0.06, 0.1, r.size.d + 0.06);
			r.band.position.y = PLATE_Y + 0.05;
			r.band.material.emissiveIntensity = 0.95 * c.mounted;
			let top = PLATE_Y + h;
			for (let i = 0; i < MAXF; i++) {
				const f = r.floors[i];
				const amt = Math.min(Math.max(c.floors - i, 0), 1);
				f.visible = vis && amt > 0.02 && i < r.pills.length;
				if (!f.visible) continue;
				const drop = (1 - amt) * 3;
				f.scale.set(r.size.w + 0.08, FH * 0.86, r.size.d + 0.08);
				f.position.y = top + FH * i + FH / 2 + drop;
				f.material.opacity = amt * (1 - 0.6 * c.red);
			}
			const total = h + FH * Math.min(c.floors, r.pills.length);
			r.outline.visible = vis && c.hl > 0.02;
			r.outline.scale.set(r.size.w + 0.24, total + 0.16, r.size.d + 0.24);
			r.outline.position.y = PLATE_Y + total / 2;
			r.outline.material.opacity = c.hl;
			r.lab.visible = c.appear > 0.2;
			r.lab.element.style.opacity = String(Math.min(1, 1 - 0.6 * c.ghost));
			if (r.conn) {
				const on = vis && r.parent.cur.appear > 0.01;
				r.conn.visible = on;
				r.conn.material.opacity = Math.min(c.appear, r.parent.cur.appear);
			}
		}
		for (const b of beads) {
			const t = ((now / 2600) + b.phase) % 1;
			b.bead.position.copy(b.curve.getPointAt(t));
		}
		if (camTween) {
			camTween.t = Math.min(1, camTween.t + dt / 0.9);
			const e = camTween.t < 0.5 ? 4 * camTween.t ** 3 : 1 - Math.pow(-2 * camTween.t + 2, 3) / 2;
			controls.target.lerpVectors(camTween.fromT, camTween.toT, e);
			camera.position.lerpVectors(camTween.fromP, camTween.toP, e);
			if (camTween.t >= 1) camTween = null;
		}
		controls.update();
		renderer.render(scene, camera);
		labels.render(scene, camera);
		requestAnimationFrame(frame);
	}

	function resize() {
		const w = stage.clientWidth, h = stage.clientHeight;
		renderer.setSize(w, h);
		labels.setSize(w, h);
		camera.aspect = w / Math.max(h, 1);
		camera.updateProjectionMatrix();
	}
	window.addEventListener('resize', resize);
	resize();
	requestAnimationFrame(frame);
	return { show };
}

render(slideFromHash(), true);
start().then((m) => {
	model = m;
	if (model && current >= 0) model.show(SLIDES[current], true);
});
</script>
</body>
</html>
'''


def page():
	blob = json.dumps(data(), ensure_ascii=True, separators=(",", ":"))
	blob = blob.replace("</", "<\\/")
	return PAGE.replace("@@THREE@@", THREE_VERSION).replace("@@DATA@@", blob)


if __name__ == "__main__":
	sys.stdout.write(page())
