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
const runs = window.GetProperty('smoke.runs', 0) + 1;
window.SetProperty('smoke.runs', runs);
console.log('smoke: properties runs=' + runs);
console.log('smoke: paths profile=' + (fb.ProfilePath.length > 0) + ' foobar=' + (fb.FoobarPath.length > 0) + ' version=' + fb.Version + ' instance=' + window.InstanceType);
console.log('smoke: menu Playback/Stop=' + fb.RunMainMenuCommand('Playback/Stop') + ' nope=' + fb.RunMainMenuCommand('Nope/Nothing'));
const ap = plman.ActivePlaylist;
const lib = fb.GetLibraryItems(), pool = lib.Count ? lib : plman.GetPlaylistItems(ap);
const tagged = fb.GetQueryItems(pool, 'TITLE PRESENT');
const first = pool.Count ? pool.Item(0) : null;
console.log('smoke: library=' + lib.Count + ' tracks=' + pool.Count + ' query=' + tagged.Count + ' meta=' + (first ? first.GetFileInfo().MetaCount : -1) + ' length=' + (first ? first.Length : -1));
console.log('smoke: plman playlists=' + plman.PlaylistCount + ' items=' + plman.PlaylistItemCount(ap) + ' selected=' + plman.GetPlaylistSelectedItems(ap).Count + ' order=' + plman.PlaybackOrder);
window.NotifyOthers('smoke', { from: window.ID });
function on_notify_data(name, info) { if (name === 'smoke') console.log('smoke: notify from ' + info.from + ' to ' + window.ID); }
window.SetCursor(32649);
const built = window.CreatePopupMenu(), cmm = fb.CreateContextMenuManager(), mmm = fb.CreateMainMenuManager();
cmm.InitContextPlaylist();
cmm.BuildMenu(built, 1, -1);
mmm.Init('Playback');
mmm.BuildMenu(built, 1000, 500);
console.log('smoke: menus built');
const solid = gdi.CreateImage(8, 8);
let pg = solid.GetGraphics();
pg.FillSolidRect(0, 0, 8, 8, 0xff336699);
solid.ReleaseGraphics(pg);
const half = gdi.CreateImage(8, 8);
pg = half.GetGraphics();
pg.FillSolidRect(0, 0, 4, 8, 0xa0ff0000);
half.ReleaseGraphics(pg);
console.log('smoke: pixels ' + (solid.GetColourScheme(1)[0] >>> 0).toString(16) + ' ' +
            JSON.parse(half.GetColourSchemeJSON(3)).map(c => (c.col >>> 0).toString(16) + ':' + c.freq).join(','));
const fso = new ActiveXObject('Scripting.FileSystemObject');
const fsoDir = fso.BuildPath(fb.ProfilePath + 'foo_js_panel', 'smoke-fso-' + window.ID);
if (fso.FolderExists(fsoDir)) fso.DeleteFolder(fsoDir);
fso.CreateFolder(fsoDir);
const fsoFile = fsoDir + '\\note.txt';
const stream = fso.CreateTextFile(fsoFile, true);
stream.WriteLine('hello');
stream.Close();
const back = fso.OpenTextFile(fsoFile, 1).ReadLine();
const files = fso.GetFolder(fsoDir).Files.Count;
fso.DeleteFolder(fsoDir);
const shellVersion = new ActiveXObject('WScript.Shell').RegRead('HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\CurrentMajorVersionNumber');
console.log('smoke: activex fso=' + back + ' files=' + files + ' gone=' + !fso.FolderExists(fsoDir) + ' reg=' + shellVersion +
            ' ui=' + new ActiveXObject('UIHacks').FrameStyle + ' timers=' + (typeof setTimeout));
const outputs = JSON.parse(fb.GetOutputDevices()), presets = JSON.parse(fb.GetDSPPresets());
const rg = fb.ReplaygainMode;
fb.ReplaygainMode = (rg + 1) % 4;
const rgSet = fb.ReplaygainMode;
fb.ReplaygainMode = rg;
const lockP = plman.CreatePlaylist(plman.PlaylistCount, 'smoke-lock-' + window.ID);
plman.SetPlaylistLockedActions(lockP, ['AddItems', 'RenamePlaylist']);
const lockInfo = plman.GetPlaylistLockedActions(lockP).join('+') + '/' + plman.GetPlaylistLockName(lockP);
plman.SetPlaylistLockedActions(lockP, []);
const unlocked = plman.GetPlaylistLockedActions(lockP).length;
plman.RemovePlaylist(lockP);
console.log('smoke: state outputs=' + outputs.length + ' active=' + outputs.filter(o => o.active).length + ' dsp=' + presets.length +
            ' rg=' + (rgSet === (rg + 1) % 4) + ' lock=' + lockInfo + ' unlocked=' + unlocked + ' charset=' + utils.DetectCharset(fb.ComponentPath + 'nope') +
            ' map=' + utils.MapString('Ab', 0, 0x200));
const top = fb.AlwaysOnTop;
let topSeen = 0;
function on_always_on_top_changed(state) {
    if (++topSeen === 1) { console.log('smoke: state-cb top=' + state); fb.AlwaysOnTop = top; }
}
fb.AlwaysOnTop = !top;
let menuSeen = 0;
function on_main_menu(index) { if (++menuSeen === 1) console.log('smoke: main menu ' + index); }
console.log('smoke: script command run=' + fb.RunMainMenuCommand('View/JS Panel/Script commands/Script command 2'));
const tip = window.CreateTooltip('Segoe UI', 14, 0);
tip.Text = 'smoke tooltip ' + window.ID;
tip.Activate();
console.log('smoke: tooltip ' + tip.Text);
const hex = c => (c >>> 0).toString(16);
const uiFont = window.GetFontDUI(0);
console.log('smoke: theme dui=' + hex(window.GetColourDUI(0)) + ',' + hex(window.GetColourDUI(1)) + ' cui=' + hex(window.GetColourCUI(4)) +
            ' font=' + uiFont.Name + ' ' + uiFont.Size + ' sys=' + hex(utils.GetSysColour(5)));
console.log('smoke: fonts segoe=' + utils.CheckFont('Segoe UI') + ' semibold=' + utils.CheckFont('Segoe UI Semibold') + ' nope=' + utils.CheckFont('Nope Font Xyz'));
