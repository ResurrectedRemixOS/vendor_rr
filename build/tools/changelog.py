#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: The Resurrected Remix Project
# SPDX-License-Identifier: Apache-2.0
#
# Lists the newest Resurrected Remix commits for the OTA json: commits a project carries on top
# of the revision the manifest pins it to (refs/remotes/m/*), so upstream history is skipped.

import argparse
import json
import os
import subprocess
import sys
import time

# RR repos that are not in the manifest yet. Every commit in them is RR's.
EXTRA_PROJECTS = ['packages/apps/Remixer', 'vendor/extras']


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

    try:
        with open('.repo/project.list') as f:
            projects = [line.strip() for line in f if line.strip()]
    except OSError:
        print('changelog: no .repo/project.list, skipping', file=sys.stderr)
        projects = []
    projects += [p for p in EXTRA_PROJECTS if p not in projects]

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
