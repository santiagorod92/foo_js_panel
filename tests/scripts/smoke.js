include('lib/colours.js');
const font = gdi.Font('Segoe UI', 16, 1);
let painted = 0, ticks = 0;
console.log('smoke: loaded, include ok, title=' + JSON.stringify(fb.TitleFormat('%title%').Eval()));
const iv = window.SetInterval(() => {
    if (++ticks === 3) {
        window.ClearInterval(iv);
        console.log('smoke: timer ok');
        window.Repaint();
    }
}, 100);
window.SetTimeout(() => console.log('smoke: timeout ok'), 50);
function on_size(w, h) { console.log('smoke: size ' + w + 'x' + h); }
function on_paint(gr) {
    gr.FillSolidRect(0, 0, window.Width, window.Height, RGB(20, 60, 40));
    gr.FillRoundRect(20, 20, window.Width - 40, 40, 8, 8, RGB(60, 160, 100));
    const tw = gr.CalcTextWidth('smoke test', font);
    gr.GdiDrawText('smoke test: ' + ticks + ' ticks, text width ' + tw, font, RGB(255, 255, 255),
                   20, 80, window.Width - 40, 30, 0x1 | 0x4 | 0x20);
    if (++painted === 1 || ticks === 3) console.log('smoke: paint ' + window.Width + 'x' + window.Height + ' tw=' + tw + ' ticks=' + ticks);
}
console.log('smoke: navidrome available=' + navidrome.IsAvailable + ' configured=' + navidrome.IsConfigured);
if (navidrome.IsConfigured) {
    navidrome.GetArtistsAsync().then(
        a => console.log('smoke: navidrome artists=' + a.length + (a.length ? ' first=' + JSON.stringify(a[0].Name) : '')),
        e => console.log('smoke: navidrome error ' + e.message));
}
