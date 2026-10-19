#!/usr/bin/env python3
#
# doccheck.py: run the examples of a document, so that what the
# document says shtetl prints is what it prints.
#
# The document marks its examples:
#
# - <!-- example: NAME --> starts an example: an empty configuration
#   root of its own.
# - A ```ucl block whose first line is "# PATH", PATH ending in .conf,
#   is that file of the root, written whole, the comment line too. A
#   later block for the same PATH replaces it. A ucl block without
#   the line is an illustration, and not run.
# - A ```console block holds commands, each on a line
#   "$ shtetl show ...", and below each what it prints, standard error
#   and standard output together, in the order a terminal shows them
#   (shtetl's messages come before what it prints). The command runs
#   as "build/docshow ROOT ...": the daemon's show (sh_show) over the
#   example's root, a directory, since there is no daemon to ask on
#   the machine that checks. "$ echo $?" prints the exit status of the
#   command before it.
#
# usage: python3 tools/doccheck.py [--update] [doc] [docshow]
#
# --update writes what each command printed into the document instead
# of comparing, for review with git diff before it is committed.
#
# ASCII only.

import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

FILE = re.compile(r"^# ([A-Za-z0-9_][A-Za-z0-9_./-]*\.conf)$")
EXAMPLE = re.compile(r"^<!-- example: ([a-z0-9-]+) -->$")


def fences(lines):
	"""Each fenced block: (info, first line, end line), 0-based."""
	i = 0
	while i < len(lines):
		if lines[i].startswith("```"):
			info = lines[i][3:].strip()
			j = i + 1
			while j < len(lines) and lines[j] != "```":
				j += 1
			if j == len(lines):
				sys.exit("doccheck: line %d: a block never closed"
				    % (i + 1))
			yield (info, i + 1, j)
			i = j + 1
		else:
			i += 1


def run(docshow, root, cmd, last):
	"""What CMD prints, and its exit status."""
	if cmd == "echo $?":
		return ("%d\n" % last, 0)
	words = shlex.split(cmd)
	if words[:2] != ["shtetl", "show"]:
		raise ValueError("not shtetl show: " + cmd)
	p = subprocess.run([docshow, root] + words[2:],
	    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=root)
	return (p.stdout.decode("ascii", "backslashreplace"), p.returncode)


def main():
	args = sys.argv[1:]
	update = "--update" in args
	args = [a for a in args if a != "--update"]
	doc = args[0] if len(args) > 0 else "docs/configuration.md"
	shtetl = os.path.abspath(args[1] if len(args) > 1 else
	    "build/docshow")
	if not os.access(shtetl, os.X_OK):
		sys.exit("doccheck: no %s: run make doc" % shtetl)
	with open(doc) as f:
		lines = f.read().split("\n")

	starts = {}
	for i, line in enumerate(lines):
		m = EXAMPLE.match(line)
		if m:
			starts[i] = m.group(1)
	tmp = tempfile.mkdtemp(prefix="shtetl-doc.")
	root = None
	name = None
	fails = 0
	cases = 0
	out = []		# the document, rewritten for --update
	at = 0
	try:
		blocks = list(fences(lines))
		marks = sorted(starts)
		for info, first, end in blocks:
			# an example begun since the last block
			for k in marks:
				if at <= k < first:
					name = starts[k]
					root = os.path.join(tmp, name)
					if os.path.exists(root):
						sys.exit("doccheck: example %s "
						    "twice" % name)
					os.makedirs(root)
			body = lines[first:end]
			lang = info.split()[0] if info else ""
			if lang == "ucl" and body and FILE.match(body[0]):
				if root is None:
					sys.exit("doccheck: line %d: a file "
					    "before any example" % first)
				path = os.path.join(root,
				    FILE.match(body[0]).group(1))
				os.makedirs(os.path.dirname(path),
				    exist_ok=True)
				with open(path, "w") as f:
					f.write("\n".join(body) + "\n")
			if lang != "console":
				out.extend(lines[at:end + 1])
				at = end + 1
				continue
			if root is None:
				sys.exit("doccheck: line %d: a command before "
				    "any example" % first)
			out.extend(lines[at:first])
			last = 0
			i = 0
			while i < len(body):
				if not body[i].startswith("$ "):
					sys.exit("doccheck: line %d: output "
					    "with no command" % (first + i + 1))
				cmd = body[i][2:]
				j = i + 1
				while j < len(body) and \
				    not body[j].startswith("$ "):
					j += 1
				want = "\n".join(body[i + 1:j])
				got, last = run(shtetl, root, cmd, last)
				got = got.rstrip("\n")
				cases += 1
				out.append(body[i])
				if update:
					if got:
						out.extend(got.split("\n"))
				else:
					out.extend(body[i + 1:j])
					if got != want:
						fails += 1
						print("doccheck: %s, line %d: "
						    "%s\n--- the document\n%s\n"
						    "--- shtetl\n%s\n" % (name,
						    first + i + 1, cmd, want,
						    got))
				i = j
			out.append(lines[end])
			at = end + 1
		out.extend(lines[at:])
	finally:
		shutil.rmtree(tmp)
	if update:
		with open(doc, "w") as f:
			f.write("\n".join(out))
		print("doccheck: %s: %d commands written" % (doc, cases))
		return 0
	if fails:
		print("doccheck: %d of %d commands differ" % (fails, cases))
		return 1
	print("doccheck: %s: %d commands, %d examples" % (doc, cases,
	    len(starts)))
	return 0


if __name__ == "__main__":
	sys.exit(main())
