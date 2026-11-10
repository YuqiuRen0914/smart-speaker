"""Recreate the embed package offline with the pinned upstream generators.

Uses a C preprocessor plus MicroPython's own qstr/module/root-pointer scripts.
No external checkout, shell utilities, make, Docker or platform SDK required.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, sys, tempfile

component = Path(__file__).resolve().parents[1]
repo = component.parents[1]

def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)

def generate(output, compiler):
    metadata = json.loads((component/'UPSTREAM.json').read_text())
    generation = json.loads((component/'GENERATION.json').read_text())
    if generation['upstream_revision'] != metadata['revision']:
        raise RuntimeError('Mismatched generation source revision')
    for name, digest in generation['inputs'].items():
        if hashlib.sha256((component/name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Changed generation input; update the lock explicitly: '+name)
    upstream = component/'upstream'
    manifest = (component/'upstream.sha256').read_bytes()
    if hashlib.sha256(manifest).hexdigest() != metadata['manifest_sha256']:
        raise RuntimeError('Changed upstream manifest')
    expected=set()
    for line in manifest.decode().splitlines():
        digest, name = line.split('  ', 1)
        expected.add(name)
        if hashlib.sha256((upstream/name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Modified upstream source: '+name)
    if {p.relative_to(upstream).as_posix() for p in upstream.rglob('*') if p.is_file()} != expected:
        raise RuntimeError('Unexpected upstream file set')
    if not output.resolve().is_relative_to(repo.resolve()):
        raise RuntimeError('Generate into a new directory within this workspace')
    output.mkdir(parents=True, exist_ok=False)
    for source in sorted((upstream/'py').glob('*')):
        if source.suffix in ('.c', '.h'):
            dest=output/'py'/source.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
    for name in ('extmod/modplatform.h', 'shared/runtime/gchelper.h', 'shared/runtime/gchelper_generic.c'):
        dest=output/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(upstream/name, dest)
    shutil.copytree(upstream/'ports/embed/port', output/'port')
    # These four include removals are the audited pre-migration embed patch.
    run(['git', 'apply', '--directory='+output.relative_to(repo).as_posix(),
         component/'patches/embed-includes.patch'], cwd=repo)
    headers=output/'genhdr'
    headers.mkdir()
    environment=dict(os.environ, MICROPY_GIT_TAG=metadata['version_label'],
                     MICROPY_GIT_HASH=metadata['revision'], SOURCE_DATE_EPOCH='1777680000')
    run([sys.executable, upstream/'py/makeversionhdr.py', headers/'mpversion.h'], env=environment)
    with tempfile.TemporaryDirectory(prefix='embed-generators-', dir=repo/'.build') as folder:
        build=Path(folder)
        preprocessed=build/'sources.i'
        flags=['-I'+str(component/'tools'), '-I'+str(component/'include'),
               '-I'+str(output), '-I'+str(upstream/'ports/embed'), '-std=c99', '-DNO_QSTR']
        # Exactly the source collection used by the official embed.mk; disabled
        # emitters/architectures disappear during preprocessing.
        sources=sorted((output/'py').glob('*.c'))
        run([sys.executable, upstream/'py/makeqstrdefs.py', 'pp', compiler, '-E',
             'output', preprocessed, 'cflags', *flags, 'cxxflags', 'sources',
             *sources, 'dependencies', 'changed_sources'])
        collected={}
        for mode in ('qstr', 'module', 'root_pointer'):
            split=build/mode
            split.mkdir()
            combined=build/(mode+'.collected')
            run([sys.executable, upstream/'py/makeqstrdefs.py', 'split', mode, preprocessed, split, '_'])
            run([sys.executable, upstream/'py/makeqstrdefs.py', 'cat', mode, '_', split, combined])
            collected[mode]=combined
        definitions=(upstream/'py/qstrdefs.h').read_text()+collected['qstr'].read_text()
        definitions=re.sub(r'^(Q\(.*\))$', r'"\1"', definitions, flags=re.MULTILINE)
        result=run([compiler, '-E', '-x', 'c', *flags[:-1], '-'], input=definitions.encode(), stdout=subprocess.PIPE)
        preprocessed_qstr=build/'qstrdefs.preprocessed.h'
        preprocessed_qstr.write_bytes(re.sub(rb'^"(Q\(.*\))"', rb'\1', result.stdout, flags=re.MULTILINE))
        for script, source, dest in (
            ('makeqstrdata.py', preprocessed_qstr, 'qstrdefs.generated.h'),
            ('makemoduledefs.py', collected['module'], 'moduledefs.h'),
            ('make_root_pointers.py', collected['root_pointer'], 'root_pointers.h')):
            with (headers/dest).open('wb') as target:
                run([sys.executable, upstream/'py'/script, source], stdout=target)
    # Canonical generated bytes are independent of the host's text newline.
    for file in output.rglob('*'):
        if file.is_file(): file.write_bytes(file.read_bytes().replace(b'\r\n', b'\n'))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    parser.add_argument('--check', action='store_true', help='Regenerate and compare all bytes without changing committed output')
    parser.add_argument('--output', type=Path, help='Write a new package into a non-existing directory')
    args=parser.parse_args()
    if args.check == bool(args.output): parser.error('Choose exactly one of --check or --output')
    (repo/'.build').mkdir(exist_ok=True)
    if args.check:
        with tempfile.TemporaryDirectory(prefix='embed-check-', dir=repo/'.build') as folder:
            output=Path(folder)/'embed'
            generate(output, args.cc)
            original=component/'generated/embed'
            paths=lambda base: {p.relative_to(base).as_posix():p.read_bytes() for p in base.rglob('*') if p.is_file()}
            actual, expected=paths(output),paths(original)
            mismatches=[p for p in sorted(actual.keys() | expected.keys()) if actual.get(p)!=expected.get(p)]
            if mismatches: raise RuntimeError('Non-reproducible embed files: '+', '.join(mismatches))
            print(f'PASS: all {len(actual)} embed files reproduced byte-for-byte')
    else:
        generate(args.output.resolve(), args.cc)
        print('Generated embed package: '+str(args.output.resolve()))

if __name__ == '__main__': main()
