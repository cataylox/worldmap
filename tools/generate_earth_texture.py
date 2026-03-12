#!/usr/bin/env python3
import math
import re
import sys
from pathlib import Path

WIDTH = 2048
HEIGHT = 1024

VERTEX_RE = re.compile(r'\{(-?\d+\.\d+),\s*(-?\d+\.\d+),\s*(-?\d+\.\d+)\}')
PAIR_RE = re.compile(r'\{(\d+),\s*(\d+)\}')


def extract_block(text: str, name: str) -> str:
    start = text.index(name)
    start = text.index('{', start)
    end = text.index('};', start)
    return text[start:end + 1]


def parse_world_header(path: Path):
    text = path.read_text(encoding='utf-8')
    vertices_block = extract_block(text, 'WORLD_VERTICES[]')
    rings_block = extract_block(text, 'WORLD_RINGS[]')
    polygons_block = extract_block(text, 'WORLD_POLYGONS[]')

    vertices = [(float(a), float(b)) for a, b, _ in VERTEX_RE.findall(vertices_block)]
    rings = [(int(a), int(b)) for a, b in PAIR_RE.findall(rings_block)]
    polygons = [(int(a), int(b)) for a, b in PAIR_RE.findall(polygons_block)]
    return vertices, rings, polygons


def unwrap_ring(points):
    if not points:
        return []
    result = [points[0]]
    current_lon = points[0][0]
    for lon, lat in points[1:]:
        while lon - current_lon > 180.0:
            lon -= 360.0
        while lon - current_lon < -180.0:
            lon += 360.0
        result.append((lon, lat))
        current_lon = lon
    return result


def lon_to_x(lon):
    return (lon + 180.0) / 360.0 * WIDTH


def fill_interval(mask_row, x0, x1):
    if x1 <= x0:
        return
    for shift in (-720.0, -360.0, 0.0, 360.0, 720.0):
        a = max(-180.0, x0 + shift)
        b = min(180.0, x1 + shift)
        if b <= a:
            continue
        start = max(0, int(math.floor(lon_to_x(a))))
        end = min(WIDTH, int(math.ceil(lon_to_x(b))))
        for x in range(start, end):
            mask_row[x] = True


def rasterize_land(vertices, rings, polygons):
    mask = [[False] * WIDTH for _ in range(HEIGHT)]
    polygon_rings = []

    for ring_start, ring_count in polygons:
        ring_set = []
        for index in range(ring_start, ring_start + ring_count):
            vertex_start, vertex_count = rings[index]
            ring_points = vertices[vertex_start:vertex_start + vertex_count]
            ring_set.append(unwrap_ring(ring_points))
        polygon_rings.append(ring_set)

    for y in range(HEIGHT):
        lat = 90.0 - (y + 0.5) * 180.0 / HEIGHT
        row = mask[y]
        for ring_set in polygon_rings:
            intersections = []
            for ring in ring_set:
                count = len(ring)
                for i in range(count):
                    lon1, lat1 = ring[i]
                    lon2, lat2 = ring[(i + 1) % count]
                    if lat1 == lat2:
                        continue
                    if (lat1 > lat) != (lat2 > lat):
                        t = (lat - lat1) / (lat2 - lat1)
                        intersections.append(lon1 + t * (lon2 - lon1))
            intersections.sort()
            for i in range(0, len(intersections) - 1, 2):
                fill_interval(row, intersections[i], intersections[i + 1])
    return mask


def pseudo_noise(lon, lat):
    return (
        math.sin(math.radians(lon * 3.1)) * 0.35
        + math.sin(math.radians(lat * 4.7 + lon * 0.8)) * 0.25
        + math.cos(math.radians(lon * 1.7 - lat * 2.3)) * 0.20
        + math.sin(math.radians(lon * 9.3 + lat * 6.1)) * 0.12
    )


def build_texture(mask):
    data = bytearray(WIDTH * HEIGHT * 3)
    for y in range(HEIGHT):
        lat = 90.0 - (y + 0.5) * 180.0 / HEIGHT
        for x in range(WIDTH):
            lon = (x + 0.5) * 360.0 / WIDTH - 180.0
            idx = (y * WIDTH + x) * 3
            noise = pseudo_noise(lon, lat)

            if mask[y][x]:
                polar = max(0.0, (abs(lat) - 58.0) / 24.0)
                tropical = max(0.0, 1.0 - abs(lat) / 28.0)
                arid_band = max(0.0, 1.0 - abs(abs(lat) - 24.0) / 12.0)
                moisture = 0.5 + noise * 0.5
                desert = arid_band * (1.0 - max(0.0, moisture))
                forest = tropical * max(0.0, moisture)

                r = 78 + 54 * desert + 10 * noise + 18 * forest + 120 * polar
                g = 112 + 28 * forest - 22 * desert + 18 * noise + 110 * polar
                b = 62 + 10 * forest - 16 * desert + 8 * noise + 105 * polar
            else:
                equatorial = 1.0 - abs(lat) / 90.0
                coast = 0
                for yy in range(max(0, y - 1), min(HEIGHT, y + 2)):
                    for xx in range(max(0, x - 1), min(WIDTH, x + 2)):
                        if mask[yy][xx]:
                            coast = 1
                            break
                    if coast:
                        break
                shallows = 18 * coast
                r = 8 + 10 * equatorial + 8 * noise + shallows
                g = 30 + 34 * equatorial + 10 * noise + shallows
                b = 82 + 68 * equatorial + 16 * noise + shallows

            data[idx] = max(0, min(255, int(r)))
            data[idx + 1] = max(0, min(255, int(g)))
            data[idx + 2] = max(0, min(255, int(b)))
    return data


def write_ppm(path: Path, data: bytearray):
    with path.open('wb') as fh:
        fh.write(f'P6\n{WIDTH} {HEIGHT}\n255\n'.encode('ascii'))
        fh.write(data)


def main():
    if len(sys.argv) != 3:
        print('usage: generate_earth_texture.py <world_data.h> <output.ppm>', file=sys.stderr)
        return 1
    header_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    vertices, rings, polygons = parse_world_header(header_path)
    mask = rasterize_land(vertices, rings, polygons)
    data = build_texture(mask)
    write_ppm(output_path, data)
    print(f'generated {output_path} ({WIDTH}x{HEIGHT})')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
