#include "sample_script.h"

namespace jsp {

const char* sample_script() {
    return R"JS(
function RGB(r, g, b) { return 0xff000000 | (r << 16) | (g << 8) | b; }
function RGBA(r, g, b, a) { return ((a << 24) | (r << 16) | (g << 8) | b) >>> 0; }

const DT_CENTER = 0x1, DT_VCENTER = 0x4, DT_SINGLELINE = 0x20, DT_END_ELLIPSIS = 0x8000;
const fontTitle = gdi.Font("Segoe UI", 20, 1);
const fontInfo = gdi.Font("Segoe UI", 14, 0);
const fontHint = gdi.Font("Segoe UI", 12, 2);
const tfTitle = fb.TitleFormat("%title%");
const tfInfo = fb.TitleFormat("[%artist%][ - %album%]");
let hover = false;
let ticker = 0;

function format_time(s) {
    s = Math.max(0, Math.floor(s));
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
}

function on_paint(gr) {
    const w = window.Width, h = window.Height;
    gr.FillGradRect(0, 0, w, h, 90, RGB(40, 44, 52), RGB(22, 24, 28));
    const flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    if (fb.IsPlaying) {
        gr.GdiDrawText(tfTitle.Eval(), fontTitle, RGB(240, 240, 240), 10, h / 2 - 48, w - 20, 30, flags);
        gr.GdiDrawText(tfInfo.Eval(), fontInfo, RGB(170, 180, 190), 10, h / 2 - 16, w - 20, 22, flags);
        const len = fb.PlaybackLength, pos = fb.PlaybackTime;
        const bw = Math.max(0, w - 40);
        gr.FillRoundRect(20, h / 2 + 16, bw, 6, 3, 3, RGB(60, 64, 72));
        if (len > 0) gr.FillRoundRect(20, h / 2 + 16, Math.round(bw * pos / len), 6, 3, 3, RGB(90, 160, 255));
        gr.GdiDrawText(format_time(pos) + " / " + format_time(len), fontInfo, RGB(150, 155, 165),
                       10, h / 2 + 28, w - 20, 22, flags);
    } else {
        gr.GdiDrawText("JS Panel", fontTitle, RGB(240, 240, 240), 10, h / 2 - 40, w - 20, 30, flags);
        gr.GdiDrawText("Nothing playing. Click to play.", fontInfo, RGB(170, 180, 190), 10, h / 2 - 6, w - 20, 22, flags);
    }
    if (hover) gr.DrawRect(1, 1, w - 3, h - 3, 1, RGBA(90, 160, 255, 255));
    gr.GdiDrawText("Right-click: choose a script file", fontHint, RGB(110, 115, 125), 10, h - 26, w - 20, 20, flags);
}

function on_mouse_move() { if (!hover) { hover = true; window.Repaint(); } }
function on_mouse_leave() { hover = false; window.Repaint(); }
function on_mouse_lbtn_up() { fb.PlayOrPause(); }
function on_playback_new_track() { window.Repaint(); }
function on_playback_stop() { window.Repaint(); }
function on_playback_pause() { window.Repaint(); }
function on_playback_time() { window.Repaint(); }
)JS";
}


namespace {

const char* const kAlbumArt = R"JS(
window.DefineScript('Album art', { author: 'JS Panel' });
const font = gdi.Font('Segoe UI', 14, 0);
let art = null, loaded = false;

function load() {
    art = null;
    loaded = false;
    const h = fb.GetNowPlaying() || fb.GetFocusItem();
    if (h) utils.GetAlbumArtAsync(window.ID, h, 0);
    else loaded = true;
    window.Repaint();
}

function on_get_album_art_done(handle, id, image) {
    art = image;
    loaded = true;
    window.Repaint();
}

function on_paint(gr) {
    const w = window.Width, h = window.Height;
    gr.FillSolidRect(0, 0, w, h, window.GetColourDUI(1));
    if (art) {
        const s = Math.min(w / art.Width, h / art.Height);
        const iw = Math.round(art.Width * s), ih = Math.round(art.Height * s);
        gr.DrawImage(art, Math.round((w - iw) / 2), Math.round((h - ih) / 2), iw, ih, 0, 0, art.Width, art.Height);
    } else {
        gr.GdiDrawText(loaded ? 'No cover' : 'Loading…', font, window.GetColourDUI(0), 0, 0, w, h, 0x1 | 0x4 | 0x20);
    }
}

function on_playback_new_track() { load(); }
function on_playback_stop(reason) { if (reason !== 2) load(); }
function on_item_focus_change() { if (!fb.IsPlaying) load(); }
function on_colours_changed() { window.Repaint(); }
load();
)JS";

