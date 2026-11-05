#!/usr/bin/env python3
"""Run the optional public UI/Hanim adapter from a fresh headless application."""
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
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mcpp',default=os.environ.get('MCPP','mcpp'))
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--web',action='store_true')
    parser.add_argument('--keep-project',action='store_true')
    parser.add_argument('--project',type=Path,help='Reuse a previously generated /tmp/omui-hanim-consumer-* build')
    args=parser.parse_args()
    fixture=Path(__file__).resolve().parent;repo=fixture.parents[2]
    output=args.output.resolve();output.parent.mkdir(parents=True,exist_ok=True)
    artifacts=output.parent/(output.stem+'-artifacts');artifacts.mkdir(exist_ok=True)
    project=args.project.resolve() if args.project else Path(tempfile.mkdtemp(prefix='omui-hanim-consumer-'))
    if args.project and (project.parent!=Path('/tmp') or not project.name.startswith('omui-hanim-consumer-') or not (project/'mcpp.toml').is_file()):
        parser.error('--project must be an existing generated consumer workspace')
    lock=digest(repo/'mcpp.lock')
    report={'format':'omui.hanim-presentation@0.1','ok':False,'project':str(project),'reused_project':args.project is not None,
            'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
            'source_lock_before':lock,'commands':[],'profiles':[],
            'fixture_sha256':{str(p.relative_to(fixture)):digest(p) for p in [fixture/'run.py',fixture/'mcpp.toml.in',fixture/'src/main.cpp']},
            'scope':['Local presentation offsets and reusable host transport; public Engine and TimeMap remain the execution/time owners.',
                     'Actual standard button, shaped text, mouse and Agent semantic activation without GUI/display server.',
                     'Projected target anchors follow in the current frame; complete sampled-layout constraints remain out of scope.',
                     'Host controls, visibility/interaction/preferences, frame demand and arbitrary clock sampling are exercised without a window.',
                     'No claim of complete UI geometry capture or 3D/image-effect support.']}

    def run(label: str,argv: list[str],bootstrap=False,expected_exit=0,overrides=None) -> str:
        env=os.environ.copy();env.setdefault('MCPP_JOBS','2');env.pop('DISPLAY',None);env.pop('WAYLAND_DISPLAY',None)
        # Out-of-tree release binaries intentionally cannot rely on debug
        # source paths to discover assets. This test supplies its real fonts.
        env['OMUI_FONTS']=str(repo/'modules/content/text/fonts')
        if overrides:env.update(overrides)
        if bootstrap:env.pop('MCPP_LOCKED',None)
        else:env['MCPP_LOCKED']='1'
        start=time.monotonic();process=subprocess.run([args.mcpp,*argv],cwd=project,env=env,capture_output=True,text=True)
        log=artifacts/(label+'.log');log.write_text(process.stdout+'\nSTDERR\n'+process.stderr)
        report['commands'].append({'argv':argv,'exit':process.returncode,'expected_exit':expected_exit,
                                   'seconds':time.monotonic()-start,'log':str(log)})
        output.write_text(json.dumps(report,indent=2)+'\n')
        if process.returncode!=expected_exit:raise RuntimeError('failed: '+str(log))
        return process.stdout

    def metadata(profile: str):
        # The IDE database includes optional/inactive units. Audit the actual
        # build DAG instead, and identify the requested target/profile without
        # depending on mtimes of files mcpp may leave byte-identical.
        matches=[];optimization={'dev':'-O0','release':'-O2','wasm-test':'-O1'}[profile]
        for resolution in (project/'target').glob('*/*/resolution.json'):
            data=json.loads(resolution.read_text());cdb=resolution.parent/'compile_commands.json'
            entries=json.loads(cdb.read_text())
            main=next((c for c in entries if Path(c['file']).resolve()==project/'src/main.cpp'),None)
            if main and optimization in main['arguments'] and (data['toolchain']['triple'].startswith('wasm')==(profile=='wasm-test')):
                matches.append((resolution,data,cdb,entries))
        if len(matches)!=1:raise RuntimeError('ambiguous target/profile metadata: '+profile)
        resolution,data,cdb,entries=matches[0];ninja=resolution.parent/'build.ninja'
        outputs=set()
        for line in ninja.read_text().splitlines():
            if line.startswith('build '):outputs.update(line[6:].split(':',1)[0].split())
        active=[]
        for entry in entries:
            output=Path(entry['output'])
            if output.is_absolute():
                if not output.is_relative_to(resolution.parent):continue
                output=output.relative_to(resolution.parent)
            if output.as_posix() in outputs:active.append(entry)
        if not active:raise RuntimeError('no active compile units found')
        return resolution,data,cdb,active,ninja

    try:
        (project/'src').mkdir(exist_ok=True)
        manifest=(fixture/'mcpp.toml.in').read_text()
        for key,path in {'view':'modules/ui/view','components':'components/base','raster':'modules/gfx/raster',
                         'image':'modules/base/image','hash':'modules/base/hash'}.items():
            manifest=manifest.replace('{{'+key+'_path}}',json.dumps(str(repo/path)))
        (project/'mcpp.toml').write_text(manifest);shutil.copyfile(fixture/'src/main.cpp',project/'src/main.cpp')
        unlocked=not (project/'mcpp.lock').is_file()
        run('bootstrap',['build','--profile','dev',*([] if unlocked else ['--locked'])],unlocked)
        if args.web:
            # Resolve the second platform into this consumer's own lock before
            # freezing it. The source workspace lock is never a bootstrap input.
            run('bootstrap-web',['build','--target','wasm32-emscripten','--profile','wasm-test','--configure-only'],True)
        consumer_lock=digest(project/'mcpp.lock')
        cases=[('dev',[]),('release',[])]+([('wasm-test',['--target','wasm32-emscripten'])] if args.web else [])
        for profile,target in cases:
            images=artifacts/profile;images.mkdir(exist_ok=True)
            stdout=run(profile,['run','--locked','--profile',profile,*target,'--',str(images)])
            records=[json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
            result=next(row for row in records if row.get('kind')=='omui.hanim-consumer')
            if not result['ok'] or len(result['frames'])!=61 or len(result['driver_frames'])!=61:raise RuntimeError('consumer did not complete the real task')
            for frame in result['frames']+result['driver_frames']:
                path=Path(frame['png']);frame['png_sha256']=digest(path)
                if not path.is_relative_to(images) or path.stat().st_size<100:raise RuntimeError('invalid pixel artifact')
            run(profile+'-metadata',['build','--locked','--profile',profile,*target,'--configure-only'])
            resolution,data,cdb,commands,ninja=metadata(profile)
            packages=sorted(p['package']['canonical'].split('@')[0] for p in data['graph']['packages'] if 'target' in p['instances'])
            if 'openmui.view' not in packages or 'mcpplibs.hanim' not in packages or 'openmui.sdk' in packages or 'openmui.site' in packages:
                raise RuntimeError('unexpected application dependency closure')
            sources=[]
            for command in commands:
                path=Path(command['file']);directory=Path(command['directory'])
                if not directory.is_absolute():directory=project/directory
                sources.append((path if path.is_absolute() else directory/path).resolve())
            if repo/'modules/ui/view/src/hanim/bridge.cpp' not in sources or repo/'modules/gfx/hanim/src/engine/engine.cpp' not in sources:
                raise RuntimeError('optional public adapter/player sources absent')
            shutil.copyfile(resolution,artifacts/(profile+'.resolution.json'));shutil.copyfile(cdb,artifacts/(profile+'.compile_commands.json'))
            (artifacts/(profile+'.active-compile_commands.json')).write_text(json.dumps(commands,indent=2)+'\n')
            shutil.copyfile(ninja,artifacts/(profile+'.build.ninja'))
            report['profiles'].append({'profile':profile,'result':result,'packages':packages,
                'source_files_sha256':{str(p.relative_to(repo)):digest(p) for p in sorted(set(sources)) if p.is_relative_to(repo)}})
            if digest(project/'mcpp.lock')!=consumer_lock:raise RuntimeError('consumer lock changed')
        reference=report['profiles'][0]['result']['frames']+report['profiles'][0]['result']['driver_frames']
        for profile in report['profiles'][1:]:
            frames=profile['result']['frames']+profile['result']['driver_frames']
            if any(a['pixel_fnv1a']!=b['pixel_fnv1a'] or a['png_sha256']!=b['png_sha256'] for a,b in zip(reference,frames)):
                raise RuntimeError('profile process pixels differ')
        stdout=run('missing-fonts',['run','--locked','--profile','release','--',str(artifacts/'release')],
                   expected_exit=1,overrides={'OMUI_FONTS':str(project/'missing-fonts')})
        missing=next(json.loads(line) for line in stdout.splitlines() if line.startswith('{'))
        if missing['ok'] or missing['frames'] or not any(c['name']=='font_resources_loaded' and not c['ok'] for c in missing['checks']):
            raise RuntimeError('missing resources were reported as a visual success')
        report['missing_fonts_refused']=True
        # A real second application in the same disposable workspace: turning
        # off the adapter must remove its player sources from the link graph.
        (project/'mcpp.toml').write_text(manifest.replace(', features = ["hanim"]',''))
        (project/'src/main.cpp').write_text('''import std;
import openmui.view;
int main() {
    omui::Runtime runtime;
    runtime.mount("main",omui::box({.width=20,.height=20}));
    const auto frame=runtime.frame({hanim::seconds(0),{}});
    if(!frame.failure.empty())return 1;
    std::println("omui.view-without-player: ok");
}
''')
        stdout=run('without-player',['run','--locked','--profile','dev'])
        if 'omui.view-without-player: ok' not in stdout:raise RuntimeError('feature-off application did not run')
        run('without-player-metadata',['build','--locked','--profile','dev','--configure-only'])
        resolution,data,cdb,commands,ninja=metadata('dev')
        if any('/src/hanim/bridge.' in c['file'] or '/hanim/src/engine/' in c['file'] for c in commands):
            raise RuntimeError('optional adapter/player leaked into ordinary view build')
        shutil.copyfile(resolution,artifacts/'without-player.resolution.json')
        shutil.copyfile(cdb,artifacts/'without-player.compile_commands.json')
        (artifacts/'without-player.active-compile_commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        shutil.copyfile(ninja,artifacts/'without-player.build.ninja')
        shutil.copyfile(project/'src/main.cpp',artifacts/'without-player.cpp')
        shutil.copyfile(project/'mcpp.toml',artifacts/'without-player.mcpp.toml')
        report['feature_off']={'ok':True,'compiled_units':len(commands),'adapter_and_engine_absent':True,
                               'source_sha256':digest(project/'src/main.cpp')}
        if digest(project/'mcpp.lock')!=consumer_lock:raise RuntimeError('feature-off consumer lock changed')
        report['source_lock_after']=digest(repo/'mcpp.lock')
        if report['source_lock_after']!=lock:raise RuntimeError('source lock changed')
        report['ok']=True
    except Exception as error:
        report['error']=str(error)
    finally:
        if 'manifest' in locals():
            (project/'mcpp.toml').write_text(manifest)
            shutil.copyfile(fixture/'src/main.cpp',project/'src/main.cpp')
        report['project_retained']=args.keep_project or not report['ok']
        output.write_text(json.dumps(report,indent=2)+'\n')
        if not report['project_retained']:shutil.rmtree(project)
    print(json.dumps({'ok':report['ok'],'report':str(output),'error':report.get('error')}))
    return 0 if report['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
