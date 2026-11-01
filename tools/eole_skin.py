import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fcl_extract import find_panels

ROLES = [
    ('TitleBar', 'WSHtitle_bar.js', 0),
    ('Controls', 'WSHcontrols.js', 0),
    ('Cover', 'WSHcoverpanel.js', 0),
    ('Filter', 'WSHfilter.js', 0),
    ('LibraryTree', 'WSHlibrary_tree.js', 0),
    ('Browser', 'WSHgraphicbrowser_trackinfos.js', 0),
    ('Playlists', 'WSHsmoothplaylistmanager.js', 0),
    ('PlaylistFilter1', 'WSHfilter.js', 1),
    ('PlaylistFilter2', 'WSHfilter.js', 2),
    ('PlaylistFilter3', 'WSHfilter.js', 3),
    ('PlaylistHeader', 'WSHplaylistheader.js', 0),
    ('BottomPlaylist', 'WSHsmoothplaylist_trackinfos.js', 0),
    ('Biography', 'SMPbiography.js', 0),
    ('TrackInfos', 'WSHnowplaying.js', 0),
    ('NowPlayingPlaylist', 'WSHsmoothplaylist_trackinfos.js', 1),
]

ADAPTER = r'''
(function () {
    if (typeof panelsui === 'undefined' || !panelsui.IsAvailable) return;
    const mirror = s => panelsui.SetVariable('eole.' + s.file_prefix.replace(/_$/, '').toLowerCase(), String(s.value));
    for (const k of Object.keys(globalThis)) {
        const s = globalThis[k];
        if (!s || typeof s !== 'object' || typeof s.setValue !== 'function' || typeof s.file_prefix !== 'string') continue;
        const set = s.setValue;
        s.setValue = function (...args) {
            const r = set.apply(this, args);
            mirror(this);
            return r;
        };
        mirror(s);
    }
    globalThis.RefreshPSS = function () {};
})();
'''

ARTIST_IMAGES = r'''
(function () {
    if (typeof navidrome === 'undefined' || !navidrome.IsConfigured || typeof brw === 'undefined') return;
    const files = new ActiveXObject('Scripting.FileSystemObject');
    const shell = new ActiveXObject('WScript.Shell');
    const vbs = fb.ProfilePath + 'yttm\\foo_lastfm_img.vbs';
    const tried = new Set();
    let busy = false;
    const folderOf = name => fb.ProfilePath + 'yttm\\biography-cache\\art_img\\' + name.toLowerCase().charAt(0) + '\\' + name;
    const hasImage = dir => files.FolderExists(dir) && utils.Glob(dir + '\\*').some(f => /\.(jpe?g|png|gif|bmp)$/i.test(f));
    const refresh = name => {
        brw.groups.forEach((g, i) => {
            if (!g.metadb || String(g.groupkey).sanitise() !== name) return;
            delete_file_cache(g.metadb, i);
            g.tid = -1; g.load_requested = 0; g.cover_formated = false; g.save_requested = false;
            g.cover_img_mask = null; g.cover_img = null; g.cover_type = null;
            g_image_cache.reset(g.cachekey);
        });
        brw.repaint();
    };
    const wait = (file, name, left) => {
        if (files.FileExists(file) || left <= 0) {
            if (files.FileExists(file)) refresh(name);
            busy = false;
            return;
        }
        setTimeout(() => wait(file, name, left - 1), 300);
    };
    const next = () => {
        if (busy || properties.tagMode != 2 || !files.FileExists(vbs)) return;
        const g = brw.groups.find(g => g.metadb && g.groupkey && !tried.has(g.groupkey));
        if (!g) return;
        const artist = String(g.groupkey), name = artist.sanitise();
        tried.add(artist);
        const dir = folderOf(name);
        if (!name || hasImage(dir)) return;
        busy = true;
        navidrome.GetArtistInfoAsync(artist).then(info => {
            if (!info.ImageUrl || info.ImageUrl.includes('2a96cbd8b46e442fc41c2b86b821562f')) { busy = false; return; }
            const file = dir + '\\' + name + '_navidrome.jpg';
            shell.Run('cscript //nologo "' + vbs + '" "' + info.ImageUrl + '" "' + file + '"', 0);
            wait(file, name, 40);
        }, () => { busy = false; });
    };
    setInterval(next, 500);
})();
'''

FULL, MINI = 0, 1
LIBRARY, PLAYLISTS, NOW_PLAYING, VISUALIZATION = 0, 1, 2, 3
HEADER = 40


def var(name):
    return '$add(0,$getpvar(eole.%s))' % name


def add(a, b):
    return '$add(%s,%s)' % (a, b)


