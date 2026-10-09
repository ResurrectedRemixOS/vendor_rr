#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: The Resurrected Remix Project
# SPDX-License-Identifier: Apache-2.0
#
# Writes the Updater OTA json for a finished bacon zip: <PRODUCT_OUT>/<device>.json.

import argparse
import hashlib
import json
import os
import sys


def props(path):
    out = {}
    try:
        with open(path) as f:
            for line in f:
                key, sep, value = line.strip().partition('=')
                if sep and not key.startswith('#'):
                    out[key] = value
    except OSError:
        pass
    return out


def digests(path):
    md5, sha256 = hashlib.md5(), hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            md5.update(chunk)
            sha256.update(chunk)
    return md5.hexdigest(), sha256.hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--zip', required=True)
    p.add_argument('--device', required=True)
    p.add_argument('--product-out', required=True)
    p.add_argument('--version', default='')
    p.add_argument('--buildtype', default='')
    p.add_argument('--variant', default='')
    p.add_argument('--maintainer', default='')
    p.add_argument('--url', default='', help='download url, {device} and {filename} expand')
    p.add_argument('--changelog', default='')
    args = p.parse_args()

    system = props(os.path.join(args.product_out, 'system', 'build.prop'))
    filename = os.path.basename(args.zip)

    changelog = []
    if args.changelog:
        try:
            with open(args.changelog) as f:
                changelog = json.load(f)
        except (OSError, ValueError):
            print(f'createjson: could not read {args.changelog}', file=sys.stderr)

    md5, sha256 = digests(args.zip)
    item = {
        'maintainer': args.maintainer,
        'device': args.device,
        'filename': filename,
        'download': args.url.replace('{device}', args.device).replace('{filename}', filename),
        'timestamp': int(system.get('ro.system.build.date.utc', '0') or 0),
        'md5': md5,
        'sha256': sha256,
        'size': os.path.getsize(args.zip),
        'version': args.version,
        'buildtype': args.buildtype,
        'buildvariant': args.variant,
        'os_patch_level': system.get('ro.build.version.security_patch', ''),
        'os_sdk_level': int(system.get('ro.build.version.sdk', '0') or 0),
        'changelog': changelog,
    }

    out = os.path.join(args.product_out, f'{args.device}.json')
    with open(out, 'w') as f:
        json.dump({'response': [item]}, f, indent=2)
    if not args.url:
        print('createjson: RR_OTA_DOWNLOAD_URL is unset, the json has no download link',
              file=sys.stderr)
    print(f'createjson: wrote {out}', file=sys.stderr)


if __name__ == '__main__':
    main()
