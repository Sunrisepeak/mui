#!/usr/bin/env python3
"""Validate animation-first authoring using only the public hanim umbrella."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def digest(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mcpp', default=os.environ.get('MCPP', 'mcpp'))
    parser.add_argument('--keep-project', action='store_true')
    args = parser.parse_args()
    repo = args.source_root.resolve()
    fixture = Path(__file__).resolve().parent
    output = args.output.resolve(); output.parent.mkdir(parents=True, exist_ok=True)
    artifacts = output.parent / (output.stem + '-artifacts'); artifacts.mkdir(exist_ok=True)
    project = Path(tempfile.mkdtemp(prefix='hanim-api-probe-'))
    lock = digest(repo/'mcpp.lock')
    report = {'format':'hanim.public-authoring@0.2', 'ok':False, 'source_root':str(repo),
              'source_commit':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
              'project':str(project), 'source_lock_before':lock, 'commands':[], 'profiles':[],
              'fixture_sha256':{str(p.relative_to(fixture)):digest(p) for p in sorted([fixture/'run.py',fixture/'mcpp.toml.in',*fixture.glob('src/*')])},
              'scope':['Public import hanim; no consumer facade or second compiler/player.',
                       'Real strict contour/transform/fill morph and parameter effects only.',
                       'ObjectRef uses borrowed Engine registry; external hosts use explicit Snapshot inputs.',
                       'No renderer, GPU, move/reveal, general effect scopes or Agent provider.',
                       'No hot-path performance/allocation or sanitizer claim.']}
    def run(label, argv, bootstrap=False):
        env=os.environ.copy(); env.setdefault('MCPP_JOBS','2')
        if bootstrap:env.pop('MCPP_LOCKED',None)
        else:env['MCPP_LOCKED']='1'
        started=time.monotonic()
        process=subprocess.run(argv,cwd=project,env=env,capture_output=True,text=True)
        stdout=artifacts/(label+'.stdout.log');stderr=artifacts/(label+'.stderr.log')
        stdout.write_text(process.stdout);stderr.write_text(process.stderr)
        report['commands'].append({'argv':argv,'exit_code':process.returncode,'seconds':time.monotonic()-started,
                                   'stdout':str(stdout),'stderr':str(stderr)})
        output.write_text(json.dumps(report,indent=2)+'\n')
        if process.returncode:raise RuntimeError(f'{label}: exit {process.returncode}; see {stderr}')
        return process.stdout
    try:
        (project/'src').mkdir()
        text=(fixture/'mcpp.toml.in').read_text().replace('{{hanim_path}}',json.dumps(str(repo/'modules/gfx/hanim')))
        (project/'mcpp.toml').write_text(text)
        for p in fixture.glob('src/*'):shutil.copyfile(p,project/'src'/p.name)
        shutil.copyfile(project/'mcpp.toml',artifacts/'consumer.mcpp.toml')
        run('bootstrap',[args.mcpp,'build','--profile','dev'],True)
        temp_lock=digest(project/'mcpp.lock');report['temporary_lock_sha256']=temp_lock
        report['temporary_lock_present']=temp_lock is not None
        for profile in ['dev','release']:
            stdout=run(profile,[args.mcpp,'run','--locked','--profile',profile])
            results=[json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
            result=next((x for x in results if x.get('kind')=='hanim.api-probe'),None)
            if not result or not result['ok'] or not result['checks'] or not all(x['ok'] for x in result['checks']):
                raise RuntimeError(f'{profile}: missing successful check report')
            run(profile+'-metadata',[args.mcpp,'build','--locked','--profile',profile,'--configure-only'])
            resolution=max((project/'target').glob('*/*/resolution.json'),key=lambda p:p.stat().st_mtime_ns)
            data=json.loads(resolution.read_text());names=[]
            if data.get('schema_version')!=2:raise RuntimeError('unsupported resolution schema')
            for entry in data['graph']['packages']:
                p=entry['package'];name=(p.get('namespace','')+'.'+p['name']).lstrip('.');names.append(name)
                if name in {'openmui.core','openmui.sdk','openmui.testing','openlifa.openmui','openmui.spec'} or name.startswith(('openmui.ui.','openmui.host.','openmui.site','openmui.rhi','openmui.render')):
                    raise RuntimeError(f'forbidden UI/renderer dependency: {name}')
            # mcpp's existing default namespace normalizes the unqualified
            # package hanim to mcpplibs.hanim; the module/import stays hanim.
            umbrella=next((p['package'] for p in data['graph']['packages']
                           if p['package']['name']=='hanim'),None)
            if not umbrella or umbrella['source']!='path+'+str(repo/'modules/gfx/hanim'):
                raise RuntimeError('umbrella did not resolve to the public source package')
            if not {'mcpplibs.hanim','hanim.engine','hanim.program','hanim.library','hanim.effect','hanim.object','hanim.core'}.issubset(names):
                raise RuntimeError('real public hanim package missing')
            cdb=resolution.parent/'compile_commands.json';entries=json.loads(cdb.read_text())
            sources=[];source_hashes={}
            for item in entries:
                p=Path(item['file']);directory=Path(item['directory'])
                directory=directory if directory.is_absolute() else project/directory
                p=(p if p.is_absolute() else directory/p).resolve();sources.append(str(p))
                if p.is_relative_to(repo) and p.relative_to(repo).parts[:2] not in {('modules','base'),('modules','gfx')}:
                    raise RuntimeError(f'non-independent repository source: {p}')
                if p.is_relative_to(repo):source_hashes[str(p.relative_to(repo))]=digest(p)
                if p.is_relative_to(project/'src') and '-std=c++23' not in item.get('arguments',[]):
                    raise RuntimeError('consumer was not compiled as C++23')
            if not {str(project/'src/main.cpp')}.issubset(sources):
                raise RuntimeError('actual compilation omitted consumer source')
            shutil.copyfile(resolution,artifacts/(profile+'.resolution.json'))
            shutil.copyfile(cdb,artifacts/(profile+'.compile_commands.json'))
            if digest(project/'mcpp.lock')!=temp_lock:raise RuntimeError('consumer lock changed')
            report['profiles'].append({'profile':profile,'result':result,'packages':sorted(names),'compiled_sources':sources,
                                       'source_files_sha256':source_hashes,'toolchain':data['toolchain'],
                                       'resolution':str(artifacts/(profile+'.resolution.json')),'cdb':str(artifacts/(profile+'.compile_commands.json'))})
        report['ok']=True
    except (OSError,RuntimeError,KeyError,ValueError) as error:
        report['error']=str(error)
    finally:
        report['source_lock_after']=digest(repo/'mcpp.lock')
        if report['source_lock_after']!=lock:report.update(ok=False,error='source repository lock changed')
        report['project_retained']=args.keep_project
        output.write_text(json.dumps(report,indent=2)+'\n')
        if not args.keep_project:shutil.rmtree(project)
    print(json.dumps({'ok':report['ok'],'report':str(output),'profiles':len(report['profiles']),
                      **({'error':report['error']} if 'error' in report else {})}))
    return 0 if report['ok'] else 1


if __name__=='__main__':raise SystemExit(main())
