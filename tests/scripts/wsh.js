// ==PREPROCESSOR==
// @name "JS Panel WSH check"
// @author "JS Panel"
// @feature "v1.4"
// @feature "watch-metadb"
// ==/PREPROCESSOR==
var g_font = gdi.Font('Segoe UI', 18, 1);
var g_small = gdi.Font('Segoe UI', 13, 0);
var g_render = gdi.CreateStyleTextRender();
g_render.OutLineText(0xffffffff, 0xff1f4f8f, 2);
var g_ticks = 0, g_painted = 0;
var g_timer = window.CreateTimerInterval(100);
var g_files = utils.Glob(fb.ProfilePath + '*').toArray();
var g_items = new Enumerator([1, 2, 3]), g_sum = 0;
for (; !g_items.atEnd(); g_items.moveNext()) g_sum += g_items.item();
var g_unknown = typeof fb.NoSuchWshApi;
console.log('wsh: loaded colour=' + (typeof window.GetColorDUI(1)) + ' sys=' + (typeof utils.GetSysColor(5)) + ' playlists=' + (fb.PlaylistCount > 0) +
            ' glob=' + (typeof g_files.length) + ' enum=' + g_sum + ' vb=' + new VBArray([4, 5]).toArray().length + ' height=' + (g_font.Height > 0) +
            ' match=' + utils.PathWildcardMatch('*.JS', 'a.js'));
CollectGarbage();

function on_timer(id) {
    if (id !== g_timer.ID) return;
    window.Repaint();
    if (++g_ticks === 2) {
        window.KillTimer(g_timer);
        console.log('wsh: timer ticks=' + g_ticks);
    }
}

function on_colors_changed() {
    window.Repaint();
}

function on_paint(gr) {
    gr.FillGradRect(0, 0, window.Width, window.Height, 90, 0xff20304a, 0xff101820);
    gr.FillRoundRect(10, 10, window.Width - 20, 44, 8, 8, 0xff3a6ea5);
    g_render.RenderStringRect(gr, 'WSH Panel Mod script', g_font, 20, 10, window.Width - 40, 44, 0x11005000);
    var m = gr.MeasureString('WSH Panel Mod script', g_font, 0, 0, window.Width, 100);
    gr.GdiDrawText('lines=' + m.lines + ' chars=' + m.chars + ' ticks=' + g_ticks, g_small, 0xffd0d0d0, 20, 64, window.Width - 40, 24, 0);
    gr.DrawRect(4, 4, window.Width - 9, window.Height - 9, 1, 0xff808080);
    if (++g_painted === 1) console.log('wsh: paint lines=' + m.lines + ' chars=' + m.chars);
}