const char* const kPlaylist = R"JS(
window.DefineScript('Playlist', { author: 'JS Panel' });
const VK_DELETE = 0x2E, VK_UP = 0x26, VK_DOWN = 0x28, VK_RETURN = 0x0D, MK_CONTROL = 8, MK_SHIFT = 4;
const font = gdi.Font('Segoe UI', 13, 0), bold = gdi.Font('Segoe UI', 13, 1);
const tfLeft = fb.TitleFormat('[%tracknumber%. ]%title%[  ·  %artist%]'), tfRight = fb.TitleFormat('%length%');
const rowH = 24;
let rows = [], top = 0, anchor = -1, dropRow = -1, downAt = null;

function colour(i) { return window.GetColourDUI(i); }
function playlist() { return plman.ActivePlaylist; }
function visible() { return Math.max(1, Math.floor(window.Height / rowH)); }
function clamp_top() { top = Math.max(0, Math.min(top, rows.length - visible())); }

function refresh() {
    const p = playlist();
    const items = p >= 0 ? plman.GetPlaylistItems(p) : fb.CreateHandleList();
    const left = tfLeft.EvalWithMetadbs(items), right = tfRight.EvalWithMetadbs(items);
    rows = left.map((t, i) => [t, right[i]]);
    clamp_top();
    window.Repaint();
}

function row_at(y) {
    const r = top + Math.floor(y / rowH);
    return r >= 0 && r < rows.length ? r : -1;
}

function on_paint(gr) {
    const w = window.Width, h = window.Height, p = playlist();
    gr.FillSolidRect(0, 0, w, h, colour(1));
    if (p < 0 || !rows.length) {
        gr.GdiDrawText(p < 0 ? 'No playlist' : 'Empty playlist: drop files here', font, colour(0), 0, 0, w, h, 0x1 | 0x4 | 0x20);
        return;
    }
    const playing = plman.GetPlayingItemLocation();
    const focus = plman.GetPlaylistFocusItemIndex(p);
    for (let i = top, y = 0; i < rows.length && y < h; ++i, y += rowH) {
        const selected = plman.IsPlaylistItemSelected(p, i);
        if (selected) gr.FillSolidRect(0, y, w, rowH, colour(3));
        if (i === focus) gr.DrawRect(0, y, w - 1, rowH - 1, 1, colour(2));
        const isPlaying = playing.IsValid && playing.PlaylistIndex === p && playing.PlaylistItemIndex === i;
        const f = isPlaying ? bold : font;
        gr.GdiDrawText(rows[i][1], f, colour(0), w - 70, y, 62, rowH, 0x2 | 0x4 | 0x20);
        gr.GdiDrawText(rows[i][0], f, isPlaying ? colour(2) : colour(0), 8, y, w - 86, rowH, 0x4 | 0x20 | 0x8000);
    }
    if (dropRow >= 0) gr.FillSolidRect(0, (dropRow - top) * rowH - 1, w, 2, colour(2));
}