def sub(a, b):
    return '$sub(%s,%s)' % (a, b)


def per_view(prefix):
    return '$select($add(%s,1),%s)' % (var('mainpanel'), ','.join(var(prefix + v) for v in ('lib', 'playlist', 'bio', 'visu')))


def when(cond, value, text):
    return '$ifequal(%s,%s,%s,)' % (cond, value, text)


def unless(cond, value, text):
    return '$ifequal(%s,%s,,%s)' % (cond, value, text)


def view(layout, v, text):
    inner = text if v is None else when(var('mainpanel'), v, text)
    return when(var('layout'), layout, inner)


def place(name, x, y, w, h, kind='JS Panel'):
    return '$panel(%s,%s,%s,%s,%s,%s)' % (name, kind, x, y, w, h)


def get(name):
    return '$get(%s)' % name


def layout_lines():
    W, H = '%_width%', '%_height%'
    defs = [
        ('top', '$ifequal(%s,1,64,$select(%s,64,36,50))' % (var('layout'), add(var('compacttitlebar'), 1))),
        ('ctl', '$ifequal(%s,1,$ifequal(%s,0,43,51),$ifequal(%s,0,72,$ifequal(%s,0,47,54)))' % (
            var('layout'), var('showtrackinfosmall'), var('minicontrolbar'), var('showtrackinfobig'))),
        ('mid', sub(sub(H, get('top')), get('ctl'))),
        ('bar_y', sub(H, get('ctl'))),
        ('rpw', var('rightplaylistwidth')),
        ('np', per_view('nowplaying')),
        ('ti', per_view('trackinfos')),
        ('main_w', '$ifequal(%s,0,%s,%s)' % (get('np'), W, sub(W, get('rpw')))),
        ('right_x', sub(W, get('rpw'))),
        ('ti_h', '$min(%s,$min($div(%s,2),$ifequal(%s,0,%s,%s)))' % (get('mid'), H, var('trackinfostext'), get('rpw'), add(get('rpw'), 41))),
        ('small_cover', '$ifequal(%s,0,%s,%s)' % (var('layout'), var('coverpanelbig'), var('coverpanelmini'))),
        ('dark', '$ifequal(%s,1,%s,$select($add(%s,1),%s,%s,$ifequal(%s,1,1,%s),%s))' % (
            var('layout'), var('darkmini'), var('mainpanel'), var('darklib'), var('darkplaylist'),
            var('darkbiostick'), var('darkbio'), var('darkvisu'))),
        ('lfw', var('libraryfilterwidth')),
        ('ppw', var('playlistpanelwidth')),
        ('pl_w', sub(get('main_w'), get('ppw'))),
        ('fp', var('filterspanel')),
        ('f_h', '$select(%s,0,$mul($div(%s,10),2),$mul($div(%s,10),3),$mul($div(%s,5),2),$mul($div(%s,5),3),%s)' % (
            add(get('fp'), 1), get('mid'), get('mid'), get('mid'), get('mid'), get('mid'))),
        ('filters', add(add('$mul(%s,4)' % var('filter1'), '$mul(%s,2)' % var('filter2')), var('filter3'))),
        ('half', '$div(%s,2)' % get('pl_w')),
        ('third', '$div(%s,3)' % get('pl_w')),
        ('ly', var('lyrics')),
        ('ly_w', '$muldiv(%s,$ifequal(%s,10,1,$mul(%s,10)),100)' % (get('main_w'), get('ly'), get('ly'))),
    ]
    top, ctl, mid, bar_y, rpw = get('top'), get('ctl'), get('mid'), get('bar_y'), get('rpw')
    main_w, right_x, ti_h, ti = get('main_w'), get('right_x'), get('ti_h'), get('ti')
    lines = ['$puts(%s,%s)' % d for d in defs] + [
        '$ifequal($add(0,$getpvar(hidetitlebar)),1,$windowstyle(hidetitlebar),$windowstyle(showtitlebar))',
        '$ifequal(%s,1,$drawrect(0,0,%s,%s,brushcolor-30-30-30 pencolor-null),$drawrect(0,0,%s,%s,brushcolor-255-255-255 pencolor-null))' % (
            get('dark'), W, H, W, H),
        place('TitleBar', 0, 0, W, top),
        when(get('small_cover'), 1, place('Cover', 0, bar_y, ctl, ctl)),
        '$ifequal(%s,1,%s,%s)' % (get('small_cover'), place('Controls', ctl, bar_y, sub(W, ctl), ctl), place('Controls', 0, bar_y, W, ctl)),
        view(MINI, None, place('NowPlayingPlaylist', 0, top, W, sub(mid, 1))),
    ]
    right = [
        when(ti, 0, place('NowPlayingPlaylist', right_x, top, rpw, mid)),
        when(ti, 1, place('TrackInfos', right_x, top, rpw, ti_h) + place('NowPlayingPlaylist', right_x, add(top, ti_h), rpw, sub(mid, ti_h))),
        when(ti, 2, place('NowPlayingPlaylist', right_x, top, rpw, sub(mid, ti_h)) + place('TrackInfos', right_x, sub(bar_y, ti_h), rpw, ti_h)),
    ]
    lines += [view(FULL, None, unless(get('np'), 0, r)) for r in right]
    lfw = get('lfw')
    side = '$ifequal(%s,1,%s,%s)' % (var('librarytree'), place('LibraryTree', 0, top, lfw, mid), place('Filter', 0, top, lfw, mid))
    lines.append(view(FULL, LIBRARY, '$ifequal(%s,0,%s,%s)' % (
        var('libraryfilter'), place('Browser', 0, top, main_w, mid), side + place('Browser', lfw, top, sub(main_w, lfw), mid))))
    ppw, pl_w, f_h, half, third = get('ppw'), get('pl_w'), get('f_h'), get('half'), get('third')
    lines.append(view(FULL, PLAYLISTS, place('Playlists', 0, top, ppw, mid)))
    splits = {
        0: [(3, '0', pl_w)], 1: [(3, '0', pl_w)], 2: [(2, '0', pl_w)],
        3: [(2, '0', half), (3, half, sub(pl_w, half))],
        4: [(1, '0', pl_w)],
        5: [(1, '0', half), (3, half, sub(pl_w, half))],
        6: [(1, '0', half), (2, half, sub(pl_w, half))],
        7: [(1, '0', third), (2, third, third), (3, '$mul(%s,2)' % third, sub(pl_w, '$mul(%s,2)' % third))],
    }
    for k, parts in splits.items():
        panels = ''.join(place('PlaylistFilter%d' % n, add(ppw, x), top, w, f_h) for n, x, w in parts)
        lines.append(view(FULL, PLAYLISTS, unless(get('fp'), 0, when(get('filters'), k, panels))))
    lines.append(view(FULL, PLAYLISTS, unless(get('fp'), 5, place('PlaylistHeader', ppw, add(top, f_h), pl_w, HEADER) +
                                               place('BottomPlaylist', ppw, add(add(top, f_h), HEADER), pl_w, sub(sub(mid, f_h), HEADER)))))
    ly, ly_w = get('ly'), get('ly_w')
    split = place('Biography', 0, top, sub(main_w, ly_w), mid) + place('Lyrics', sub(main_w, ly_w), top, ly_w, mid, 'Lyric Show')
    lines.append(view(FULL, NOW_PLAYING, '$ifequal(%s,0,%s,$ifequal(%s,11,%s,%s))' % (
        ly, place('Biography', 0, top, main_w, mid), ly, place('Lyrics', 0, top, main_w, mid, 'Lyric Show'), split)))
    lines.append(view(FULL, VISUALIZATION, place('Spectrum', 0, top, main_w, mid, 'Spectrum')))
    return lines


