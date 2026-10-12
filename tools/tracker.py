#!/usr/bin/env python3
# Local tracker ("jira") tool for shtetl's JSON, after the one of the
# zfs rebase project (scripts/tracker.py there). Preserves key
# order, validates statuses against _schema.status, and APPENDS to
# notes by default -- the note field is single-valued and clobbering
# it has already eaten one reminder (module-stub-v2's FreeBSD
# Makefile note, recovered from a session transcript).
#
# Usage:
#   tracker.py list
#   tracker.py show <issue-key>
#   tracker.py status <issue-key> <new-status>
#   tracker.py note <issue-key> "text to append"
#   tracker.py note --replace <issue-key> "replacement text"
#
# Default file: tracker/shtetl.json relative
# to the project root (this script's parent directory). Override
# with --file <path> before the subcommand.

import collections
import json
import os
import sys


def load(path):
    with open(path) as f:
        return json.load(f, object_pairs_hook=collections.OrderedDict)


def save(path, data):
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)
        f.write('\n')


def issue(data, key):
    if key.startswith('_') or key not in data:
        sys.exit('no such issue: %s (try: tracker.py list)' % key)
    return data[key]


def statuses(data):
    vocab = data.get('_schema', {}).get('status', {})
    return [k for k in vocab if k != 'order']


def main():
    args = sys.argv[1:]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(root, 'tracker', 'shtetl.json')
    if args and args[0] == '--file':
        path = args[1]
        args = args[2:]
    if not args:
        sys.exit(__doc__ or 'usage: tracker.py '
            'list|show|status|note ...')

    cmd, rest = args[0], args[1:]
    data = load(path)

    if cmd == 'list':
        width = max(len(k) for k in data if not k.startswith('_'))
        for k, v in data.items():
            if k.startswith('_'):
                continue
            print('%-*s  %s' % (width, k, v.get('completed', '?')))
    elif cmd == 'show':
        print(json.dumps(issue(data, rest[0]), indent=2))
    elif cmd == 'status':
        key, new = rest[0], rest[1]
        vocab = statuses(data)
        if vocab and new not in vocab:
            sys.exit('bad status %r; one of: %s' %
                (new, ', '.join(vocab)))
        old = issue(data, key).get('completed')
        data[key]['completed'] = new
        save(path, data)
        print('%s: %s -> %s' % (key, old, new))
    elif cmd == 'note':
        replace = rest and rest[0] == '--replace'
        if replace:
            rest = rest[1:]
        key, text = rest[0], rest[1]
        it = issue(data, key)
        old = it.get('note', '')
        it['note'] = text if (replace or not old) else \
            old.rstrip() + ' ' + text
        save(path, data)
        print('%s: note %s (%d chars total)' %
            (key, 'replaced' if replace else 'appended',
            len(it['note'])))
    else:
        sys.exit('unknown command: %s' % cmd)


if __name__ == '__main__':
    main()
