import os
import sys

PATCHES = [
    ('js/JStheme_common.js',
     '        case "fy+h":\t\t\n        case "http":',
     '        case "navi":\n        taggable = 1;\n        type = 0;\n        break;\n        case "fy+h":\t\t\n        case "http":',
     'navidrome:// tracks can be rated like local files'),
    ('js/biography/scripts/lastfm.js',
     '\t\tthis.imgExisting = p_imgExisting;\n\t\tthis.func = null;\n',
     '\t\tthis.imgExisting = p_imgExisting;\n'
     '\t\tif (typeof navidrome !== \'undefined\' && navidrome.IsConfigured) navidrome.GetArtistInfoAsync(this.dl_ar).then(i => {\n'
     '\t\t\tif (!i.ImageUrl || i.ImageUrl.includes(\'2a96cbd8b46e442fc41c2b86b821562f\')) return;\n'
     '\t\t\tconst pth = this.img_folder + $.clean(this.dl_ar) + \'_navidrome.jpg\';\n'
     '\t\t\tif ($.file(pth)) return;\n'
     '\t\t\t$.buildPth(this.img_folder);\n'
     '\t\t\t$.save(this.img_folder + \'update.txt\', \'\', true);\n'
     '\t\t\ttimer.decelerating();\n'
     '\t\t\t$.run(`cscript //nologo "${cfg.storageFolder}foo_lastfm_img.vbs" "${i.ImageUrl}" "${pth}"`, 0);\n'
     '\t\t}, () => {});\n'
     '\t\tthis.func = null;\n',
     'artist photos come from Navidrome (last.fm\'s photo pages need a browser now)'),
    ('js/WSHlibrary_tree.js',
     '                if (!this.sel_items.length) return;\n'
     '                this.load(this.sel_items, true, false, false, this.gen_pl, false);\n'
     '                break;\n',
     '                if (!this.sel_items.length) return;\n'
     '                this.load(this.sel_items, true, false, false, this.gen_pl, false);\n'
     '                plman.ActivePlaylist = this.pln;\n'
     '                plman.ExecutePlaylistDefaultAction(this.pln, (plman.PlaybackOrder == 3 || plman.PlaybackOrder == 4) ? Math.ceil(plman.PlaylistItemCount(this.pln) * Math.random() - 1) : 0);\n'
     '                break;\n',
     'Enter in the library tree replaces the playlist with the selection and plays it'),
]


def main():
    if len(sys.argv) != 2:
        print('usage: eole_patches.py THEMES/EOLE_DIR', file=sys.stderr)
        return 2
    root = sys.argv[1]
    for rel, old, new, why in PATCHES:
        path = os.path.join(root, rel)
        with open(path, encoding='utf-8-sig') as f:
            text = f.read()
        if new in text:
            print('already applied: %s (%s)' % (rel, why))
            continue
        if text.count(old) != 1:
            print('NOT FOUND: %s (%s)' % (rel, why), file=sys.stderr)
            return 1
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text.replace(old, new))
        print('applied: %s (%s)' % (rel, why))
    return 0


if __name__ == '__main__':
    sys.exit(main())
