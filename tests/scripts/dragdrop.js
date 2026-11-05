let target = false, ready = false, dropList = -1, downX = -1, downY = -1, dragged = false;
function source_tracks() {
    for (let p = 0; p < plman.PlaylistCount; ++p) {
        if (plman.GetPlaylistName(p) === 'jsp-drop' || plman.PlaylistItemCount(p) < 2) continue;
        const items = plman.GetPlaylistItems(p);
        const out = fb.CreateHandleList();
        out.Add(items.Item(0));
        out.Add(items.Item(1));
        return out;
    }
    return fb.CreateHandleList();
}
function on_size(w, h) {
    if (ready || w <= 0 || h <= 0) return;
    ready = true;
    target = h < 100;
    if (target) {
        dropList = plman.FindOrCreatePlaylist('jsp-drop', true);
        plman.ClearPlaylist(dropList);
    } else {
        const copied = fb.CopyHandleListToClipboard(source_tracks());
        console.log('dragdrop: clipboard copied=' + copied + ' has=' + fb.CheckClipboardContents() + ' count=' + fb.GetClipboardContents(window.ID).Count);
    }
    console.log('dragdrop: ready ' + (target ? 'target' : 'source') + ' ' + w + 'x' + h);
}
function on_paint(gr) { gr.FillSolidRect(0, 0, window.Width, window.Height, target ? 0xff304060 : 0xff406030); }
function on_mouse_lbtn_down(x, y) {
    downX = x; downY = y; dragged = false;
    console.log('dragdrop: down ' + (target ? 'target' : 'source') + ' at ' + x + ',' + y);
}
function on_mouse_lbtn_up() { downX = -1; }
function on_mouse_move(x, y, mask) {
    if (target || dragged || downX < 0 || !(mask & 1) || Math.abs(y - downY) + Math.abs(x - downX) < 8) return;
    dragged = true;
    const effect = fb.DoDragDrop(window.ID, source_tracks(), 1 | 2, {});
    console.log('dragdrop: source effect=' + effect);
    downX = -1;
}
function on_drag_enter(action) {
    if (target) console.log('dragdrop: enter effect=' + action.Effect);
    action.Effect = target ? 1 : 0;
}
function on_drag_over(action) { action.Effect = target ? 1 : 0; }
function on_drag_drop(action, x, y) {
    if (!target) { action.Effect = 0; return; }
    action.Effect = 1;
    action.Playlist = dropList;
    action.Base = 0;
    action.ToSelect = false;
    console.log('dragdrop: drop at ' + x + ',' + y);
}
function on_playlist_items_added(p) {
    if (target && p === dropList) console.log('dragdrop: added ' + plman.PlaylistItemCount(p));
}
