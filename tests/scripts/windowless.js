include('lib/colours.js');
const font = gdi.Font('Segoe UI', 16, 1);
let paints = 0, ticks = 0, focused = false, lastKey = 0, moved = false;
console.log('windowless: loaded name=' + panelsui.PanelName + ' transparent=' + window.IsTransparent);
const iv = window.SetInterval(() => {
    if (++ticks === 2) {
        window.ClearInterval(iv);
        console.log('windowless: timer ok');
    }
    window.Repaint();
}, 150);
function on_size(w, h) { console.log('windowless: size ' + w + 'x' + h); }
function on_paint(gr) {
    gr.FillSolidRect(0, 0, Math.floor(window.Width / 2), window.Height, RGB(30, 90, 160));
    gr.GdiDrawText('windowless ' + ticks + (focused ? ' focused' : '') + (lastKey ? ' key ' + lastKey : ''), font, RGB(255, 255, 255),
                   12, 12, window.Width - 24, 30, 0);
    if (++paints === 1 || paints === 3) console.log('windowless: paint ' + window.Width + 'x' + window.Height + ' n=' + paints);
}
function on_mouse_move() {
    if (moved) return;
    moved = true;
    console.log('windowless: move');
}
function on_mouse_lbtn_down(x, y) {
    console.log('windowless: down ' + x + ',' + y);
    window.SetCursor(32649);
}
function on_mouse_lbtn_up(x, y) { console.log('windowless: up ' + x + ',' + y); }
function on_mouse_wheel(step) { console.log('windowless: wheel ' + step); }
function on_focus(f) {
    focused = f;
    console.log('windowless: focus ' + f);
    window.Repaint();
}
function on_key_down(vk) {
    lastKey = vk;
    console.log('windowless: key ' + vk);
    window.Repaint();
    return true;
}
