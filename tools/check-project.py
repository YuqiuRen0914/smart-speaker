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
    # This baseline keeps LunarOS as one IDF component, with domain owners
    # selected by its original subsystem and desktop source declarations.
    for relative in ('lunaros/src/subsystems.cmake', 'lunaros/src/desktop/sources.cmake'):
        if not (ROOT / relative).is_file():
            errors.append('Missing source ownership declaration: ' + relative)
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
        # IRAM annotations alone do not prove cache safety: inspect the linked
        # image, including the GPIO callee, transition table and ISR state.
        compiler = Path(description['c_compiler'])
        objdump = compiler.with_name(compiler.name.replace('-gcc', '-objdump'))
        symbols = subprocess.check_output(
            [str(objdump), '-t', str(build_dir / description['app_elf'])], text=True)
        sections = {row.split()[-1]: row.split()[-3] for row in symbols.splitlines()
                    if len(row.split()) >= 6}
        internal = {
            '_ZN13smart_speaker6detail11encoder_isrEPv': '.iram',
            'gpio_get_level': '.iram',
            'xTaskGetTickCountFromISR': '.iram',
            '_ZZN13smart_speaker6detail11encoder_isrEPvE11kTransition': '.dram',
            '_ZN13smart_speaker6detail14g_encoder_lockE': '.dram',
            '_ZN13smart_speaker6detail15g_encoder_deltaE': '.dram',
            '_ZN13smart_speaker6detail15g_encoder_stateE': '.dram',
            '_ZN13smart_speaker6detail24g_encoder_last_edge_tickE': '.dram',
        }
        for symbol, prefix in internal.items():
            if not sections.get(symbol, '').startswith(prefix):
                errors.append('Encoder ISR cache safety: ' + symbol + ' in ' +
                              sections.get(symbol, 'missing'))
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'Standalone boundary: {len(components)} local components; baseline subsystem ownership verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    audit(parser.parse_args().build_dir)
