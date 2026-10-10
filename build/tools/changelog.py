#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: The Resurrected Remix Project
# SPDX-License-Identifier: Apache-2.0
#
# Lists the newest Resurrected Remix commits for the OTA json. RR's repos are standalone checkouts
# (a real .git directory; repo-managed ones use a symlink). Commits on top of refs/remotes/m/*
# count when a repo still has that ref, so upstream history is skipped.

import argparse
import json
import os
import subprocess
import sys
import time

SKIP_TOP = {'.repo', 'out', 'prebuilts'}
MAX_DEPTH = 6


def standalone_projects(top='.', depth=0):
    found = []
    try:
        entries = list(os.scandir(top))
    except OSError:
        return found
    for entry in entries:
        if entry.name == '.git' and entry.is_dir(follow_symlinks=False):
            found.append(os.path.relpath(top))
    if depth >= MAX_DEPTH:
        return found
    for entry in entries:
        if not entry.is_dir(follow_symlinks=False) or entry.name == '.git':
            continue
        if depth == 0 and entry.name in SKIP_TOP:
            continue
        found += standalone_projects(entry.path, depth + 1)
    return found


def git(path, *args):
    result = subprocess.run(['git', '-C', path, *args], capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else ''


def manifest_ref(path):
    refs = git(path, 'for-each-ref', '--format=%(refname)', 'refs/remotes/m').split()
    return refs[0] if refs else None


def commits(path, since):
    base = manifest_ref(path)
    revs = f'{base}..HEAD' if base else 'HEAD'
    log = git(path, 'log', '--no-merges', f'--since=@{since}', '--format=%ct%x1f%s', revs)
    out = []
    for line in log.splitlines():
        stamp, _, subject = line.partition('\x1f')
        if stamp.isdigit() and subject:
            out.append({'title': subject.strip(), 'repo': path, 'date': int(stamp)})
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True)
    p.add_argument('--count', type=int, default=20)
    p.add_argument('--days', type=int, default=30)
    args = p.parse_args()

    projects = sorted(standalone_projects())

    since = int(time.time()) - args.days * 86400
    entries = []
    for path in projects:
        if os.path.isdir(path):
            entries += commits(path, since)

    entries.sort(key=lambda e: e['date'], reverse=True)
    seen = set()
    newest = []
    for entry in entries:
        if entry['title'] in seen:
            continue
        seen.add(entry['title'])
        newest.append(entry)
        if len(newest) == args.count:
            break

    with open(args.out, 'w') as f:
        json.dump(newest, f, indent=2)
    print(f'changelog: {len(newest)} changes written to {args.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
