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


WSH_SIZES = {16: '<b', 17: '<B', 2: '<h', 18: '<H', 11: '<h', 3: '<i', 22: '<i', 19: '<I', 23: '<I', 4: '<f',
             20: '<q', 21: '<Q', 6: '<q', 5: '<d', 7: '<d'}


def parse_wsh_panel(data, pos):
    r = Reader(data, pos)
    version = r.u32()
    if not 0x73 <= version <= 0x7b:
        raise ValueError('version')
    if version >= 0x7a:
        r.take(1)
    guid = r.take(16) if version >= 0x79 else b''
    edge = r.u32() if version >= 0x78 else 0
    props = {}
    if version >= 0x77:
        count = r.u32()
        if count > 100000:
            raise ValueError('count')
        for _ in range(count):
            name = r.string(4096)
            vt = struct.unpack('<H', r.take(2))[0]
            if vt == 8:
                props[name] = r.string()
            elif vt in WSH_SIZES:
                fmt = WSH_SIZES[vt]
                value = struct.unpack(fmt, r.take(struct.calcsize(fmt)))[0]
                props[name] = value != 0 if vt == 11 else value / 10000 if vt == 6 else value
    r.take((1 if version >= 0x76 else 0) + (1 if version >= 0x75 else 0) + (WINDOWPLACEMENT if version >= 0x74 else 0))
    engine = r.string(64).lower()
    if engine and 'jscript' not in engine and 'javascript' not in engine:
        raise ValueError('engine')
    script = r.string()
    transparent = r.take(1)[0] != 0
    return {'offset': pos, 'end': r.p, 'id': guid.hex(), 'edge': edge, 'transparent': transparent, 'properties': props,
            'script': script, 'package': ''}


def wsh_panels(data):
    panels = []
    start = 0
    while True:
        i = data.find(b'\x00\x00\x00', start)
        if i < 1:
            break
        start = i + 1
        try:
            p = parse_wsh_panel(data, i - 1)
        except (ValueError, UnicodeDecodeError, struct.error):
            continue
        if len(p['script'].strip()) < 32:
            continue
        panels.append(p)
        start = p['end']
    return panels


def json_panels(data):
    panels = []
    start = 0
    while True:
        i = data.find(b'"id": "settings"', start)
        if i < 0:
            break
        start = i + 1
        for b in range(i - 1, max(i - 512, 4), -1):
            if data[b:b + 1] != b'{':
                continue
            n = struct.unpack('<I', data[b - 4:b])[0]
            try:
                cfg = json.loads(data[b:b + n].decode('utf-8'))
            except (ValueError, UnicodeDecodeError):
                continue
            if cfg.get('id') != 'settings':
                continue
            kind = cfg.get('scriptType')
            payload = cfg.get('payload', {})
            panel = {'offset': b, 'end': b + n, 'id': cfg.get('panelId', ''), 'edge': cfg.get('edgeStyle', 0),
                     'transparent': cfg.get('isPseudoTransparent', False),
                     'properties': cfg.get('properties', {}).get('values', {}), 'script': '', 'package': ''}
            if kind == 1:
                panel['script'] = payload.get('script', '')
            elif kind == 3:
                path = payload.get('path', '').replace('\\', '\\\\')
                base = {1: 'fb.ComponentPath + ', 2: 'fb.ProfilePath + ', 3: 'fb.FoobarPath + '}.get(payload.get('locationType'), '')
                panel['script'] = "include(%s'%s');\n" % (base, path)
            elif kind in (4, 5):
                panel['package'] = payload.get('id', '')
            else:
                break
            panels.append(panel)
            start = b + n
            break
    return panels


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
        p['package'] = ''
        panels.append(p)
        start = p['end']
    return panels + json_panels(data) + wsh_panels(data)


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
        if p['package']:
            with open(os.path.join(out, name + '.package'), 'w', encoding='utf-8') as f:
                f.write(p['package'] + '\n')
        else:
            with open(os.path.join(out, name + '.js'), 'w', encoding='utf-8') as f:
                f.write(p['script'])
        with open(os.path.join(out, name + '.properties.json'), 'w', encoding='utf-8') as f:
            json.dump(p['properties'], f, ensure_ascii=False, indent=1)
        includes = [l.strip() for l in p['script'].splitlines() if 'include(' in l and not l.strip().startswith('//')]
        index.append({'name': name, 'offset': p['offset'], 'id': p['id'], 'properties': len(p['properties']),
                      'script_bytes': len(p['script']), 'includes': includes, 'package': p['package']})
    with open(os.path.join(out, 'index.json'), 'w', encoding='utf-8') as f:
        json.dump(index, f, ensure_ascii=False, indent=1)
    for e in index:
        print('%s  @%-7d props=%-4d script=%-6d %s' % (e['name'], e['offset'], e['properties'], e['script_bytes'],
                                                      e['package'] or ' '.join(i.split("js\\\\")[-1].rstrip("');") for i in e['includes'])))
    print('%d SMP and WSH panels' % len(index))
    return 0


if __name__ == '__main__':
    sys.exit(main())
