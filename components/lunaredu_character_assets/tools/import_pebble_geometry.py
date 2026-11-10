"""Bake the user-selected reference's Pebble contour/eye projection for firmware.

Run explicitly with the Character Motion Lab replica base URL. Only geometric
data is retained; no browser runtime or app archive is bundled.
"""
import json
import math
from pathlib import Path
import re
import sys
from urllib.request import urlopen

source = urlopen(sys.argv[1].rstrip('/') + '/geometry-data.js', timeout=15).read().decode()
geo, _ = json.JSONDecoder().raw_decode(source[source.index('{'):])
shape = geo['shapes']['pebble']
radius = geo['Re']


def resample(points, count):
    closed = points + [points[0]]
    lengths = [0.0]
    for a, b in zip(closed, closed[1:]):
        lengths.append(lengths[-1] + math.dist(a, b))
    out, edge = [], 0
    for n in range(count):
        at = n * lengths[-1] / count
        while lengths[edge + 1] < at:
            edge += 1
        t = (at - lengths[edge]) / (lengths[edge + 1] - lengths[edge])
        out.append(tuple(a + (b - a) * t for a, b in zip(closed[edge], closed[edge + 1])))
    return out


tokens = re.findall(r'[MCZ]|-?\d+(?:\.\d+)?', shape['path'])
assert tokens[0] == 'M'
point = (float(tokens[1]), float(tokens[2]))
outline, at = [point], 3
while tokens[at] != 'Z':
    assert tokens[at] == 'C'
    values = list(map(float, tokens[at + 1:at + 7]))
    b, c, d = values[:2], values[2:4], values[4:]
    for n in range(1, 5):
        t, u = n / 4, 1 - n / 4
        outline.append(tuple(u**3 * point[j] + 3*u*u*t*b[j] + 3*u*t*t*c[j] + t**3*d[j] for j in range(2)))
    point = tuple(d)
    at += 7


def rotation(turn, tilt, roll):
    a, b, c = map(math.radians, (turn, tilt, roll))
    ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
    return [[cc*ca-sc*sb*sa, -sc*cb, cc*sa+sc*sb*ca],
            [sc*ca+cc*sb*sa, cc*cb, sc*sa-cc*sb*ca],
            [-cb*sa, sb, cb*ca]]


pose, home = rotation(17, -14, 29), rotation(33, -19, 38)
matrix = [[sum(pose[i][k] * home[j][k] for k in range(3)) for j in range(3)] for i in range(3)]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def span(y):
    hits = []
    for a, b in zip(outline, outline[1:] + outline[:1]):
        if (a[1] <= y < b[1]) or (b[1] <= y < a[1]):
            hits.append(a[0] + (b[0]-a[0])*(y-a[1])/(b[1]-a[1]))
    return (min(hits), max(hits)) if hits else (radius, radius)


def projected_eye(poly):
    cx = sum(p[0] for p in poly) / len(poly)
    cy = sum(p[1] for p in poly) / len(poly)
    x, y = (cx-radius)/radius, (radius-cy)/radius
    z = math.sqrt(max(0, 1-x*x-y*y)) or 0.02
    projected = [dot(row, (x, y, z)) for row in matrix]
    up = [-y*x, 1-y*y, -y*z]
    norm = math.sqrt(dot(up, up))
    up = [v/norm for v in up]
    right = [y*up[2]-z*up[1], z*up[0]-x*up[2], x*up[1]-y*up[0]]
    u = [dot(row, up) for row in matrix]
    r = [dot(row, right) for row in matrix]
    a, b, c, d = right[0], -right[1], up[0], -up[1]
    det = a*d-c*b
    km, fee = (r[0]*d-u[0]*b)/det, (-r[0]*c+u[0]*a)/det
    ree, zee = (-r[1]*d+u[1]*b)/det, (r[1]*c-u[1]*a)/det
    face = shape['face']
    px = radius + face['x'] + projected[0]*radius*face['sx']*1.18
    py = radius + face['y'] - projected[1]*radius*face['sy']
    eye_scale = 0.92 / 0.96
    xs = math.hypot(km, ree) * eye_scale * 0.96
    ys = math.hypot(fee, zee) * eye_scale * 0.92
    margin = 21 * ys + 2
    py = min(shape['bottom'] - margin, max(shape['top'] + margin, py))
    left, right = -math.inf, math.inf
    for p in poly[::2]:
        lo, hi = span(py + (p[1]-cy)*ys)
        left = max(left, lo - (p[0]-cx)*xs)
        right = min(right, hi - (p[0]-cx)*xs)
    px = min(right, max(left, px)) if left <= right else (left+right)/2
    return [((px-radius + (p[0]-cx)*km*eye_scale*0.96 + (p[1]-cy)*fee*eye_scale*0.92)/radius,
             (py-radius + (p[0]-cx)*ree*eye_scale*0.96 + (p[1]-cy)*zee*eye_scale*0.92)/radius)
            for p in resample(poly, 32)]


body = [((x-radius)/radius, (y-radius)/radius) for x, y in resample(outline, 64)]
expressions = [('Idle', 0), ('Curious', 3), ('Happy', 2), ('Sleeping', 13), ('Confused', 14), ('Listening', 10), ('Excited', 17), ('Surprised', 21), ('Playful', 11)]
lines = ['#pragma once', '', '// Geometry sampled from the user-selected Character Motion Lab replica.',
         '// Pebble contour and login eye projection; regenerate with tools/import_pebble_geometry.py.',
         '// Source: ' + sys.argv[1],
         'namespace lunaredu::character_assets::pebble {', 'struct Point { float x; float y; };']


def emit(name, points):
    lines.append(f'inline constexpr Point {name}[] = {{')
    for i in range(0, len(points), 4):
        lines.append('  ' + ', '.join('{%.6ff, %.6ff}' % p for p in points[i:i+4]) + ',')
    lines.append('};')


emit('kBody', body)
for name, index in expressions:
    for eye in range(2):
        emit(f'k{name}{eye}', projected_eye(geo['eyes'][index][eye]))
lines.append('}  // namespace lunaredu::character_assets::pebble')
dest = Path(__file__).resolve().parents[1] / 'include/lunaredu_character_assets_pebble_geometry.h'
dest.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('Baked Pebble contour and nine projected eye pairs:', dest)
