"""Generate the existing MicroPython project QSTR overlay without Node.js."""
import json
from pathlib import Path
import re
import sys

material, target = map(Path, sys.argv[1:])
owner = Path(__file__).resolve().parents[1]
original = (material / 'generated/embed/genhdr/qstrdefs.generated.h').read_text()
known = set(re.findall(r'QDEF[01]\((MP_QSTR\w*),', original))
excluded = {'MP_QSTRnumber_of', 'MP_QSTRnumber_of_static', 'MP_QSTRstart_of_main',
            'MP_QSTR_maximum_space_recursion_space_depth_space_exceeded'}
names = set()
for directory in (material / 'generated/embed/py', owner / 'port'):
    for source in directory.rglob('*'):
        if source.suffix in ('.c', '.h'):
            names.update(set(re.findall(r'\bMP_QSTR_\w+', source.read_text())) - known - excluded)
symbols = {'_lt_': '<', '_gt_': '>', '_dot_': '.', '_hyphen_': '-', '_slash_': '/',
           '_space_': ' ', '_star_': '*', '_colon_': ':', '_equals_': '='}
rows = []
for name in sorted(names):
    value = name[8:]
    for symbol, character in symbols.items():
        value = value.replace(symbol, character)
    digest = 5381
    for byte in value.encode():
        digest = ((digest * 33) ^ byte) & 65535
    rows.append(f'QDEF1({name}, {digest or 1}, {len(value.encode())}, {json.dumps(value, ensure_ascii=False)})')
rows += [line for line in original.split('\n') if line.startswith('QDEF1(')]


def encoded_value(row):
    literal = re.search(r', "(.*)"\)$', row)[1]
    literal = re.sub(r'\\x([a-fA-F0-9]{2})', r'\\u00\1', literal)
    return json.loads('"' + literal + '"').encode()


rows.sort(key=encoded_value)
content = '\n'.join(line for line in original.split('\n') if not line.startswith('QDEF1(')) + '\n' + '\n'.join(rows) + '\n'
output = target / 'genhdr/qstrdefs.generated.h'
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != content:
    output.write_text(content)
