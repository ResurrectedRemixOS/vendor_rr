#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: The Resurrected Remix Project
# SPDX-License-Identifier: Apache-2.0
#
# Writes <device>_changelog.txt in AlphaDroid's layout: a block per day, then each repo's commits.
# RR's repos are standalone checkouts (a real .git directory; repo-managed ones use a symlink), so
# .repo/project.list would miss them. Commits on top of refs/remotes/m/* count when a repo still
# has that ref, so upstream history is skipped.
#
# With --device, other devices' repos are left out. A repo is a device's when its path names the
# device or its platform (device/oneplus/sm8550-common names sm8550), or when that device's trees
# point at it (hardware/oplus, vendor/oneplus/dolby). The rest is ROM source and every device keeps it.

import argparse
import os
import re
import subprocess
import sys
import time
from collections import defaultdict

SKIP_TOP = {'.repo', 'out', 'prebuilts'}
SKIP_PROJECTS = {'vendor/OTA'}
MAX_DEPTH = 6
REF = re.compile(r'(?<![\w.-])(?:device|kernel|vendor|hardware)/[\w.+/-]+')


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


def words(path):
    return set(re.split(r'[/_-]', path.lower()))


def tree_names(projects):
    """The first word of every device/<oem>/<name> tree: dodge, sm8750 from sm8750-common, zumapro."""
    names = set()
    for path in projects:
        parts = path.split('/')
        if len(parts) == 3 and parts[0] == 'device' and any(
                n.startswith('BoardConfig') or n == 'AndroidProducts.mk' for n in os.listdir(path)):
            names.add(re.split(r'[_-]', parts[2].lower())[0])
    return names


def device_side(path):
    """Trees, kernels, blobs and OEM HALs. vendor/rr, vendor/extras and vendor/gms are the ROM's."""
    parts = path.split('/')
    return len(parts) > 2 if parts[0] == 'vendor' else parts[0] in ('device', 'kernel', 'hardware')


def references(path):
    """Repo paths a tree's makefiles and blueprints name, comments left out."""
    found = set()
    out = git(path, 'grep', '-h', '-I', '-E', '(device|kernel|vendor|hardware)/', '--', '*.mk', '*.bp')
    for line in out.splitlines():
        if not line.lstrip().startswith(('#', '//')):
            found.update(m.rstrip('/.') for m in REF.findall(line))
    return found


def points_at(refs, path):
    return any(r == path or r.startswith(path + '/') for r in refs)


def other_devices(projects, device):
    """The repos that belong to devices other than this one."""
    names = tree_names(projects)
    named = {p: words(p) & names for p in projects}
    refs = {p: references(p) for p in projects
            if device_side(p) and p.split('/')[0] in ('device', 'vendor')}
    # A device-side repo that names no device is a device's only when another tree points at it.
    # Nothing points at the rest, so every device keeps them. A repo inside another goes with it.
    owned = {p for p in projects if named[p] or device_side(p) and any(
        points_at(r, p) for q, r in refs.items() if q != p and not p.startswith(q + '/'))}
    owned |= {p for p in projects if any(p.startswith(q + '/') for q in owned)}
    mine, want = set(), {device}
    while True:
        pointed = set().union(*(refs.get(p, ()) for p in mine))
        new = {p for p in owned - mine if named[p] & want or points_at(pointed, p)
               or any(p.startswith(m + '/') for m in mine)}
        if not new:
            return owned - mine
        mine |= new
        for p in new:
            want |= named[p]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True)
    p.add_argument('--days', type=int, default=20)
    p.add_argument('--device', help="leave out other devices' repos")
    args = p.parse_args()

    since = int(time.time()) - args.days * 86400
    days = defaultdict(lambda: defaultdict(list))
    projects = [p for p in sorted(standalone_projects()) if p not in SKIP_PROJECTS]
    others = other_devices(projects, args.device) if args.device else set()
    for path in projects:
        if path in others:
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
