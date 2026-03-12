#!/usr/bin/env python3
import json
import sys
from pathlib import Path


def load_geojson(path: Path):
    text = path.read_text(encoding='utf-8')
    start = text.find('{')
    if start < 0:
        raise SystemExit(f'No JSON object found in {path}')
    return json.loads(text[start:])


def iter_polygons(geometry):
    gtype = geometry['type']
    coords = geometry['coordinates']
    if gtype == 'Polygon':
        yield coords
    elif gtype == 'MultiPolygon':
        for polygon in coords:
            yield polygon
    else:
        return


def normalize_ring(ring):
    points = [(round(point[0], 4), round(point[1], 4), 0.0) for point in ring]
    if len(points) > 1 and points[0][:2] == points[-1][:2]:
        points = points[:-1]
    if len(points) < 3:
        return None
    return points


def main() -> int:
    if len(sys.argv) != 3:
        print('usage: generate_world_data.py <input-geojson> <output-header>', file=sys.stderr)
        return 1

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    data = load_geojson(input_path)

    vertices = []
    rings = []
    polygons = []

    for feature in data.get('features', []):
        geometry = feature.get('geometry')
        if not geometry:
            continue

        for polygon in iter_polygons(geometry):
            ring_start = len(rings)
            ring_count = 0

            for ring in polygon:
                normalized = normalize_ring(ring)
                if not normalized:
                    continue
                vertex_start = len(vertices)
                vertices.extend(normalized)
                rings.append((vertex_start, len(normalized)))
                ring_count += 1

            if ring_count:
                polygons.append((ring_start, ring_count))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open('w', encoding='utf-8') as out:
        out.write('/* Generated from Natural Earth ne_110m_land.geojson (public domain). */\n')
        out.write('#ifndef WORLD_DATA_H\n#define WORLD_DATA_H\n\n')
        out.write('#include <stddef.h>\n\n')
        out.write('typedef struct {\n    double lon;\n    double lat;\n    double z;\n} WorldVertex;\n\n')
        out.write('typedef struct {\n    int vertex_start;\n    int vertex_count;\n} WorldRing;\n\n')
        out.write('typedef struct {\n    int ring_start;\n    int ring_count;\n} WorldPolygon;\n\n')

        out.write(f'static const size_t WORLD_VERTEX_COUNT = {len(vertices)};\n')
        out.write(f'static const size_t WORLD_RING_COUNT = {len(rings)};\n')
        out.write(f'static const size_t WORLD_POLYGON_COUNT = {len(polygons)};\n\n')

        out.write('static const WorldVertex WORLD_VERTICES[] = {\n')
        for lon, lat, z in vertices:
            out.write(f'    {{{lon:.4f}, {lat:.4f}, {z:.1f}}},\n')
        out.write('};\n\n')

        out.write('static const WorldRing WORLD_RINGS[] = {\n')
        for vertex_start, vertex_count in rings:
            out.write(f'    {{{vertex_start}, {vertex_count}}},\n')
        out.write('};\n\n')

        out.write('static const WorldPolygon WORLD_POLYGONS[] = {\n')
        for ring_start, ring_count in polygons:
            out.write(f'    {{{ring_start}, {ring_count}}},\n')
        out.write('};\n\n#endif\n')

    print(f'generated {output_path} with {len(polygons)} polygons, {len(rings)} rings, {len(vertices)} vertices')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
