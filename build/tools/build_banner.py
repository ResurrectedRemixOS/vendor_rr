#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: The Resurrected Remix Project
# SPDX-License-Identifier: Apache-2.0
#
# Prints the Resurrected Remix banner when bacon finishes.

import argparse
import datetime
import getpass
import os
import socket
import sys

HOT = (0xFF, 0x4D, 0x76)
CRIMSON = (0xFF, 0x00, 0x3F)
DEEP = (0x84, 0x00, 0x09)
SHADOW = (0x5C, 0x0A, 0x1E)
EDGE = (0x7A, 0x14, 0x2E)
INK = (0xF2, 0xEE, 0xF0)
MUTED = (0x8E, 0x86, 0x8A)
OK = (0x3D, 0xDC, 0x84)

MARK = [
    '██████╗ ██████╗ ',
    '██╔══██╗██╔══██╗',
    '██████╔╝██████╔╝',
    '██╔══██╗██╔══██╗',
    '██║  ██║██║  ██║',
    '╚═╝  ╚═╝╚═╝  ╚═╝',
]
MARK_SHADE = '╗╔╝╚║═'
WORDMARK = 'R E S U R R E C T E D   R E M I X'
TAGLINE = 'back from the dead.'

COLOR = 'NO_COLOR' not in os.environ


def fg(rgb, text, bold=False, italic=False):
    if not COLOR or not text:
        return text
    attrs = ('\x1b[1m' if bold else '') + ('\x1b[3m' if italic else '')
    return f'{attrs}\x1b[38;2;{rgb[0]};{rgb[1]};{rgb[2]}m{text}\x1b[0m'


def ramp(stops, t):
    t = max(0.0, min(1.0, t))
    seg = t * (len(stops) - 1)
    i = min(int(seg), len(stops) - 2)
    f = seg - i
    a, b = stops[i], stops[i + 1]
    return tuple(round(a[c] + (b[c] - a[c]) * f) for c in range(3))


def mark():
    width = len(MARK[0])
    rows = len(MARK)
    lean = 0.35 * width / rows
    span = width + lean * (rows - 1)
    out = []
    for row, line in enumerate(MARK):
        buf = ''
        for col, ch in enumerate(line):
            if ch == ' ':
                buf += ch
            elif ch in MARK_SHADE:
                buf += fg(SHADOW, ch)
            else:
                buf += fg(ramp([HOT, CRIMSON, DEEP], (col + lean * row) / span), ch)
        out.append(buf)
    return out


def human_size(size):
    for unit in ('B', 'KB', 'MB'):
        if size < 1024:
            return f'{size:.0f} {unit}'
        size /= 1024
    return f'{size:.2f} GB'


def who():
    try:
        user = os.environ.get('BUILD_USERNAME') or getpass.getuser()
    except Exception:
        user = 'unknown'
    host = os.environ.get('BUILD_HOSTNAME') or socket.gethostname()
    return f'{user}@{host}'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--zip', required=True)
    p.add_argument('--device', required=True)
    p.add_argument('--model', default='')
    p.add_argument('--date', default='')
    p.add_argument('--buildtype', default='')
    p.add_argument('--android', default='')
    p.add_argument('--patch', default='')
    p.add_argument('--variant', default='')
    p.add_argument('--gms', default='')
    args = p.parse_args()

    try:
        size = human_size(os.path.getsize(args.zip))
    except OSError:
        size = '?'
    try:
        with open(args.zip + '.sha256sum') as f:
            sha = f.read().split()[0]
        sha = f'{sha[:12]}…{sha[-8:]}'
    except (OSError, IndexError):
        sha = '?'

    device = f'{args.model} · {args.device}' if args.model.strip() else args.device
    rows = [
        ('Device', device),
        ('Build', f'{args.date} · {args.buildtype}'),
        ('Android', f'{args.android} · patch {args.patch}'),
        ('Variant', f'{args.variant} · {"GApps" if args.gms == "true" else "Vanilla"}'),
        ('Size', size),
        ('SHA-256', sha),
        ('Built', f'{datetime.datetime.now():%a %d %b %Y %H:%M} · {who()}'),
    ]

    side = [
        fg(INK, WORDMARK, bold=True),
        ''.join(fg(ramp([CRIMSON, DEEP, SHADOW], i / (len(WORDMARK) - 1)), '─')
                for i in range(len(WORDMARK))),
        fg(MUTED, TAGLINE, italic=True),
        '',
        fg(OK, '✓ ', bold=True) + fg(INK, 'build complete', bold=True)
        + fg(MUTED, f'   {args.device} · {args.variant}'),
        '',
    ]

    out = ['']
    for art, text in zip(mark(), side):
        out.append('   ' + art + '    ' + text)
    out.append('')

    width = max(58, max(14 + len(v) for _, v in rows))
    out.append('   ' + fg(EDGE, '╭' + '─' * width + '╮'))
    for k, v in rows:
        out.append('   ' + fg(EDGE, '│') + '  ' + fg(MUTED, f'{k:<10}') + fg(INK, v)
                   + ' ' * (width - 12 - len(v)) + fg(EDGE, '│'))
    out.append('   ' + fg(EDGE, '╰' + '─' * width + '╯'))
    out.append('   ' + fg(CRIMSON, '➜ ', bold=True) + fg(MUTED, 'Package Complete: ')
               + fg(INK, args.zip))
    out.append('')

    sys.stdout.buffer.write(('\n'.join(out) + '\n').encode('utf-8'))
    sys.stdout.flush()


if __name__ == '__main__':
    main()
