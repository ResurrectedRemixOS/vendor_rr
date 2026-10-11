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


DEVICE_FIELDS = ('forum', 'gapps', 'firmware', 'modem', 'bootloader', 'recovery', 'paypal',
                 'telegram', 'dt', 'common-dt', 'kernel')


def read_response(path):
    try:
        with open(path) as f:
            response = json.load(f).get('response')
    except (OSError, ValueError, AttributeError):
        return []
    return response if isinstance(response, list) else []


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
    p.add_argument('--ota-dir', default='vendor/OTA',
                   help='OTA repo checkout; its <device>.json supplies the device fields')
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

    out = os.path.join(args.product_out, f'{args.device}.json')
    official = os.path.join(args.ota_dir, f'{args.device}.json')

    # The device fields (oem, links, trees) come from the published json in the OTA repo.
    device_fields = {}
    published = read_response(official)
    if published:
        device_fields = published[0]

    def field(key):
        return device_fields.get(key, '')

    md5, sha256 = digests(args.zip)
    item = {
        'maintainer': args.maintainer,
        'oem': field('oem'),
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
        'os_sdk_level': int(system.get('ro.build.version.sdk', '0') or 0),
        'os_patch_level': system.get('ro.build.version.security_patch', ''),
        'changelog': changelog,
    }
    for key in DEVICE_FIELDS:
        item[key] = field(key)

    # Keep the other variants: start from the last json in OUT, else the published one, and
    # replace this variant's entry.
    response = read_response(out) if os.path.isfile(out) else published
    for i, entry in enumerate(response):
        if entry.get('buildvariant', entry.get('buildtype')) == args.variant:
            response[i] = item
            break
    else:
        response.append(item)

    with open(out, 'w') as f:
        json.dump({'response': response}, f, indent=4)
    if not args.url:
        print('createjson: RR_OTA_DOWNLOAD_URL is unset, the json has no download link',
              file=sys.stderr)
    if not published:
        print(f'createjson: no {official}, so the device fields are empty. See '
              'https://github.com/ResurrectedRemixOS/OTA for official support.', file=sys.stderr)
    print(f'createjson: wrote {out}', file=sys.stderr)


if __name__ == '__main__':
    main()