def main():
    if len(sys.argv) != 3:
        print('usage: eole_skin.py LAYOUT.fcl SKIN_DIR', file=sys.stderr)
        return 2
    panels = find_panels(open(sys.argv[1], 'rb').read())
    skin = sys.argv[2]
    os.makedirs(os.path.join(skin, 'js'), exist_ok=True)
    ini = ['script = main.txt', 'pvar.once.hidetitlebar = 1', 'command.Toggle title bar = PVAR:TOGGLE:hidetitlebar']
    for name, script_name, nth in ROLES:
        matches = [p for p in panels if any(script_name in line for line in p['script'].splitlines()
                                            if 'include(' in line and not line.strip().startswith('//'))]
        if len(matches) <= nth:
            print('missing panel for %s (%s)' % (name, script_name), file=sys.stderr)
            return 1
        p = matches[nth]
        seed = '(function () {\n    const seed = %s;\n    for (const k in seed) window.GetProperty(k, seed[k]);\n})();\n' % json.dumps(
            p['properties'], ensure_ascii=False)
        with open(os.path.join(skin, 'js', name + '.js'), 'w', encoding='utf-8') as f:
            f.write(seed + p['script'] + '\n' + ADAPTER + (ARTIST_IMAGES if name == 'Filter' else ''))
        ini.append('panel.%s.script = js/%s.js' % (name, name))
        print('%-11s %s' % (name, script_name))
    with open(os.path.join(skin, 'foo_ui_panels.ini'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(ini) + '\n')
    with open(os.path.join(skin, 'main.txt'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(layout_lines()) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
