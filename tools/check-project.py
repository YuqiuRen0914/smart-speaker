"""Audit the standalone component boundary and optional IDF build source paths."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IGNORED = {'.git', '.build', 'build', 'managed_components', '__pycache__', '.local'}


def inside(path, root):
    return path == root or root in path.parents


def audit(build_dir=None):
    errors = []
    component_text = (ROOT / 'cmake/components.cmake').read_text()
    block = re.search(r'set\(speaker_component_paths\s+(.*?)\n\)', component_text, re.S)[1]
    components = block.split()
    if len(components) != len(set(components)):
        errors.append('Duplicate local component directory')
    for relative in components:
        if not (ROOT / relative / 'CMakeLists.txt').is_file():
            errors.append('Missing local component: ' + relative)
    for directory, subdirs, files in os.walk(ROOT):
        subdirs[:] = [name for name in subdirs if name not in IGNORED]
        for name in subdirs + files:
            source = Path(directory) / name
            if source.is_symlink():
                errors.append('Symlink in distributable source: ' + str(source.relative_to(ROOT)))
        # Immutable upstream documentation may contain their own example paths.
        if 'upstream' in Path(directory).relative_to(ROOT).parts:
            continue
        for name in files:
            source = Path(directory) / name
            if source.suffix not in {'.cmake', '.cpp', '.h', '.hpp', '.py', '.mjs', '.yml'} and name != 'CMakeLists.txt':
                continue
            if source == Path(__file__).resolve():
                continue
            text = source.read_text()
            if re.search(r'LUNAREDU_SDK_ROOT|/Users/[^/\s]+/|lunar-hardware-new|apps/(?:products|examples)/', text):
                errors.append('External repository reference: ' + str(source.relative_to(ROOT)))
            if source.relative_to(ROOT).parts[0] in ('lunaros', 'components', 'components_device'):
                if re.search(r'(?:#\s*include\s*["<][^">]*|\$\{[^}]+\}/)main/(?:product|smart_speaker)', text):
                    errors.append('Reverse product dependency: ' + str(source.relative_to(ROOT)))
    subprocess.run(['cmake', '-P', str(ROOT / 'lunaros/source_ownership.cmake')], check=True)
    subprocess.run([os.sys.executable, str(ROOT / 'tools/check-generated.py'),
                    str(ROOT / 'lunaros/services/required/service_network/src/ports/espnow/generated.json'), str(ROOT)], check=True)
    if build_dir:
        build_dir = build_dir.resolve()
        description = json.loads((build_dir / 'project_description.json').read_text())
        idf = Path(description['idf_path']).resolve()
        if Path(description['project_path']).resolve() != ROOT:
            errors.append('Build belongs to a different checkout')
        allowed = (ROOT, idf)
        local = set()
        for name, info in description['build_component_info'].items():
            path = Path(info['dir']).resolve()
            if not any(inside(path, base) for base in allowed):
                errors.append('External component: ' + name)
            if inside(path, ROOT) and not inside(path, ROOT / 'managed_components'):
                local.add(str(path.relative_to(ROOT)))
        if local != set(components) | {'main'}:
            errors.append('Built local component closure differs from cmake/components.cmake: ' +
                          str(sorted(local ^ (set(components) | {'main'}))))
        commands = json.loads((build_dir / 'compile_commands.json').read_text())
        for command in commands:
            source = Path(command['file'])
            if not source.is_absolute():
                source = Path(command['directory']) / source
            if not any(inside(source.resolve(), base) for base in allowed):
                errors.append('External compilation source: ' + str(source))
        print(f'Build paths: {len(commands)} translation units checked')
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'Standalone boundary: {len(components)} local components; OS ownership and generated port verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    audit(parser.parse_args().build_dir)
