#!/usr/bin/env python3
"""Render route-specific opening assets with the actual native site program.

The mcpp launch envelope supplies the executable and its runtime environment.
Only the selected page's picture and geometry are requested by the bootstrap;
the catalogue contains references, never embedded images. PNGs are copied
without any image transformation. Their accompanying bounds come from the
same production semantic tree.
"""

import argparse
import hashlib
from itertools import product
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


# These are asset dependencies of the current product openings. Detail routes
# without their own capture keep readable HTML rather than show another case.
OPENINGS = {
    'home': 'main/body/view/home/page/landing/scene/visual',
    'components': 'main/body/view/components/page/wall',
    'motion': 'main/body/view/explore/page/works/grid/mark',
    'docs/motion': 'main/body/view/motion/page/wall',
    'docs/styles': 'main/body/view/styles/page/styles_feature/comparison',
    'docs/interface': 'main/body/view/interface/page/interface_feature/workspace/object',
    'agent': 'main/body/view/agent/page/agent_feature/specimen',
}
VIEWPORTS = ((390, 844), (1280, 800))


def hashed_copy(source, directory, extension):
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    destination = directory / ('opening-' + digest[:20] + extension)
    if not destination.exists():
        shutil.copyfile(source, destination)
    return 'assets/' + destination.name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--launch', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    launch = json.loads(args.launch.read_text(encoding='utf-8'))
    if launch.get('kind') != 'mcpp.launch' or launch.get('kindVersion') != 1:
        parser.error('expected an mcpp.launch version 1 envelope')
    command = launch['data']
    if command.get('via') is not None:
        parser.error('opening assets require a directly executable native site')
    environment = dict(os.environ, **command.get('env', {}))
    output = args.out.resolve()
    assets = output / 'assets'
    assets.mkdir(parents=True, exist_ok=True)
    catalogue = {'version': 1, 'basis': 'native-reference', 'previews': [], 'references': []}
    with tempfile.TemporaryDirectory(prefix='omui-site-openings-') as scratch:
        scratch = Path(scratch)
        for language in ('en', 'zh'):
            for (width, height), reduced in product(VIEWPORTS, (False, True)):
                for route, root in OPENINGS.items():
                    png = scratch / 'opening.png'
                    argv = [command['program'], '--first-frame', str(png), '--route', route,
                            '--lang', language, '--width', str(width), '--height', str(height), '--root', root]
                    if reduced:
                        argv.append('--reduced-motion')
                    result = subprocess.run(argv, cwd=command.get('cwd'), env=environment,
                                            text=True, capture_output=True, timeout=120)
                    if result.returncode:
                        raise RuntimeError(f'capture {language}/{route} at {width}: {result.stdout} {result.stderr}')
                    geometry = png.with_suffix('.frame.omui.json')
                    record = json.loads(geometry.read_text(encoding='utf-8'))
                    if not record.get('nodes') or record.get('root') != root:
                        raise RuntimeError(f'capture has no production geometry: {language}/{route}')
                    # The CLI report records its output paths. Those local
                    # temporary names are not inputs to a public asset hash.
                    record.pop('png', None)
                    record.pop('sidecar', None)
                    geometry.write_text(json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n', encoding='utf-8')
                    entry = {'route': route, 'language': language, 'viewport': [width, height],
                             'motion': 'reduced' if reduced else 'full',
                             'image': hashed_copy(png, assets, '.png'),
                             'geometry': hashed_copy(geometry, assets, '.frame.omui.json')}
                    catalogue['previews'].append(entry)
                # Full-size fixed-time frames are independent verification
                # references, not another visual layer above the live page.
                png = scratch / 'reference.png'
                argv = [command['program'], '--first-frame', str(png), '--route', 'home',
                        '--lang', language, '--width', str(width), '--height', str(height)]
                if reduced:
                    argv.append('--reduced-motion')
                result = subprocess.run(argv, cwd=command.get('cwd'), env=environment,
                                        text=True, capture_output=True, timeout=120)
                if result.returncode:
                    raise RuntimeError(f'home reference: {result.stderr}')
                geometry = png.with_suffix('.frame.omui.json')
                record = json.loads(geometry.read_text(encoding='utf-8'))
                record.pop('png', None)
                record.pop('sidecar', None)
                geometry.write_text(json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n', encoding='utf-8')
                catalogue['references'].append({
                    'route': 'home', 'language': language, 'viewport': [width, height],
                    'motion': 'reduced' if reduced else 'full',
                    'image': hashed_copy(png, assets, '.png'),
                    'geometry': hashed_copy(geometry, assets, '.frame.omui.json')})
    destination = output / 'first-frames.assets.omui.json'
    destination.write_text(json.dumps(catalogue, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    print(f'site openings: {len(catalogue["previews"])} page pictures, '
          f'{len(catalogue["references"])} full-frame references; {destination}')


if __name__ == '__main__':
    main()
