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
    ('Tracks', 'WSHsmoothplaylist_trackinfos.js', 0),
    ('Playlist', 'WSHsmoothplaylist_trackinfos.js', 1),
    ('Playlists', 'WSHsmoothplaylistmanager.js', 0),
    ('Biography', 'SMPbiography.js', 0),
    ('NowPlaying', 'WSHnowplaying.js', 0),
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

TOP, BOTTOM, SIDE, RIGHT = 70, 70, 210, 320


def when(layout, view, panel):
    inner = panel if view is None else '$ifequal($add(0,$getpvar(eole.mainpanel)),%d,%s,)' % (view, panel)
    return '$ifequal($add(0,$getpvar(eole.layout)),%d,%s,)' % (layout, inner)


def tree(on, panel):
    return '$ifequal($add(0,$getpvar(eole.librarytree)),1,%s,%s)' % ((panel, '') if on else ('', panel))


def place(name, x, y, w, h):
    return '$panel(%s,JS Panel,%s,%s,%s,%s)' % (name, x, y, w, h)


def layout_lines():
    mid_h = '$eval(%%_height%% - %d)' % (TOP + BOTTOM)
    lines = [
        '$ifequal($add(0,$getpvar(hidetitlebar)),1,$windowstyle(hidetitlebar),$windowstyle(showtitlebar))',
        '$drawrect(0,0,%_width%,%_height%,brushcolor-24-25-30 pencolor-null)',
        place('TitleBar', 0, 0, '%_width%', TOP),
        place('Controls', 0, '$eval(%%_height%% - %d)' % BOTTOM, '%_width%', BOTTOM),
        when(MINI, None, place('Cover', 0, TOP, '%_width%', mid_h)),
        when(FULL, LIBRARY, tree(False, place('Filter', 0, TOP, SIDE, mid_h))),
        when(FULL, LIBRARY, tree(True, place('LibraryTree', 0, TOP, SIDE, mid_h))),
        when(FULL, LIBRARY, place('Browser', SIDE, TOP, '$eval(%%_width%% - %d)' % (SIDE + RIGHT), mid_h)),
        when(FULL, LIBRARY, place('Cover', '$eval(%%_width%% - %d)' % RIGHT, TOP, RIGHT, 330)),
        when(FULL, LIBRARY, place('Tracks', '$eval(%%_width%% - %d)' % RIGHT, TOP + 330, RIGHT,
                                  '$eval(%%_height%% - %d)' % (TOP + BOTTOM + 330))),
        when(FULL, PLAYLISTS, place('Playlists', 0, TOP, SIDE, mid_h)),
        when(FULL, PLAYLISTS, place('Playlist', SIDE, TOP, '$eval(%%_width%% - %d)' % (SIDE + RIGHT), mid_h)),
        when(FULL, PLAYLISTS, place('Cover', '$eval(%%_width%% - %d)' % RIGHT, TOP, RIGHT, mid_h)),
        when(FULL, NOW_PLAYING, place('Cover', 0, TOP, '$eval(%_width% / 2)', mid_h)),
        when(FULL, NOW_PLAYING, place('Biography', '$eval(%_width% / 2)', TOP, '$eval(%_width% - %_width% / 2)', mid_h)),
        when(FULL, VISUALIZATION, place('NowPlaying', 0, TOP, '%_width%', mid_h)),
    ]
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
