window.DefineScript('JS Panel showcase', { author: 'foo_js_panel', version: '1' });

const fonts = { title: gdi.Font('Segoe UI', 20, 1), body: gdi.Font('Segoe UI', 13), small: gdi.Font('Segoe UI', 11), button: gdi.Font('Segoe UI', 12, 1) };
const tf = { title: fb.TitleFormat('%title%'), artist: fb.TitleFormat('%artist%'), album: fb.TitleFormat('%album% $if(%date%,· %date%)') };
const fso = new ActiveXObject('Scripting.FileSystemObject');
const shell = new ActiveXObject('WScript.Shell');
const clipboard = new ActiveXObject('htmlfile').parentWindow.clipboardData;

let cover = null, shadow = null, scheme = [], accent = 0xff0078d7, coverFor = null;
let status = 'Click a button, right-click for a menu, or type here.';
let typed = '';
let hover = -1;
const label = () => window.GetProperty('demo.label', 'Change me with "Input box"');

function track() { return fb.GetNowPlaying() || fb.GetFocusItem(); }

function circle(img, size) {
    const out = img.Resize(size, size);
    const mask = gdi.CreateImage(size, size);
    const g = mask.GetGraphics();
    g.FillSolidRect(0, 0, size, size, 0xffffffff);
    g.FillEllipse(0, 0, size, size, 0xff000000);
    mask.ReleaseGraphics(g);
    out.ApplyMask(mask);
    return out;
}

function makeShadow(size) {
    const s = gdi.CreateImage(size + 40, size + 40);
    const g = s.GetGraphics();
    g.FillEllipse(20, 24, size, size, 0xb0000000);
    s.ReleaseGraphics(g);
    s.StackBlur(14);
    return s;
}

function loadCover() {
    const h = track();
    const key = h ? h.RawPath + '|' + h.Length : '';
    if (key === coverFor) return;
    coverFor = key;
    cover = null;
    scheme = [];
    if (!h) { window.Repaint(); return; }
    utils.GetAlbumArtAsyncV2(window.ID, h, 0).then(r => {
        if (!r.image) { window.Repaint(); return; }
        cover = circle(r.image, 150);
        shadow = makeShadow(150);
        scheme = r.image.GetColourScheme(6);
        if (scheme.length) accent = scheme[0];
        window.Repaint();
    });
}

const buttons = [
    { text: 'Popup menu', run: (x, y) => showMenu(x, y) },
    { text: 'Input box', run: () => {
        const v = utils.InputBox(window.ID, 'New text for the label (saved with window.SetProperty):', 'JS Panel', label(), false);
        window.SetProperty('demo.label', v);
        status = 'Saved "' + v + '". Reload foobar2000 and it is still there.';
    } },
    { text: 'Message box', run: () => {
        const r = shell.Popup('Do you like it so far?', 0, 'WScript.Shell.Popup', 4);
        status = r === 6 ? 'You pressed Yes (6).' : 'You pressed No (' + r + ').';
    } },
    { text: 'Open last.fm', run: () => {
        const h = track();
        const artist = h ? tf.artist.EvalWithMetadb(h) : 'foobar2000';
        shell.Run('https://www.last.fm/music/' + encodeURIComponent(artist));
        status = 'Opened last.fm for ' + artist + ' (WScript.Shell.Run).';
    } },
    { text: 'Copy title', run: () => {
        const h = track();
        const t = h ? tf.artist.EvalWithMetadb(h) + ' - ' + tf.title.EvalWithMetadb(h) : '';
        clipboard.setData('Text', t);
        status = 'Copied to the clipboard: ' + clipboard.getData('Text');
    } },
    { text: 'Write a file', run: () => {
        const dir = fso.BuildPath(fb.ProfilePath + 'foo_js_panel', 'demo');
        if (!fso.FolderExists(dir)) fso.CreateFolder(dir);
        const file = fso.BuildPath(dir, 'log.txt');
        const ts = fso.OpenTextFile(file, 8, true);
        ts.WriteLine(new Date().toISOString() + ' ' + (track() ? tf.title.EvalWithMetadb(track()) : '-'));
        ts.Close();
        let lines = 0;
        const rd = fso.OpenTextFile(file, 1);
        while (!rd.AtEndOfStream) { rd.ReadLine(); lines++; }
        status = 'FileSystemObject: ' + file + ' now has ' + lines + ' line(s).';
    } },
    { text: 'HTTP request', run: () => {
        const x = new ActiveXObject('Microsoft.XMLHTTP');
        status = 'Asking api.github.com…';
        window.Repaint();
        x.open('GET', 'https://api.github.com/repos/santiagorod92/foo_js_panel', true);
        x.setRequestHeader('User-Agent', 'foo_js_panel demo');
        x.onreadystatechange = () => {
            if (x.readyState !== 4) return;
            try {
                const repo = JSON.parse(x.responseText);
                status = 'XMLHTTP ' + x.status + ': ' + repo.full_name + ', ' + repo.stargazers_count + ' star(s).';
            } catch (e) {
                status = 'XMLHTTP ' + x.status + ' (' + x.responseText.length + ' bytes)';
            }
            window.Repaint();
        };
        x.send();
    } },
];

function layout() {
    const top = 210, w = 120, h = 30, gap = 10, cols = Math.max(1, Math.floor((window.Width - 20 + gap) / (w + gap)));
    buttons.forEach((b, i) => { b.x = 20 + (i % cols) * (w + gap); b.y = top + Math.floor(i / cols) * (h + gap); b.w = w; b.h = h; });
}

