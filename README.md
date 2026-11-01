# JS Panel (foo_js_panel)

JavaScript panels for **foobar2000 v2** on **Windows (x64, x86, ARM64) and macOS** (universal).
A panel runs a script that draws itself and reacts to the mouse and to playback, with an API
modelled on **Spider Monkey Panel** (`on_paint(gr)`, `gr.FillSolidRect`, `fb.TitleFormat`,
`window.SetInterval`, …). The goal is for themes built on Spider Monkey Panel / JScript Panel,
which only ever ran on Windows, to run on macOS too.

The engine is [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (ES2023), built into the
component. Nothing else needs installing.

> **Status: early.** The engine, the panel on both platforms and most of the Spider Monkey
> Panel API work, and the Eole theme runs as a Panels UI skin ([how](#running-the-eole-theme)).
> See [Roadmap](#roadmap) for what's left.

## Installation

Install `foo_js_panel_<version>.fb2k-component` from the
[releases](https://github.com/santiagorod92/foo_js_panel/releases) (*Preferences › Components ›
Install…*). The same file holds the macOS bundle and the three Windows builds.

- **Windows (Default UI):** *View › Layout › Enable layout editing mode*, right-click where it
  goes › *Add New UI Element…* › **JS Panel**.
- **macOS:** *View › Layout › Edit Layout*, add **JS Panel**.
- **Panels UI:** `$panel(MyPanel,JS Panel,x,y,w,h)` in a skin script.

A new panel runs a built-in sample (now playing, a progress bar, click to play/pause).
Right-click it:
- **Edit script…** opens the script in an editor window: syntax is checked as you type (the
  status line shows the error and its line), **Apply** (Ctrl+S / ⌘S) saves the file and reloads
  the panel, **OK** (Ctrl+Enter / ⌘↩) applies and closes, **Revert** goes back to what was there
  when it opened. Editing the built-in sample saves it as a new file in
  `<profile>/foo_js_panel/` and the panel switches to it.
- **Choose script file…** picks a `.js` file; **Reload script** re-reads it after you edit it
  elsewhere.
 On Windows the choice is saved with that panel; on macOS (where layout elements have no
settings of their own) it's the default script for every JS Panel.

## Works with Panels UI

[Panels UI (reborn)](https://github.com/santiagorod92/foo_ui_panels) hosts a JS Panel like any
other panel: `$panel(Browser,JS Panel,0,40,%_width%,$eval(%_height% - 40))`, and
`panel.Browser.script = js/browser.js` in the skin's `foo_ui_panels.ini` picks the script that
panel runs (relative to the skin folder), so every JS Panel in a skin has its own. With JS Panel
installed, Panels UI's layout wizard also offers three layouts built on it, each written out with
its scripts in `js/`, ready to edit:
- **Albums + JS**: the Album browser with a now-playing bar (`js/now_playing.js`:
  the album cover, title, artist and album, progress; click to play/pause, wheel for next/previous).
- **Playlist + JS**: the playlist (double-click or Enter plays, Delete removes, drop files to
  add) with the same bar.
- **Library + Navidrome**: two JS Panels on the left, the Media Library as artists › albums
  (`js/library_tree.js`) and the Navidrome artists (`js/navidrome_artists.js`), next to the
  playlist. Click to select (a panel takes the keyboard when clicked), then double-click or Enter
  plays (the library one through a "Library selection" playlist) and Ctrl+double-click /
  Ctrl+Enter adds; arrows, Page Up/Down, Home/End move, ←/→ fold/unfold an artist.

Both components work without the other. The two share their drawing code (Panels UI's `gfx::Canvas`, GDI+ on Windows and
CoreGraphics/CoreText on macOS), so text and shapes look the same in both.

## Running the Eole theme

[Eole](https://github.com/Ottodix/Eole-foobar-theme) is a Columns UI + Spider Monkey Panel
theme. Two scripts from a source checkout of this repo turn it into a Panels UI skin whose panels
are JS Panels, so it runs on macOS too, with your [Navidrome](https://www.navidrome.org/) server
as the library if foo_navidrome is installed. Eole isn't redistributed here: you download it
yourself, and the scripts only read and patch your copy.

**You need:** foobar2000 v2, JS Panel, [Panels UI](https://github.com/santiagorod92/foo_ui_panels)
as the user interface, Python 3, and this repo (`git clone
https://github.com/santiagorod92/foo_js_panel`). Optional:
[foo_navidrome](https://github.com/santiagorod92/foo_navidrome) 1.38 or later for the server's
tracks, ratings and artist photos. Columns UI, Spider Monkey Panel and Eole's other components
aren't needed.

In the commands, `PROFILE` is your foobar2000 profile folder: `~/Library/foobar2000-v2` on
macOS, `%APPDATA%\foobar2000-v2` on Windows (or the portable install's `profile` folder). On
Windows, write `py` instead of `python3` and `\` instead of `/`.

1. **Get Eole.** Download it from its GitHub page and copy its `themes/eole` folder to
   `PROFILE/themes/eole`. The scripts look for it there (`fb.ProfilePath + 'themes\\eole\\…'`).
2. **Patch it** for JS Panel and Navidrome:
   ```
   python3 tools/eole_patches.py PROFILE/themes/eole
   ```
   - `navidrome://` tracks can be rated like local files.
   - Artist photos in the Biography also come from Navidrome (last.fm's photo pages don't always
     answer foobar2000).
   - Enter in the Library Tree replaces the playlist with the selection and plays it.

   Running it again is safe: a patch that's already there is skipped.
3. **Build the skin** from Eole's Columns UI layout:
   ```
   python3 tools/eole_skin.py PROFILE/themes/eole/columnsUI_eole.fcl PROFILE/foo_ui_panels-skins/Eole
   ```
   It writes one script per panel (title bar, controls, cover, filters, Library Tree, album
   browser, tracks, playlists, Biography, now playing) with Eole's saved settings, and a
   `main.txt` that lays them out per view. Eole's view buttons (Library, Playlists, Now playing,
   Visualization, mini player) switch the layout through Panels UI variables.
   Keep the folder name: each panel's settings are stored under its script's path, so a renamed
   folder starts from the defaults again.
4. **Pick the skin:** *Preferences › Display › Panels UI (reborn)*, choose **Eole** in the skin
   list (or **Browse…** to its folder). Restart foobar2000.
5. **Set it up:**
   - **Dark theme:** Eole's ☰ menu (top left) › *Dark theme*, once in each view (Library,
     Playlists, Now playing…). If a panel shown later (such as the Library Tree) is still light,
     toggle it off and on while that panel is visible.
   - **Navidrome library:** on by default (*View › JS Panel › Navidrome tracks in the library*):
     the server's tracks join the Media Library in the filters, the Library Tree and the album
     browser. The first start after foobar2000 opens takes a moment while the list loads.
   - **Title bar:** the system title bar is hidden and Eole's own one (drag it to move the
     window, double-click to maximize) takes its place. *View › Panels UI › Skin commands ›
     Toggle title bar* brings the system one back.
   - **Library filter:** the arrow next to *Genres* switches between Genres, Artists, Albums and
     the Library Tree. In Artists, the photos come from the Biography's cache and, for artists
     it hasn't seen yet, from Navidrome. The Library Tree's arrow menu picks its view (by folder,
     artist, album artist, album, genre, year).
   - **Tracklist background:** the tracks shown under an album take their colour from the cover,
     so a light cover gives a light list. Right-click the album browser › *Tracklist ›
     Background › Transparent background* keeps the theme's own colour.
   - **Biography:** last.fm, Wikipedia and AllMusic text, with photos from last.fm and
     Navidrome. On macOS last.fm's pages may answer foobar2000 with a JavaScript check instead
     (seen in testing; Windows got them), and then the Wikipedia text and the Navidrome photos
     still show.

To update Eole, copy the new `themes/eole` over the old one and repeat steps 2 and 3; your
settings stay in `PROFILE/foo_js_panel/properties/`.

## What's implemented

| Object | Members |
|---|---|
| callbacks | `on_paint(gr)`, `on_size(w, h)`, `on_key_down(vkey)` (Windows virtual-key codes on both platforms; return `true` when handled, other keys go on to foobar2000's shortcuts), `on_get_album_art_done(handle, art_id, image, path)`, `on_library_items_added / removed / changed(handles)`, `on_mouse_move / lbtn_down / lbtn_up / lbtn_dblclk / rbtn_down / rbtn_up (x, y, mask)` (return `true` from `on_mouse_rbtn_up` to skip the default menu), `on_mouse_mbtn_down / mbtn_up / mbtn_dblclk (x, y, mask)`, `on_mouse_leave()`, `on_mouse_wheel(step)`, `on_key_up(vkey)`, `on_char(codepoint)` (WM_CHAR codes on both platforms: Backspace 8, Enter 13, Esc 27), `on_focus(is_focused)`, `on_playback_starting(cmd, paused)`, `on_playback_new_track(handle)`, `on_playback_stop(reason)`, `on_playback_pause(state)`, `on_playback_time(time)`, `on_playback_seek(time)`, `on_playback_edited(handle)`, `on_playback_dynamic_info()`, `on_playback_dynamic_info_track()`, `on_volume_change(db)`, `on_playlist_switch()`, `on_playlists_changed()`, `on_playlist_items_added(p)`, `on_playlist_items_removed(p, new_count)`, `on_playlist_items_reordered(p)`, `on_playlist_items_selection_change()`, `on_item_focus_change(p, from, to)`, `on_playlist_item_ensure_visible(p, i)`, `on_playback_order_changed(order)`, `on_metadb_changed(handles, fromhook)`, `on_notify_data(name, info)`, `on_load_image_done(task_id, image, path)`, `on_script_unload()` |
| `gr` | `FillSolidRect`, `DrawRect`, `DrawLine`, `FillRoundRect(x, y, w, h, arc_w, arc_h, colour)` (arcs are radii), `FillEllipse`, `FillPolygon(colour, mode, points)`, `DrawEllipse`, `DrawRoundRect`, `DrawPolygon(colour, line_width, points)` (points as `[x0, y0, x1, y1, …]` or `[[x, y], …]`), `FillGradRect`, `DrawString` (StringFormat flags), `GdiDrawText` (`DT_*` flags; with `DT_CALCRECT` the box grows to the text, top, centre or bottom aligned, as SMP does), `CalcTextWidth`, `CalcTextHeight`, `MeasureString` → `{ X, Y, Width, Height, Lines, Chars }`, `EstimateLineWrap(text, font, width)` → `[line, width, …]`, `DrawImage(img, dx, dy, dw, dh, sx, sy, sw, sh, angle, alpha)`, `GdiDrawBitmap`, `GdiAlphaBlend`; `SetSmoothingMode`, `SetTextRenderingHint`, `SetInterpolationMode` are accepted. Colours with alpha work for every shape and `DrawString` (`GdiDrawText` ignores alpha, as in SMP). The `gr` from `on_paint` only works inside it; one from `GetGraphics` until `ReleaseGraphics` |
| `gdi` | `Font(name, pxSize, style)` → `Name`, `Size`, `Style`; `Image(path)` (relative to the script's folder), `CreateImage(w, h)` (transparent), `LoadImageAsync(window_id, path)` → task id, then `on_load_image_done`; `LoadImageAsyncV2(window_id, path)` → Promise of the image |
| `GdiBitmap` | `Width`, `Height`, `GetGraphics()` / `ReleaseGraphics(gr)`, `Clone(x, y, w, h)`, `Resize(w, h, mode)` (5 = nearest neighbour), `RotateFlip(mode)` (GDI+ `RotateFlipType`), `ApplyMask(mask)` (white = transparent, same size), `ApplyAlpha(alpha)`, `InvertColours()`, `StackBlur(radius)`, `GetColourScheme(count)`, `GetColourSchemeJSON(count)`, `CreateRawBitmap()`, `SaveAs(path)` (PNG), `Dispose()` |
| `utils` | `IsKeyPressed(vk)` (on macOS ⌘ counts as `VK_CONTROL`), `GetAlbumArtAsync(window.ID, handle, art_id)` (0 front, 1 back, 2 disc, 3 icon, 4 artist; the now-playing track's front cover comes from foobar2000's shared loader, so Navidrome tracks get theirs), `CheckComponent(name, is_dll = true)`, `CheckFont`, `FileTest(path, 'e' / 's' / 'd' / 'split')`, `IsFile`, `IsDirectory`, `FileExists`, `GetFileSize`, `ReadTextFile`, `WriteTextFile`, `Glob(pattern, exc_mask, inc_mask)`, `SplitFilePath`, `FormatDuration`, `FormatFileSize`, `InputBox(window_id, prompt, caption, def, error_on_cancel)`, `GetAlbumArtV2(handle, art_id)` (synchronous), `GetAlbumArtAsyncV2(window_id, handle, art_id)` → Promise of `{ image, path }`, `Version` (`"1.6.1"`, the SMP version scripts check), `ShowHtmlDialog(id, code, { data })`: there's no HTML engine, so a dialog that only shows text and buttons becomes a native message box and its callback gets the same answer; one with a form is answered Cancel (and logged) |
| handles | `fb.GetNowPlaying()`, `fb.GetLibraryItems()`, `fb.CreateHandleList()`; `FbMetadbHandle`: `Path`, `RawPath`, `Length`, `FileSize`, `Compare(h)`, `GetFileInfo()` → `MetaCount`, `MetaFind`, `MetaName`, `MetaValue`, `MetaValueCount`, `InfoCount`, `InfoFind`, `InfoName`, `InfoValue`; `FbMetadbHandleList`: `Count`, `Item(i)`, `Add`, `AddRange`, `Insert`, `InsertRange`, `Find`, `BSearch`, `Remove`, `RemoveById`, `RemoveRange`, `RemoveAll`, `MakeIntersection`, `MakeUnion`, `MakeDifference`, `OrderByFormat(tf, dir)`, `OrderByPath`, `OrderByRelativePath`, `Sort`, `GetLibraryRelativePaths`, `CalcTotalDuration`, `CalcTotalSize`, `Clone`, `Convert()` |
| `plman` | `PlaylistCount`, `ActivePlaylist` (get/set), `PlayingPlaylist`, `GetPlaylistName`, `FindOrCreatePlaylist(name, unlocked)`, `ClearPlaylist`, `InsertPlaylistItems(p, base, handles, select)`, `ExecutePlaylistDefaultAction(p, i)`, `PlaylistItemCount`, `GetPlaylistItems`, `GetPlaylistSelectedItems`, `IsPlaylistItemSelected`, `SetPlaylistSelection(p, indexes, state)`, `SetPlaylistSelectionSingle`, `ClearPlaylistSelection`, `RemovePlaylistSelection(p, crop)`, `MovePlaylistSelection`, `GetPlaylistFocusItemIndex`, `SetPlaylistFocusItem`, `SetPlaylistFocusItemByHandle`, `EnsurePlaylistItemVisible`, `CreatePlaylist`, `DuplicatePlaylist`, `RemovePlaylist`, `RemovePlaylistSwitch`, `RenamePlaylist`, `MovePlaylist`, `IsAutoPlaylist`, `IsPlaylistLocked`, `CreateAutoPlaylist`, `ShowAutoPlaylistUI`, `PlaybackOrder` (get/set), `GetPlayingItemLocation()`, `SortByFormat`, `SortByFormatV2(p, pattern, dir)`, `UndoBackup`, `AddItemToPlaybackQueue`, `AddPlaylistItemToPlaybackQueue`, `FlushPlaybackQueue`, `GetPlaybackQueueHandles` |
| `fb` | `TitleFormat(expr).Eval()` (now playing, else the focused track), `.EvalWithMetadb(h)`, `.EvalWithMetadbs(list)` (an array), `IsPlaying`, `IsPaused`, `PlaybackTime`, `PlaybackLength`, `Play`, `Pause`, `PlayOrPause`, `Stop`, `Next`, `Prev`, `Random`, `trace`, `ProfilePath`, `ComponentPath`, `FoobarPath`, `Version`, `Volume` (get/set, dB), `VolumeUp`, `VolumeDown`, `VolumeMute`, `StopAfterCurrent`, `CursorFollowPlayback`, `PlaybackFollowCursor` (get/set), `RunMainMenuCommand('Playback/Stop')`, `RunContextCommand(path)`, `RunContextCommandWithMetadb(path, handles)`, `ShowPopupMessage`, `AcquireUiSelectionHolder()` → `SetSelection(handles)`, `SetPlaylistSelectionTracking()`, `SetPlaylistTracking()`, `Dispose()`; `GetSelection()`, `GetSelections()`, `CreateContextMenuManager()` → `InitContext(handles)`, `InitContextPlaylist()`, `InitNowPlaying()`, `BuildMenu(menu, base_id, max_id)`, `ExecuteByID(id)`; `CreateMainMenuManager()` → `Init('File' / 'Edit' / 'View' / 'Playback' / 'Library' / 'Help')`, `BuildMenu(menu, base_id, count)`, `ExecuteByID(id)`; `GetFocusItem`, `GetQueryItems(handles, query)`, `IsMetadbInMediaLibrary`, `GetLibraryRelativePath` (with `\` separators on macOS too; foo_navidrome tracks get `Album artist\Album\NN - Title`), `CreateProfiler`, `Exit` |
| `window` | `ID`, `Width`, `Height`, `Repaint`, `RepaintRect`, `SetInterval`, `SetTimeout`, `ClearInterval`, `ClearTimeout`, `GetProperty(name, default)` / `SetProperty(name, value)` (kept per script in `<profile>/foo_js_panel/properties/`), `NotifyOthers(name, info)` (every other JS Panel gets `on_notify_data`, `info` copied as JSON), `Reload`, `EditScript`, `ShowConfigure`, `DefineScript` / `DefinePanel`, `ScriptInfo`, `InstanceType` (1, a Default UI element), `IsVisible`, `IsTransparent`, `DlgCode`, `MinWidth` / `MinHeight` / `MaxWidth` / `MaxHeight`, `MemoryLimit`, `TotalMemoryUsage`, `JsMemoryStats`, `GetColourDUI` / `GetColourCUI` / `GetFontDUI` / `GetFontCUI` (a fixed dark scheme for now), `SetCursor(IDC_*)` (arrow, hand, I-beam, cross, sizing and "no" cursors map to their macOS equivalents), `CreatePopupMenu()` → `AppendMenuItem(flags, id, text)` (`MF_GRAYED`, `MF_CHECKED`, `MF_RADIOCHECK`, `MF_SEPARATOR`), `AppendMenuSeparator`, `AppendTo(parent, flags, text)`, `CheckMenuItem`, `CheckMenuRadioItem(first, last, selected)`, `EnableMenuItem`, `TrackPopupMenu(x, y)` → the picked id or 0; `CreateThemeManager('TREEVIEW')` draws the tree's expand glyphs; `ShowProperties` and `CreateTooltip` are accepted and do nothing yet |
| global | `include(path)` (relative to the file that calls it), `console.log`, Promises, `setTimeout` / `setInterval` / `clearTimeout` / `clearInterval` (extra arguments are passed on), `new FbMetadbHandleList(handle \| list \| array)`, `new FbTitleFormat(expr)`, `new FbProfiler(name)`, a small `Intl` (`DateTimeFormat`, `NumberFormat`, `Collator` with `numeric`) since QuickJS has none |
| `ActiveXObject` | Stand-ins for what Windows themes create, on both platforms: `Scripting.FileSystemObject` (files, folders, text streams, `GetFolder(…).Files` / `.SubFolders` iterable with `for…of` and `.Count`, wildcards in `DeleteFile`), `Microsoft.XMLHTTP` / `Msxml2.XMLHTTP` / `WinHttp.WinHttpRequest.5.1` (GET and POST through foobar2000's HTTP client, async with `onreadystatechange` or synchronous), `WScript.Shell` (`Run` opens URLs and files, and on Windows runs commands; `cscript` of a download `.vbs` (XMLHTTP + `SaveToFile`, as Biography's `foo_lastfm_img.vbs`) downloads the URL into the file on both platforms; `RegRead` answers the Windows version and DPI keys themes read; `Popup` is a real message box), `htmlfile` (`parentWindow.clipboardData` and a small HTML DOM for scrapers: `createElement`, `innerHTML`, `getElementsByTagName` / `ClassName`, `className`, `innerText`, `href`, `src`, `getAttribute`, `parentNode`), `UIHacks` (`MainWindowState`, `FullScreen`, `MinSize` / `MaxSize` and `SetPseudoCaption` drive the main window: the caption area drags the window and a double-click maximizes; the other settings keep their defaults), `ESLyric` (accepts any call, does nothing), `Shell.Application` and `ScriptControl` (stubs). Any other ProgID throws like JScript did |
| `panelsui` | With [Panels UI](https://github.com/santiagorod92/foo_ui_panels) as the interface: `IsAvailable`, `GetVariable(name, default)`, `SetVariable(name, value)` (the skin's persistent variables, `$getpvar(name)`; the skin repaints), `RunAction(action)` (a skin button action, such as `MINIMODE:TOGGLE` or `PVAR:TOGGLE:name`) |
| `navidrome` | With [foo_navidrome](https://github.com/santiagorod92/foo_navidrome): `IsAvailable`, `IsConfigured`, `GetAlbumsAsync()` → `[{ Id, Name, Artist, ArtistId, CoverArtId, Year, SongCount, Created }]`, `GetArtistsAsync()` → `[{ Id, Name, AlbumCount, SongCount }]`, `GetArtistInfoAsync(name)` → `{ Id, Biography, ImageUrl, LastFmUrl }` (all Promises, rejected with the error when the server can't be reached), `PlayAlbum(id, replace = true, play = true)`, `AddAlbum(id)`, `PlayArtist(id, replace, play)`, `AddArtist(id)` |

Colours are ARGB numbers as in Spider Monkey Panel (`0xffRRGGBB`); fills honour alpha. Paths written
with `\` (as Windows themes do, `fb.ProfilePath + 'themes\\eole\\js\\x.js'`) also work on macOS.

A script error stops the script and the panel shows the message and stack. A script that runs
for more than 5 s in one go is stopped (time spent in a popup menu or an input box doesn't count),
and one runtime is capped at 512 MB.

**Diagnostics:** *View › JS Panel › Verbose logging* and *Copy diagnostics*. The log is
`foo_js_panel.log` in the foobar2000 profile folder.

## Roadmap

1. **Phase 0 (this):** engine, panel on both platforms, first API slice, embedded in Panels UI.
2. **Phase 1, the Spider Monkey Panel core** (in progress, ordered by what Eole uses most): done:
   handles and lists, file info, `plman`, panel properties, `NotifyOthers`, `fb` paths, volume,
   menu commands and queries, `utils` files, the playlist and metadb callbacks, popup menus with
   the context and main menu managers, `utils.InputBox`, `window.SetCursor`, `on_key_up` /
   `on_char` / `on_focus` and the middle button, off-screen images (`gdi.CreateImage`,
   `GetGraphics`, `Resize`, `ApplyMask`, `StackBlur`, colour schemes), translucent shapes, async
   image loading, the global timers. Target: the Spider Monkey Panel samples run unchanged.
3. **Phase 2, themes** (in progress): done: the `ActiveXObject` stand-ins above, and
   `make theme-check` to run every Spider Monkey Panel panel of a Columns UI layout export in the
   engine (with Eole's layout, 17 of its 18 panels load and paint offline; the last one needs a
   playing track), and Eole running as a Panels UI skin (`tools/eole_skin.py` +
   `tools/eole_patches.py`, see [Running the Eole theme](#running-the-eole-theme)). Next: a general
   Columns UI layout converter (splitters, Panel Stack Splitter), font substitution on macOS.
4. **Panels UI bridge** (done: `panelsui` reads and writes Panels UI variables and runs its
   actions). Next: skins sending messages to scripts.
5. **More from foo_navidrome** (done: every track in `fb.GetLibraryItems()` and artist info with
   v3 of its library contract). Next: search.
6. **Later:** JS drawn straight onto the Panels UI canvas (real transparency, zoom, mini mode).

## Reporting a problem

Use the [issue forms](https://github.com/santiagorod92/foo_js_panel/issues/new/choose) and paste
the output of *Copy diagnostics*. It's redacted: your home folder becomes `~` and your user name
`<user>`.

## Building from source

| | Used for development | Notes |
|---|---|---|
| **foobar2000 SDK** | `SDK-2026-10-01` from [reupen/foobar2000-sdk-unmodified](https://github.com/reupen/foobar2000-sdk-unmodified) | shared sibling checkout, like Panels UI and foo_navidrome |
| **foo_ui_panels** | sibling checkout `../foo_ui_panels` | its `src/gfx` + canvas backends are compiled in (`PANELS_UI_SRC`) |
| **JS engine** | QuickJS-ng 0.17.0, vendored in `third_party/quickjs` (MIT) | |
| **C++ standard** | C++20 | |
| **Windows** | clang-cl / lld-link (LLVM 22) + xwin, cross-compiled on Linux or macOS | x64 + x86 static CRT, ARM64EC dynamic CRT |
| **macOS** | CMake + Ninja (Xcode tools), universal arm64 + x86_64, macOS 11+ | |

```
make test          # unit tests (engine + API on a recording canvas), ASan/UBSan
make build-all     # Windows x64, ARM64EC, x86 + the macOS bundle
make mac-smoke     # smoke test in this Mac's foobar2000, hosted by Panels UI
make mac-demo TEMPLATE=library-js   # this Mac's foobar2000 with a Panels UI JS layout (albums-js, playlist-js, library-js)
make mac-restore   # undo mac-demo / mac-local (skin, settings, the components you had)
make render SCRIPT=x.js OUT=x.png ARGS='--size 600x56 --playing'   # one frame, offline (macOS)
make theme-check LAYOUT=theme.fcl PROFILE=dir   # every SMP panel of a Columns UI layout, offline (macOS)
make win11-arm-smoke ARCH=arm64ec   # same in the Windows 11 ARM VM (also x64, x86)
make win11-arm-menus ARCH=arm64ec   # a popup menu, an InputBox and keys in the VM, with screenshots
make win11-arm-images ARCH=arm64ec  # off-screen images and translucent shapes in the VM, with a screenshot
```

## Credits

- [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (MIT), the JavaScript engine.
- The API follows [Spider Monkey Panel](https://github.com/TheQwertiest/foo_spider_monkey_panel)
  by TheQwertiest, itself after WSH Panel Mod and JScript Panel. No code is taken from them.
- Thanks to [Ottodix](https://github.com/Ottodix), author of the
  [Eole](https://github.com/Ottodix/Eole-foobar-theme) theme, the target that drove much of this
  work, and to WilB for the Biography script that ships with it. Eole isn't redistributed here;
  [Running the Eole theme](#running-the-eole-theme) only patches and converts your own copy.

## License

MIT, see [LICENSE](LICENSE).
