import json
import os
import struct
import sys

SMP_GUID = bytes.fromhex('251527326d43fa4a8dfd437ab0c83118')
WINDOWPLACEMENT = 44


class Reader:
    def __init__(self, data, pos):
        self.d = data
        self.p = pos

    def take(self, n):
        if self.p + n > len(self.d):
            raise ValueError('eof')
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u32(self):
        return struct.unpack('<I', self.take(4))[0]

    def string(self, limit=4 * 1024 * 1024):
        n = self.u32()
        if n > limit:
            raise ValueError('string too long')
        return self.take(n).decode('utf-8')


def parse_panel(data, pos):
    r = Reader(data, pos)
    if r.u32() != 1:
        raise ValueError('version')
    r.take(1)
    guid = r.take(16)
    edge = r.take(1)[0]
    count = r.u32()
    if count > 100000:
        raise ValueError('count')
    props = {}
    for _ in range(count):
        name = r.string(4096)
        kind = r.u32()
        if kind == 0:
            props[name] = r.take(1)[0] != 0
        elif kind == 1:
            props[name] = struct.unpack('<i', r.take(4))[0]
        elif kind == 2:
            props[name] = struct.unpack('<d', r.take(8))[0]
        elif kind == 3:
            props[name] = r.string()
        else:
            raise ValueError('kind')
    r.take(2 + WINDOWPLACEMENT)
    script = r.string()
    transparent = r.take(1)[0] != 0
    return {'offset': pos, 'end': r.p, 'id': guid.hex(), 'edge': edge, 'transparent': transparent, 'properties': props, 'script': script}


def find_panels(data):
    panels = []
    pos = data.find(SMP_GUID)
    first_dep = pos
    start = 0
    while True:
        i = data.find(b'\x01\x00\x00\x00', start)
        if i < 0:
            break
        start = i + 1
        if i <= first_dep:
            continue
        try:
            p = parse_panel(data, i)
        except (ValueError, UnicodeDecodeError, struct.error):
            continue
        if len(p['script'].strip()) < 32:
            continue
        panels.append(p)
        start = p['end']
    return panels


def main():
    if len(sys.argv) < 3:
        print('usage: fcl_extract.py LAYOUT.fcl OUT_DIR', file=sys.stderr)
        return 2
    data = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    panels = find_panels(data)
    index = []
    for n, p in enumerate(panels, 1):
        name = 'panel-%02d' % n
        with open(os.path.join(out, name + '.js'), 'w', encoding='utf-8') as f:
            f.write(p['script'])
        with open(os.path.join(out, name + '.properties.json'), 'w', encoding='utf-8') as f:
            json.dump(p['properties'], f, ensure_ascii=False, indent=1)
        includes = [l.strip() for l in p['script'].splitlines() if 'include(' in l and not l.strip().startswith('//')]
        index.append({'name': name, 'offset': p['offset'], 'id': p['id'], 'properties': len(p['properties']),
                      'script_bytes': len(p['script']), 'includes': includes})
    with open(os.path.join(out, 'index.json'), 'w', encoding='utf-8') as f:
        json.dump(index, f, ensure_ascii=False, indent=1)
    for e in index:
        print('%s  @%-7d props=%-4d script=%-6d %s' % (e['name'], e['offset'], e['properties'], e['script_bytes'],
                                                      ' '.join(i.split("js\\\\")[-1].rstrip("');") for i in e['includes'])))
    print('%d SMP panels' % len(index))
    return 0


if __name__ == '__main__':
    sys.exit(main())
