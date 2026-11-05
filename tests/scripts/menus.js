function on_paint(gr) { gr.FillSolidRect(0, 0, window.Width, window.Height, 0xff203040); }
function on_focus(f) { console.log('menus: focus ' + f); }
function on_char(c) { console.log('menus: char ' + String.fromCodePoint(c)); }
let picking = false;
function on_key_up(vk) {
    console.log('menus: key_up ' + vk);
    if (vk !== 65 || picking) return;
    picking = true;
    window.SetTimeout(() => {
        console.log('picker: showing');
        console.log('picker: colour=' + (utils.ColourPicker(window.ID, 0xff123456) >>> 0).toString(16));
        window.SetProperty('menus.colour', 'ff123456');
        window.ShowProperties();
        console.log('properties: showing');
    }, 200);
}
if (window.ID === 1) {
    window.SetTimeout(() => {
        const m = window.CreatePopupMenu(), sub = window.CreatePopupMenu(), ctx = window.CreatePopupMenu();
        const mm = fb.CreateMainMenuManager();
        mm.Init('Playback');
        mm.BuildMenu(m, 1000, 500);
        m.AppendMenuSeparator();
        m.AppendMenuItem(0, 1, 'Checked item');
        m.CheckMenuItem(1, true);
        m.AppendMenuItem(1, 2, 'Grayed item');
        sub.AppendMenuItem(0, 10, 'Radio A');
        sub.AppendMenuItem(0, 11, 'Radio B');
        sub.CheckMenuRadioItem(10, 11, 11);
        sub.AppendTo(m, 0, 'Submenu');
        const cm = fb.CreateContextMenuManager();
        cm.InitContextPlaylist();
        cm.BuildMenu(ctx, 100, 999);
        ctx.AppendTo(m, 0, 'Context');
        console.log('menus: showing');
        console.log('menus: picked ' + m.TrackPopupMenu(10, 10));
        window.SetTimeout(() => {
            console.log('inputbox: showing');
            try {
                console.log('inputbox: value=' + utils.InputBox(window.ID, 'Type something:', 'JS Panel InputBox', 'default', true));
            } catch (e) {
                console.log('inputbox: cancelled');
            }
        }, 200);
    }, 1500);
}
