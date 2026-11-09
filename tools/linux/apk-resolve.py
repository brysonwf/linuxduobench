#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Resolve Alpine packages + all their dependencies from one or more APKINDEX files (no apk-tools needed).
Prints one "<repo> <name>-<version>.apk" line per package, dependencies included, base minirootfs packages excluded
when a list of already-installed names is given.
usage: python3 -I apk-resolve.py <APKINDEX[:repo]>... -- <pkg>... [--installed <file with names>]
repo is the index's directory name (main/community) used to build the download URL."""
import sys

args = sys.argv[1:]
sep = args.index('--')
indexes, rest = args[:sep], args[sep + 1:]
installed = set()
if '--installed' in rest:
    i = rest.index('--installed')
    installed = set(open(rest[i + 1]).read().split())
    rest = rest[:i] + rest[i + 2:]

pkgs, provides = {}, {}
for spec in indexes:
    path, _, repo = spec.partition(':')
    cur = {}
    for line in open(path, encoding='utf-8', errors='replace').read().split('\n') + ['']:
        if not line:
            if cur.get('P') and cur['P'] not in pkgs:
                cur['repo'] = repo or 'main'
                pkgs[cur['P']] = cur
                for prov in cur.get('p', '').split():
                    provides.setdefault(prov.split('=')[0], cur['P'])
            cur = {}
            continue
        k, _, v = line.partition(':')
        cur[k] = v

def resolve(dep):
    dep = dep.lstrip('!')
    for op in ('>=', '<=', '>', '<', '=', '~'):
        dep = dep.split(op)[0]
    if dep in pkgs:
        return dep
    return provides.get(dep)

want, seen = list(rest), set()
while want:
    d = want.pop()
    if d.startswith('!'):
        continue
    n = resolve(d)
    if not n or n in seen or n in installed:
        if not n:
            print('# unresolved: %s' % d, file=sys.stderr)
        continue
    seen.add(n)
    want.extend(pkgs[n].get('D', '').split())
for n in sorted(seen):
    print('%s %s-%s.apk' % (pkgs[n]['repo'], n, pkgs[n]['V']))
