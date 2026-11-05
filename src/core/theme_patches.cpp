#include "theme_patches.h"
#include <algorithm>
#include <cctype>

namespace jsp {

namespace {

struct Patch {
    const char* file;
    const char* from;
    const char* to;
};

const Patch kEole[] = {
    { "js/JStheme_common.js", R"PATCH(        case "fy+h":		
        case "http":)PATCH", R"PATCH(        case "navi":
        taggable = 1;
        type = 0;
        break;
        case "fy+h":		
        case "http":)PATCH" },
    { "js/biography/scripts/lastfm.js", R"PATCH(		this.imgExisting = p_imgExisting;
		this.func = null;
)PATCH", R"PATCH(		this.imgExisting = p_imgExisting;
		if (typeof navidrome !== 'undefined' && navidrome.IsConfigured) navidrome.GetArtistInfoAsync(this.dl_ar).then(i => {
			if (!i.ImageUrl || i.ImageUrl.includes('2a96cbd8b46e442fc41c2b86b821562f')) return;
			const pth = this.img_folder + $.clean(this.dl_ar) + '_navidrome.jpg';
			if ($.file(pth)) return;
			$.buildPth(this.img_folder);
			$.save(this.img_folder + 'update.txt', '', true);
			timer.decelerating();
			$.run(`cscript //nologo "${cfg.storageFolder}foo_lastfm_img.vbs" "${i.ImageUrl}" "${pth}"`, 0);
		}, () => {});
		this.func = null;
)PATCH" },
    { "js/WSHlibrary_tree.js", R"PATCH(                if (!this.sel_items.length) return;
                this.load(this.sel_items, true, false, false, this.gen_pl, false);
                break;
)PATCH", R"PATCH(                if (!this.sel_items.length) return;
                this.load(this.sel_items, true, false, false, this.gen_pl, false);
                plman.ActivePlaylist = this.pln;
                plman.ExecutePlaylistDefaultAction(this.pln, (plman.PlaybackOrder == 3 || plman.PlaybackOrder == 4) ? Math.ceil(plman.PlaylistItemCount(this.pln) * Math.random() - 1) : 0);
                break;
)PATCH" },
    { "js/WSHfilter.js", "", R"PATCH(
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
)PATCH" },
};

bool ends_with(const std::string& path, const std::string& tail) {
    if (path.size() < tail.size()) return false;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        const char a = path[path.size() - tail.size() + i], b = tail[i];
        if ((a == '\\' ? '/' : (char)std::tolower((unsigned char)a)) != (char)std::tolower((unsigned char)b)) return false;
    }
    return true;
}

bool in_eole(const std::string& path) {
    std::string low(path);
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c == '\\' ? '/' : c); });
    return low.find("/eole/") != std::string::npos;
}

}

std::string patch_theme_source(const std::string& path, std::string text, std::vector<std::string>* applied) {
    if (!in_eole(path)) return text;
    for (const Patch& p : kEole) {
        if (!ends_with(path, std::string("/") + p.file)) continue;
        const std::string from = p.from, to = p.to;
        if (text.find(to) != std::string::npos) continue;
        if (from.empty()) {
            text += "\n" + to;
        } else {
            const std::size_t at = text.find(from);
            if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) continue;
            text.replace(at, from.size(), to);
        }
        if (applied) applied->push_back(p.file);
    }
    return text;
}

}