function showMenu(x, y) {
    const m = window.CreatePopupMenu(), order = window.CreatePopupMenu(), ctx = window.CreatePopupMenu(), main = window.CreatePopupMenu();
    m.AppendMenuItem(0, 1, 'Say hello');
    m.AppendMenuItem(window.GetProperty('demo.checked', false) ? 0x8 : 0, 2, 'A checked item');
    m.AppendMenuItem(0x1, 3, 'A grayed item');
    m.AppendMenuSeparator();
    ['Default', 'Repeat (playlist)', 'Repeat (track)', 'Random', 'Shuffle (tracks)'].forEach((n, i) => order.AppendMenuItem(0, 10 + i, n));
    order.CheckMenuRadioItem(10, 14, 10 + Math.min(plman.PlaybackOrder, 4));
    order.AppendTo(m, 0, 'Playback order');
    const cm = fb.CreateContextMenuManager();
    const h = track();
    if (h) {
        cm.InitContext(new FbMetadbHandleList(h));
        cm.BuildMenu(ctx, 1000, 1999);
        ctx.AppendTo(m, 0, 'This track (context menu)');
    }
    const mm = fb.CreateMainMenuManager();
    mm.Init('Playback');
    mm.BuildMenu(main, 2000, 999);
    main.AppendTo(m, 0, 'Playback (main menu)');
    m.AppendMenuSeparator();
    m.AppendMenuItem(0, 4, 'Edit this script…');
    const id = m.TrackPopupMenu(x, y);
    if (id === 1) status = 'Hello from TrackPopupMenu!';
    else if (id === 2) window.SetProperty('demo.checked', !window.GetProperty('demo.checked', false));
    else if (id === 4) window.EditScript();
    else if (id >= 10 && id < 15) plman.PlaybackOrder = id - 10;
    else if (id >= 1000 && id < 2000) cm.ExecuteByID(id - 1000);
    else if (id >= 2000) mm.ExecuteByID(id - 2000);
    if (id) status = status === 'Hello from TrackPopupMenu!' ? status : 'Menu item ' + id + '.';
    window.Repaint();
}

function on_size() { layout(); }
function on_playback_new_track() { loadCover(); window.Repaint(); }
function on_item_focus_change() { if (!fb.IsPlaying) loadCover(); }
function on_playback_stop() { loadCover(); window.Repaint(); }

function on_paint(gr) {
    const W = window.Width, H = window.Height;
    gr.FillGradRect(0, 0, W, H, 90, 0xff1c1d22, 0xff111216);
    gr.FillSolidRect(0, 0, W, 4, accent);
    if (shadow) gr.DrawImage(shadow, 0, -6, shadow.Width, shadow.Height, 0, 0, shadow.Width, shadow.Height);
    if (cover) gr.DrawImage(cover, 20, 18, 150, 150, 0, 0, 150, 150);
    else gr.FillEllipse(20, 18, 150, 150, 0x30ffffff);
    const h = track();
    const x = 190;
    gr.GdiDrawText(h ? tf.title.EvalWithMetadb(h) : 'Nothing playing', fonts.title, 0xffffffff, x, 30, W - x - 20, 30, 0x20 | 0x800 | 0x8000);
    gr.GdiDrawText(h ? tf.artist.EvalWithMetadb(h) : 'Play something to see its cover here', fonts.body, 0xffb8bcc8, x, 62, W - x - 20, 20, 0x20 | 0x800 | 0x8000);
    gr.GdiDrawText(h ? tf.album.EvalWithMetadb(h) : '', fonts.small, 0xff8a8f9c, x, 84, W - x - 20, 18, 0x20 | 0x800 | 0x8000);
    scheme.forEach((c, i) => gr.FillRoundRect(x + i * 30, 112, 24, 24, 6, 6, c));
    if (scheme.length) gr.GdiDrawText('cover colours (GetColourScheme)', fonts.small, 0xff8a8f9c, x, 140, 300, 18, 0);
    gr.FillRoundRect(x, 164, Math.min(W - x - 20, 360), 26, 13, 13, 0x40ffffff);
    gr.DrawString(label(), fonts.body, 0xffffffff, x + 12, 164, Math.min(W - x - 20, 360) - 24, 26, 0x01001000);
    buttons.forEach((b, i) => {
        gr.FillRoundRect(b.x, b.y, b.w, b.h, 8, 8, i === hover ? accent : 0xff2c2f38);
        gr.DrawRoundRect(b.x, b.y, b.w, b.h, 8, 8, 1, 0x40ffffff);
        gr.GdiDrawText(b.text, fonts.button, 0xffffffff, b.x, b.y, b.w, b.h, 0x1 | 0x4 | 0x20);
    });
    const last = buttons[buttons.length - 1];
    const sy = last ? last.y + last.h + 20 : 300;
    gr.GdiDrawText(status, fonts.body, 0xffd8dbe3, 20, sy, W - 40, 40, 0x10);
    gr.GdiDrawText('Typed: ' + (typed || '(click here, then type)'), fonts.small, 0xff8a8f9c, 20, sy + 44, W - 40, 20, 0x20);
}

function hit(x, y) { return buttons.findIndex(b => x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h); }

function on_mouse_move(x, y) {
    const i = hit(x, y);
    window.SetCursor(i >= 0 ? 32649 : 32512);
    if (i !== hover) { hover = i; window.Repaint(); }
}
function on_mouse_leave() { if (hover !== -1) { hover = -1; window.Repaint(); } }
function on_mouse_lbtn_up(x, y) {
    const i = hit(x, y);
    if (i < 0) return;
    try { buttons[i].run(x, y); } catch (e) { status = buttons[i].text + ' failed: ' + e.message; }
    window.Repaint();
}
function on_mouse_rbtn_up(x, y) { showMenu(x, y); return true; }
function on_char(c) {
    if (c === 8) typed = typed.slice(0, -1);
    else if (c >= 32) typed += String.fromCodePoint(c);
    window.Repaint();
}

layout();
loadCover();