function select_row(r, mask) {
    const p = playlist();
    if (mask & MK_SHIFT && anchor >= 0) {
        const a = Math.min(anchor, r), b = Math.max(anchor, r), rng = [];
        for (let i = a; i <= b; ++i) rng.push(i);
        plman.ClearPlaylistSelection(p);
        plman.SetPlaylistSelection(p, rng, true);
    } else if (mask & MK_CONTROL) {
        plman.SetPlaylistSelectionSingle(p, r, !plman.IsPlaylistItemSelected(p, r));
        anchor = r;
    } else if (!plman.IsPlaylistItemSelected(p, r)) {
        plman.ClearPlaylistSelection(p);
        plman.SetPlaylistSelectionSingle(p, r, true);
        anchor = r;
    }
    plman.SetPlaylistFocusItem(p, r);
}

function on_mouse_lbtn_down(x, y, mask) {
    const r = row_at(y);
    downAt = r >= 0 ? { x, y } : null;
    if (r < 0) {
        plman.ClearPlaylistSelection(playlist());
        return;
    }
    select_row(r, mask);
}

function on_mouse_lbtn_up(x, y, mask) {
    const r = row_at(y);
    if (downAt && r >= 0 && !(mask & (MK_CONTROL | MK_SHIFT))) {
        plman.ClearPlaylistSelection(playlist());
        plman.SetPlaylistSelectionSingle(playlist(), r, true);
        anchor = r;
    }
    downAt = null;
}

function on_mouse_move(x, y, mask) {
    if (!downAt || !(mask & 1) || Math.abs(x - downAt.x) + Math.abs(y - downAt.y) < 6) return;
    downAt = null;
    const items = plman.GetPlaylistSelectedItems(playlist());
    if (items.Count) fb.DoDragDrop(window.ID, items, 1, {});
}

function on_mouse_lbtn_dblclk(x, y) {
    const r = row_at(y);
    if (r >= 0) plman.ExecutePlaylistDefaultAction(playlist(), r);
}

function on_mouse_wheel(step) {
    top -= step * 3;
    clamp_top();
    window.Repaint();
}

function on_key_down(vk) {
    const p = playlist();
    if (p < 0) return false;
    const focus = plman.GetPlaylistFocusItemIndex(p);
    if (vk === VK_DELETE) {
        plman.UndoBackup(p);
        plman.RemovePlaylistSelection(p, false);
        return true;
    }
    if (vk === VK_RETURN && focus >= 0) {
        plman.ExecutePlaylistDefaultAction(p, focus);
        return true;
    }
    if ((vk === VK_UP || vk === VK_DOWN) && rows.length) {
        const r = Math.max(0, Math.min(rows.length - 1, focus + (vk === VK_UP ? -1 : 1)));
        select_row(r, utils.IsKeyPressed(0x10) ? MK_SHIFT : 0);
        if (r < top) top = r;
        if (r >= top + visible()) top = r - visible() + 1;
        window.Repaint();
        return true;
    }
    return false;
}

function drop_row(y) {
    const r = top + Math.round(y / rowH);
    return Math.max(0, Math.min(rows.length, r));
}

function on_drag_enter(action, x, y) { on_drag_over(action, x, y); }
function on_drag_over(action, x, y) {
    if (playlist() < 0 || plman.IsPlaylistLocked(playlist())) { action.Effect = 0; return; }
    action.Effect = 1;
    dropRow = drop_row(y);
    window.Repaint();
}
function on_drag_leave() { dropRow = -1; window.Repaint(); }
function on_drag_drop(action, x, y) {
    const p = playlist();
    if (p < 0 || plman.IsPlaylistLocked(p)) { action.Effect = 0; return; }
    plman.UndoBackup(p);
    action.Playlist = p;
    action.Base = drop_row(y);
    action.ToSelect = true;
    action.Effect = 1;
    dropRow = -1;
    window.Repaint();
}

function on_size() { clamp_top(); }
function on_playlist_switch() { top = 0; anchor = -1; refresh(); }
function on_playlist_items_added(p) { if (p === playlist()) refresh(); }
function on_playlist_items_removed(p) { if (p === playlist()) refresh(); }
function on_playlist_items_reordered(p) { if (p === playlist()) refresh(); }
function on_playlist_items_selection_change() { window.Repaint(); }
function on_item_focus_change() { window.Repaint(); }
function on_metadb_changed() { refresh(); }
function on_playback_new_track() { window.Repaint(); }
function on_playback_stop() { window.Repaint(); }
function on_colours_changed() { window.Repaint(); }
refresh();
)JS";

