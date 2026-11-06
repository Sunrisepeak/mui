#!/usr/bin/env python3
"""Exercise public hanim features and audit their actual target source closures."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import time


def sha(path: Path) -> str | None:
    # A std/path-only project legitimately needs no registry lock file.
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


CORE = '''
    Motion<Vec2> move({0,0});
    move.tween_to(seconds(0), {40,20}, seconds(1), ease::linear);
    require(move.value(seconds(.5)) == Vec2{20,10});
    require(move.value(seconds(0)) == Vec2{0,0});
'''
EFFECT = '''
    auto effects = take(PreparedEffects::prepare(fx::chain({fx::blur(0,8)})));
    std::array<float,1> values{};
    take(effects.sample(.5, values)); require(values[0] == 4);
    take(effects.sample(0, values)); require(values[0] == 0);
'''
OBJECT = '''
    const std::array path_verbs{mlibs::geom::PathVerb::move,mlibs::geom::PathVerb::line,mlibs::geom::PathVerb::close};
    const std::array path_points{mlibs::geom::Point{0,0},mlibs::geom::Point{1,1}};
    auto imported=take(import_path({path_verbs,path_points}));
    require(imported.contours.size()==1 && imported.contours[0].geometry.segment_count()==1);
    auto objects = take(ObjectRegistry::make());
    auto a = take(objects.create({{{.id="item"}}}));
    auto b = take(objects.create({{{.id="item",.local={.tx=40}}}}));
    auto from = take(objects.capture(a)), to = take(objects.capture(b));
    require(from.parts()[0].local.tx == 0 && to.parts()[0].local.tx == 40);
'''
LIBRARY = '''
    auto morph = take(PreparedObjectMorph::prepare(from,to));
    std::vector<FramePart> parts(morph.requirements().parts);
    std::vector<ShapeCubic> cubics(morph.requirements().cubics);
    auto frame = take(morph.sample(.5,{parts,cubics}));
    require(frame.parts[0].local.tx == 20);
'''
PROGRAM = '''
    auto program = take(PreparedProgram::prepare(anim::morph(from,to).duration(seconds(1))));
    auto current = take(program.sample(0,seconds(.5),{parts,cubics},{}));
    require(current.geometry.parts[0].local.tx == 20);
'''
ENGINE = '''
    auto engine = take(Engine::make());
    auto playback = take(engine.play(anim::morph(from,to).duration(seconds(1))));
    take(engine.seek(playback,seconds(.5)));
    require(take(engine.frame(playback)).geometry.parts[0].local.tx == 20);
    take(engine.release(playback));
'''
VECTOR = '''
    auto renderer = take(vector_render::PreparedVector::bind(morph));
    require(renderer.requirements().world_transforms == morph.requirements().parts);
'''
CONTROL = '''
    auto access=std::make_shared<mlibs::control::Access>(mlibs::control::policy_for_profile("dev"));
    mlibs::control::InvocationContext context;
    hanim::control::Controller controller(engine,access,context);
    mlibs::control::Invocation request{.remote=true,.access=access,.session=access->open_session()};
    const auto reply=controller.list(request);
    require(reply && reply->executed && reply->value && reply->value->empty());
    require(hanim::control::operations().size()==13);
    const auto prepared_control=controller.prepare(request,anim::morph(from,to));
    require(prepared_control && prepared_control->value);
    const auto started=controller.play(request,*prepared_control->value);
    require(started && started->executed && started->value);
    const auto moved=controller.seek_progress(request,*started->value,.5);
    const auto observed=controller.inspect(request,*started->value);
    require(moved && moved->executed && observed && observed->value && observed->value->position==seconds(.5));
    auto preview=request;preview.dry_run=true;
    const auto dry=controller.seek_progress(preview,*started->value,.75);
    require(dry && !dry->executed && take(engine.inspect(*started->value)).position==seconds(.5));
    require(access->revoke_session(*request.session));
    const auto denied=controller.inspect(request,*started->value);
    require(!denied && denied.error().code==hanim::control::Error::admission);
    const auto refused_plan=controller.play(request,*prepared_control->value);
    require(!refused_plan && refused_plan.error().code==hanim::control::Error::admission);
'''
ROLES = {'object', 'effect', 'library', 'program', 'engine', 'render/vector', 'control'}
CASES = [
    ('core', [], set(), CORE),
    ('effect', ['effect'], {'effect'}, CORE + EFFECT),
    ('object', ['object'], {'object'}, CORE + OBJECT),
    ('library', ['library'], {'object', 'library'}, CORE + OBJECT + LIBRARY),
    ('program', ['program'], {'object', 'effect', 'library', 'program'}, CORE + OBJECT + LIBRARY + EFFECT + PROGRAM),
    ('default', None, ROLES - {'render/vector', 'control'}, CORE + OBJECT + LIBRARY + EFFECT + PROGRAM + ENGINE),
    ('render-vector', ['render-vector'], ROLES - {'engine', 'control'}, CORE + OBJECT + LIBRARY + PROGRAM + VECTOR),
    ('control', ['control'], ROLES - {'render/vector'}, CORE + OBJECT + LIBRARY + EFFECT + PROGRAM + ENGINE + CONTROL),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mcpp', default=os.environ.get('MCPP', 'mcpp'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--keep-project', action='store_true')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    product = repo / 'modules/projects/hanim'
    output = args.output.resolve(); output.parent.mkdir(parents=True, exist_ok=True)
    artifacts = output.parent / 'artifacts'; artifacts.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='hanim-feature-consumer-'))
    lock = sha(repo / 'mcpp.lock')
    report = {'ok': False, 'format': 'hanim.features@0.1', 'project': str(work),
              'source_lock_before': lock, 'cases': [], 'commands': [],
              'limits': ['Native release consumers; no clean-machine timing or GPU performance claim.',
                         'The default archive is tested with explicit source dependencies: mcpp pack omits local path-dependency edges.',
                         'The archive freezes selected features; this is not registry publication or binary feature reselection.']}

    def run(project: Path, label: str, command: list[str], bootstrap=False) -> str:
        env = os.environ.copy(); env.setdefault('MCPP_JOBS', '2')
        env.pop('DISPLAY', None); env.pop('WAYLAND_DISPLAY', None)
        if bootstrap: env.pop('MCPP_LOCKED', None)
        else: env['MCPP_LOCKED'] = '1'
        start = time.monotonic()
        result = subprocess.run([args.mcpp, *command], cwd=project, env=env, capture_output=True, text=True)
        log = artifacts / (project.name + '-' + label + '.log')
        log.write_text(result.stdout + '\nSTDERR\n' + result.stderr)
        report['commands'].append({'case': project.name, 'argv': command, 'exit': result.returncode,
                                   'seconds': time.monotonic()-start, 'log': str(log)})
        output.write_text(json.dumps(report, indent=2)+'\n')
        if result.returncode: raise RuntimeError('failed: '+str(log))
        return result.stdout

    try:
        for name, features, roles, body in CASES:
            project = work / name; (project / 'src').mkdir(parents=True)
            selection = '' if features is None else ', default-features = false, features = '+json.dumps(features)
            manifest = ('[package]\nnamespace="hanim.validation"\nname="feature_consumer"\nversion="0.1.0"\n'
                        '[targets.consumer]\nkind="bin"\nmain="src/main.cpp"\n[build]\nsources=["src/*.cpp"]\n'
                        '[dependencies]\nhanim = { path = '+json.dumps(str(product))+selection+' }\n')
            (project / 'mcpp.toml').write_text(manifest)
            (project / 'src/main.cpp').write_text('''import std;
import hanim;
using namespace hanim;
void require(bool value) { if (!value) std::exit(1); }
template<class T, class E> T take(std::expected<T,E> value) {
    require(value.has_value());
    if constexpr (!std::is_void_v<T>) return std::move(*value);
}
int main() {
'''+body+'\nstd::println("hanim feature consumer passed");\n}\n')
            run(project, 'bootstrap', ['run', '--profile', 'release'], True)
            own_lock = sha(project / 'mcpp.lock')
            if 'hanim feature consumer passed' not in run(project, 'repeat', ['run', '--profile', 'release', '--locked']):
                raise RuntimeError('consumer did not execute')
            run(project, 'metadata', ['build', '--profile', 'release', '--locked', '--configure-only'])
            resolution = max((project / 'target').glob('*/*/resolution.json'), key=lambda p:p.stat().st_mtime_ns)
            data = json.loads(resolution.read_text())
            if data.get('schema_version') != 2: raise RuntimeError('unknown resolution schema')
            packages = {p['package']['canonical'].split('@')[0] for p in data['graph']['packages'] if 'target' in p['instances']}
            if any(p.startswith(('openmui.ui.', 'openmui.host.')) or p in {'omui.core','omui.sdk','mlibs.testing','openlifa.openmui'} for p in packages):
                raise RuntimeError('UI or test dependency in public product')
            if name != 'render-vector' and any(p.startswith(('openmui.render','xrender','mlibs.rhi','xrender.raster','mlibs.shader')) for p in packages):
                raise RuntimeError('renderer enabled without opt-in')
            if (name == 'control') != ('mlibs.control' in packages):
                raise RuntimeError('control dependency did not follow the selected feature')
            if name == 'core' and packages != {'hanim.validation.feature_consumer','mcpplibs.hanim','hanim.core','mlibs.math'}:
                raise RuntimeError('core feature closure is not minimal: '+str(packages))
            if ('object' in roles) != ('mlibs.geom' in packages):
                raise RuntimeError('shared geometry must follow the object feature')
            cdb = resolution.parent / 'compile_commands.json'; entries = json.loads(cdb.read_text())
            sources = set()
            for entry in entries:
                path = Path(entry['file']); directory = Path(entry['directory'])
                if not directory.is_absolute(): directory = project / directory
                sources.add((path if path.is_absolute() else directory / path).resolve())
            expected = {product / 'src/hanim.cppm'}
            for role in roles: expected.update(p for p in (product / 'src' / role).iterdir() if p.suffix in {'.cpp','.cppm'})
            actual = {p for p in sources if p.is_relative_to(product / 'src')}
            if actual != expected: raise RuntimeError('feature source closure differs: '+name)
            selected = next(p for p in data['graph']['packages'] if p['package']['canonical'].startswith('mcpplibs.hanim@'))
            exports = Path(selected['outDir']) / 'selected.cppm'
            expected_exports = {'export import hanim.core;', *('export import hanim.'+x.replace('/','.')+';' for x in roles)}
            if set(x for x in exports.read_text().splitlines() if x.startswith('export import ')) != expected_exports:
                raise RuntimeError('public feature exports differ')
            shutil.copyfile(resolution, artifacts / (name+'.resolution.json'))
            shutil.copyfile(cdb, artifacts / (name+'.compile_commands.json'))
            shutil.copyfile(exports, artifacts / (name+'.selected.cppm'))
            shutil.copyfile(project/'src/main.cpp', artifacts/(name+'.cpp'))
            if sha(project/'mcpp.lock') != own_lock: raise RuntimeError('consumer lock changed')
            report['cases'].append({'name':name,'features':features,'packages':sorted(packages),
                                    'product_sources':sorted(str(p.relative_to(repo)) for p in actual),
                                    'public_exports': sorted(expected_exports)})
        # The optional public control path also executes without a renderer on
        # Web; this consumer needs no file-system or browser host shim.
        web=work/'control'
        with (web/'mcpp.toml').open('a') as f:
            f.write('''
[target.'cfg(os = "emscripten")'.abi]
exceptions=true
threads=false
''')
        run(web,'web-bootstrap',['build','--target','wasm32-emscripten','--profile','release','--configure-only'],True)
        web_lock=sha(web/'mcpp.lock')
        if 'hanim feature consumer passed' not in run(web,'web-run',['run','--target','wasm32-emscripten','--profile','release','--locked']):
            raise RuntimeError('Web control consumer did not execute')
        if sha(web/'mcpp.lock')!=web_lock:raise RuntimeError('Web control consumer lock changed')
        report['web_control']={'ok':True,'target':'wasm32-emscripten','source_sha256':sha(web/'src/main.cpp'),
                               'manifest_sha256':sha(web/'mcpp.toml')}
        packed = run(repo, 'pack', ['pack', '-p', 'hanim', '--profile', 'release', '--locked', '--message-format', 'json'])
        envelopes = []
        for offset, char in enumerate(packed):
            if char != '{': continue
            try: value, _ = json.JSONDecoder().raw_decode(packed[offset:])
            except json.JSONDecodeError: continue
            if isinstance(value, dict) and value.get('kind') == 'mcpp.pack': envelopes.append(value)
        if len(envelopes) != 1: raise RuntimeError('missing unique pack envelope')
        archive = Path(envelopes[0]['data']['artifacts'][0]['path'])
        installation = work / 'installation'; installation.mkdir()
        with tarfile.open(archive) as package: package.extractall(installation, filter='data')
        manifests = list(installation.rglob('mcpp.toml'))
        if len(manifests) != 1: raise RuntimeError('unexpected packed manifest set')
        installed = manifests[0].parent
        if list(installed.rglob('*.cpp')): raise RuntimeError('archive contains implementation sources')
        project = work / 'packed'; (project / 'src').mkdir(parents=True)
        manifest = ('[package]\nnamespace="hanim.validation"\nname="packed_consumer"\nversion="0.1.0"\n'
                    '[targets.consumer]\nkind="bin"\nmain="src/main.cpp"\n[build]\nsources=["src/*.cpp"]\n'
                    '[dependencies]\n"hanim" = { path = '+json.dumps(str(installed))+' }\n')
        for identity, relative in {'hanim.core':'hanim/core', 'mlibs.geom':'geom', 'mlibs.memory':'memory', 'mlibs.utils':'utils'}.items():
            manifest += json.dumps(identity)+' = { path = '+json.dumps(str(repo/'modules/base'/relative))+' }\n'
        (project/'mcpp.toml').write_text(manifest)
        shutil.copyfile(repo/'tools/ci/hanim-api-probe/src/main.cpp',project/'src/main.cpp')
        text = run(project, 'bootstrap', ['run','--profile','release'], True)
        results = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
        result = next(x for x in results if x.get('kind') == 'hanim.api-probe')
        if not result['ok'] or not result['checks'] or not all(x['ok'] for x in result['checks']):
            raise RuntimeError('packed public authoring checks failed')
        run(project, 'repeat', ['run','--profile','release','--locked'])
        run(project, 'metadata', ['build','--profile','release','--locked','--configure-only'])
        resolution = max((project/'target').glob('*/*/resolution.json'),key=lambda p:p.stat().st_mtime_ns)
        cdb = resolution.parent/'compile_commands.json'; entries=json.loads(cdb.read_text())
        for entry in entries:
            path=Path(entry['file']); directory=Path(entry['directory'])
            if not directory.is_absolute(): directory=project/directory
            path=(path if path.is_absolute() else directory/path).resolve()
            if path.is_relative_to(product): raise RuntimeError('packed consumer compiled original product sources')
        shutil.copyfile(resolution,artifacts/'packed.resolution.json')
        shutil.copyfile(cdb,artifacts/'packed.compile_commands.json')
        report['packed']={'archive':str(archive),'sha256':sha(archive),'bytes':archive.stat().st_size,
                          'checks':len(result['checks']),'result':result,
                          'installed_interfaces':sorted(str(p.relative_to(installed)) for p in installed.rglob('*.cppm'))}
        report['ok'] = True
    except (OSError, ValueError, RuntimeError, KeyError, StopIteration) as error:
        report['error'] = str(error)
    finally:
        report['source_lock_after'] = sha(repo / 'mcpp.lock')
        if report['source_lock_after'] != lock: report.update(ok=False,error='source lock changed')
        output.write_text(json.dumps(report, indent=2)+'\n')
        if not args.keep_project: shutil.rmtree(work)
    print(json.dumps({'ok':report['ok'],'report':str(output),'cases':len(report['cases']), 'error':report.get('error')}))
    return 0 if report['ok'] else 1


if __name__ == '__main__': raise SystemExit(main())
