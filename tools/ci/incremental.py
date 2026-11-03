#!/usr/bin/env python3
"""Check a repeated mcpp build against its actual compilation outputs.

The first build warms the selected graph. The identical second build must
leave its existing compilation outputs untouched. This catches generated
sources invalidating every importer even when their bytes do not change.
Wall time is reported, not used as a runner-dependent pass/fail threshold.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def build(command, stem):
    started = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
    elapsed = time.monotonic() - started
    stem.with_name(stem.name + '.events.omui.jsonl').write_text(result.stdout, encoding='utf-8')
    log = stem.with_name(stem.name + '.log')
    log.write_text(result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f"mcpp failed ({result.returncode}); see {log}")
    directories = set()
    for line in result.stdout.splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get('event') == 'plan':
            directories.add(Path(event['configuration']['outputDir']))
    return directories, elapsed


def outputs(directories):
    found = {}
    missing = set()
    for directory in directories:
        database = directory / 'compile_commands.json'
        for entry in json.loads(database.read_text(encoding='utf-8')):
            if 'output' not in entry:
                raise RuntimeError(f"A compilation has no output in {database}")
            path = Path(entry['output'])
            if not path.is_absolute():
                path = Path(entry['directory']) / path
            path = path.resolve()
            try:
                stat = path.stat()
            except FileNotFoundError:
                # A cache-served module may have a database entry without a
                # local object. Report the coverage rather than invent one.
                missing.add(str(path))
                continue
            found[str(path)] = (stat.st_mtime_ns, stat.st_size)
    if not found:
        raise RuntimeError('The compilation databases contain no existing outputs')
    return found, missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--package', action='append', default=[])
    selection.add_argument('--workspace', action='store_true')
    parser.add_argument('--exclude', action='append', default=[])
    parser.add_argument('--profile', default='dev')
    parser.add_argument('--toolchain')
    parser.add_argument('--locked', action='store_true')
    parser.add_argument('--report', type=Path, default=Path('target/incremental.report.omui.json'))
    args = parser.parse_args()
    if args.exclude and not args.workspace:
        parser.error('--exclude requires --workspace')
    command = [os.environ.get('MCPP', 'mcpp'), 'build', '--profile', args.profile,
               '--message-format', 'json']
    if args.workspace:
        command.append('--workspace')
    for package in args.package:
        command.extend(['-p', package])
    for package in args.exclude:
        command.extend(['--exclude', package])
    if args.toolchain:
        command.extend(['--toolchain', args.toolchain])
    if args.locked:
        command.append('--locked')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    report = {'ok': False, 'command': command}
    prefix = args.report.name.removesuffix('.report.omui.json')
    try:
        directories, report['warm_seconds'] = build(command, args.report.parent / (prefix + '-warm'))
        if not directories:
            raise RuntimeError('mcpp did not identify its build configuration')
        before, missing = outputs(directories)
        repeated, report['repeat_seconds'] = build(command, args.report.parent / (prefix + '-repeat'))
        if repeated and repeated != directories:
            raise RuntimeError('An unchanged command selected a different build configuration')
        after, missing_after = outputs(directories)
        changed = sorted(path for path in before.keys() | after.keys() if before.get(path) != after.get(path))
        report.update(compilation_outputs=len(before), missing_outputs=len(missing),
                      changed_outputs=changed, coverage_unchanged=missing == missing_after)
        report['ok'] = not changed and missing == missing_after
        print(f"incremental: {len(changed)} / {len(before)} compilation outputs changed; "
              f"repeat {report['repeat_seconds']:.2f}s (warm {report['warm_seconds']:.2f}s)")
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        report['error'] = str(error)
        print(f'incremental: {error}', file=sys.stderr)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if not report['ok']:
        print(f'::error::unchanged compilation check failed ({args.report})', file=sys.stderr)
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