const char* const kSeekbar = R"JS(
window.DefineScript('Seekbar and volume', { author: 'JS Panel' });
const font = gdi.Font('Segoe UI', 12, 0);
let drag = null;

function pos2vol(p) { return 50 * Math.log(0.99 * p + 0.01) / Math.LN10; }
function vol2pos(v) { return (Math.pow(10, v / 50) - 0.01) / 0.99; }
function format_time(s) {
    s = Math.max(0, Math.floor(s));
    return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}
function layout() {
    const w = window.Width, h = window.Height, vw = Math.min(140, Math.floor(w * 0.3));
    return { seek: { x: 52, y: Math.floor(h / 2) - 3, w: Math.max(0, w - vw - 120), h: 6 },
             vol: { x: w - vw - 12, y: Math.floor(h / 2) - 3, w: vw, h: 6 } };
}

function bar(gr, r, frac) {
    gr.FillRoundRect(r.x, r.y, r.w, r.h, 3, 3, window.GetColourDUI(3));
    if (frac > 0) gr.FillRoundRect(r.x, r.y, Math.max(r.h, Math.round(r.w * Math.min(1, frac))), r.h, 3, 3, window.GetColourDUI(2));
}

function on_paint(gr) {
    const w = window.Width, h = window.Height, l = layout(), text = window.GetColourDUI(0);
    gr.FillSolidRect(0, 0, w, h, window.GetColourDUI(1));
    const len = fb.PlaybackLength, pos = fb.PlaybackTime;
    gr.GdiDrawText(format_time(pos), font, text, 4, 0, 44, h, 0x2 | 0x4 | 0x20);
    gr.GdiDrawText(len > 0 ? format_time(len) : '--:--', font, text, l.seek.x + l.seek.w + 6, 0, 44, h, 0x4 | 0x20);
    bar(gr, l.seek, len > 0 ? pos / len : 0);
    bar(gr, l.vol, vol2pos(fb.Volume));
}

function apply(x) {
    const l = layout();
    const r = drag === 'seek' ? l.seek : l.vol;
    const f = Math.max(0, Math.min(1, (x - r.x) / Math.max(1, r.w)));
    if (drag === 'seek') { if (fb.PlaybackLength > 0) fb.PlaybackTime = f * fb.PlaybackLength; }
    else fb.Volume = pos2vol(f);
    window.Repaint();
}

function hit(r, x, y) { return x >= r.x - 4 && x <= r.x + r.w + 4 && y >= r.y - 10 && y <= r.y + r.h + 10; }
function on_mouse_lbtn_down(x, y) {
    const l = layout();
    drag = hit(l.seek, x, y) ? 'seek' : hit(l.vol, x, y) ? 'vol' : null;
    if (drag) apply(x);
}
function on_mouse_move(x) { if (drag) apply(x); }
function on_mouse_lbtn_up() { drag = null; }
function on_mouse_wheel(step) { fb.Volume = Math.max(-100, Math.min(0, fb.Volume + step)); }
function on_playback_time() { window.Repaint(); }
function on_playback_seek() { window.Repaint(); }
function on_playback_stop() { window.Repaint(); }
function on_playback_new_track() { window.Repaint(); }
function on_volume_change() { window.Repaint(); }
function on_colours_changed() { window.Repaint(); }
)JS";

}

const std::vector<Sample>& samples() {
    static const std::vector<Sample> list = {
        { "Now playing", "now_playing.js", sample_script() },
        { "Album art", "album_art.js", kAlbumArt },
        { "Playlist", "playlist.js", kPlaylist },
        { "Seekbar and volume", "seekbar.js", kSeekbar },
    };
    return list;
}

}
