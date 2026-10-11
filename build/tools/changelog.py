#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: The Resurrected Remix Project
# SPDX-License-Identifier: Apache-2.0
#
# Writes <device>_changelog.txt in AlphaDroid's layout: a block per day, then each repo's commits.
# RR's repos are standalone checkouts (a real .git directory; repo-managed ones use a symlink), so
# .repo/project.list would miss them. Commits on top of refs/remotes/m/* count when a repo still
# has that ref, so upstream history is skipped.

import argparse
import os
import subprocess
import sys
import time
from collections import defaultdict

SKIP_TOP = {'.repo', 'out', 'prebuilts'}
SKIP_PROJECTS = {'vendor/OTA'}
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
    log = git(path, 'log', '--no-merges', f'--since=@{since}', '--format=%ct %h %s', revs)
    out = []
    for line in log.splitlines():
        stamp, _, rest = line.partition(' ')
        if stamp.isdigit() and rest:
            out.append((int(stamp), rest))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True)
    p.add_argument('--days', type=int, default=20)
    args = p.parse_args()

    since = int(time.time()) - args.days * 86400
    days = defaultdict(lambda: defaultdict(list))
    for path in sorted(standalone_projects()):
        if path in SKIP_PROJECTS:
            continue
        for stamp, line in commits(path, since):
            days[time.strftime('%Y-%m-%d', time.localtime(stamp))][path].append(line)

    with open(args.out, 'w') as f:
        for day in sorted(days, reverse=True):
            f.write('====================\n')
            f.write('     ' + time.strftime('%m-%d-%Y', time.strptime(day, '%Y-%m-%d')) + '\n')
            f.write('====================\n')
            for path in sorted(days[day]):
                f.write(f'\n   * {path}\n')
                f.write('\n'.join(days[day][path]) + '\n')
            f.write('\n')
    count = sum(len(lines) for repos in days.values() for lines in repos.values())
    print(f'changelog: {count} changes over {len(days)} days written to {args.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
