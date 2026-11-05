# Changelog

All notable changes to foo_js_panel (JS Panel). Versions are the `v*` git tags; the GitHub
release of each version has the downloadable `.fb2k-component`.

## [1.3.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.2.0...v1.3.0) (2026-11-05)

### Highlights
- **Eole without Python**: unzip Eole, then *View › Panels UI › Import Columns UI layout…* and pick
  `themes/eole/columnsUI_eole.fcl` (Panels UI 1.20). JS Panel adapts Eole's files as it loads
  them, without changing them on disk: `navidrome://` tracks can be rated, the Biography's and the
  Artists filter's photos also come from Navidrome, and Enter in the Library Tree plays the
  selection. `tools/eole_patches.py` and `tools/eole_skin.py` are gone. Any other Columns UI +
  Spider Monkey Panel theme imports the same way.
- **JScript Panel 3 scripts run**: a compatibility layer picked from the script's
  `==PREPROCESSOR==` header (`@import` of `lodash` or a JScript Panel 3 file) or its JScript Panel
  3-only calls: `gr.WriteText`, `FillRectangle` and the rest of its drawing, JSON font strings,
  `utils`/`fb`/`plman` additions, `PushLayer`/`PopLayer`, lodash 3.10.1 built in. Every sample
  of JScript Panel 3.8.5 loads and paints.
- **Scripts reload when you save them**: a panel reloads by itself within a second when its
  script, or a file it `include()`s or `@import`s, changes on disk (*View › JS Panel › Reload
  scripts when their files change*, on by default).

### Added
- **Editor**: line numbers; a syntax error's line number turns red and **F8** jumps to it; a
  script that stopped on an error opens on the line that failed.
- **Samples** in the panel's menu: *Now playing*, *Album art*, *Playlist* (selection, keyboard,
  drag out and drop in) and *Seekbar and volume*, copied to `<profile>/foo_js_panel/samples/` to
  edit.
- **A script per panel on macOS**: `foo_js_panel script="~/Music/scripts/now_playing.js"` in the
  layout (a relative path starts in the profile folder).
- **Drag and drop**: `on_drag_enter` / `on_drag_over` / `on_drag_drop` / `on_drag_leave`,
  `fb.DoDragDrop`, `fb.CopyHandleListToClipboard`, `CheckClipboardContents`,
  `GetClipboardContents`. Tracks and files dropped on a panel go into the playlist the script
  picks.
- **Callbacks**: `on_selection_changed`, `on_always_on_top_changed`,
  `on_playlist_stop_after_current_changed`, `on_cursor_follow_playback_changed`,
  `on_playback_follow_cursor_changed`, `on_replaygain_mode_changed`, `on_output_device_changed`,
  `on_dsp_preset_changed`, `on_main_menu` (*View › JS Panel › Script commands*),
  `on_colours_changed`, `on_font_changed` (also when macOS switches light/dark).
- **`fb`**: `PlaybackTime` can be set to seek, `PlaybackFollowCursor`, `AlwaysOnTop` (floats the
  window on macOS, where foobar2000 has no such setting), `ReplaygainMode`, `GetOutputDevices`,
  `SetOutputDevice`, `GetDSPPresets`, `SetDSPPreset`, `ShowLibrarySearchUI`.
- **`plman`**: `GetPlaylistLockedActions`, `SetPlaylistLockedActions`, `GetPlaylistLockName`.
- **`utils`**: `GetSysColour`, `ColourPicker`, `DetectCharset`, `MapString`; `CheckFont` says
  whether a face draws as itself.
- **`window`**: `GetColourDUI/CUI` and `GetFontDUI/CUI` follow the host (Default UI on Windows,
  the system's colours under light or dark mode on macOS), `CreateTooltip()` is the system
  tooltip, `ShowProperties()` edits the panel's properties as JSON, and the header's `@name`,
  `@author` and `@version` fill `window.Name` and `window.ScriptInfo`.
- **`UIHacks.FrameStyle`** hides (2, 3) or shows (0, 1) the window's title bar under Panels UI.
- **Windows fonts on macOS**: Segoe UI and its weights, Tahoma, Verdana, Calibri, Bahnschrift,
  Consolas and Cascadia draw with the system fonts.

### Changed
- **GitHub release notes are this changelog's section** for the version, in full, instead of the
  commit titles.

## [1.2.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.1.0...v1.2.0) (2026-10-31)

### Highlights
- **Eole's layout options work**: the skin `tools/eole_skin.py` builds now follows Eole's own
  Columns UI layout, so ☰ › *Skin settings › Panel layout* (right playlist, left menu, playlist
  panel and lyrics widths, filters, bottom playlist, track info) changes the window as in Eole.
  The Playlists view gains Eole's three filters and playlist header, the control bar its small
  cover, and the mini player shows the now-playing playlist. Run step 3 of *Running the Eole
  theme* again to get it, then toggle ☰ › *Dark theme* off and on in each view.

### Added
- **`window.Name`**: the name given to `DefinePanel` (Eole tells its panels apart by it).
- **Shutdown on macOS**: `WScript.Shell.Run('shutdown.exe /s')`, `/r` and `/h` shut down, restart
  and sleep the Mac (Eole's scheduler: *Shutdown / Hibernate after current or playlist*).
- **Eole skin**: the Now playing view's lyrics are Panels UI's *Lyric Show* and the Visualization
  view is Panels UI's *Spectrum* (Eole's ESLyric and Shpeck don't exist on macOS).

### Fixed
- **Mini player button on macOS**: it switched to the mini layout and straight back. A window
  shrunk to its maximum size counted as maximized; now only a window that fills the screen does.
- **`window.IsVisible`** was always true; it now follows the host, which hides panels outside the
  current view. Eole's hidden playlist answered for the visible one and saved the full layout's
  height as the mini player's, so the mini player opened as tall as the screen.
- **Mini player size on Windows**: `UIHacks` size limits counted the window frame and the menu
  bar, so the mini player lost their height on every switch until it was one row tall. Limits
  now apply to the content area on both platforms.
- **Menus on macOS** show a Windows `&&` as `&` and drop `&` accelerators.

## [1.1.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.0.0...v1.1.0) (2026-10-31)

### Highlights
- **Eole runs on macOS and Windows**: the Eole theme (Columns UI + Spider Monkey Panel) becomes a
  Panels UI skin whose panels are JS Panels, with your Navidrome server as the library. The README's
  *Running the Eole theme* walks through it: get Eole, patch it, build the skin, pick it.
- **Most of the Spider Monkey Panel API**: properties, panel messages, popup menus with the
  context and main menus, input boxes, off-screen images and image effects, translucent drawing,
  the ActiveX objects Windows themes create, and many more `fb`, `plman`, `utils` and `window`
  members (the README's *What's implemented* has the full list).

### Added
- **Panel properties**: `window.GetProperty` / `SetProperty`, kept per script in
  `<profile>/foo_js_panel/properties/`.
- **Panel messages**: `window.NotifyOthers(name, info)` and `on_notify_data`; track handles and
  handle lists inside `info` arrive as real objects.
- **Popup menus**: `window.CreatePopupMenu()` (items, separators, checks, radio groups, submenus,
  `TrackPopupMenu`), `fb.CreateContextMenuManager()` and `fb.CreateMainMenuManager()` with
  `ExecuteByID`, on Windows and macOS.
- **Input and message boxes**: `utils.InputBox`, `WScript.Shell.Popup`, `utils.ShowHtmlDialog`
  for the confirm/alert dialogs themes build in HTML.
- **Keyboard and mouse**: `on_key_up`, `on_char`, `on_focus`, the middle button,
  `window.SetCursor`.
- **Off-screen images**: `gdi.CreateImage`, `GetGraphics` / `ReleaseGraphics`, `Clone`, `Resize`,
  `RotateFlip`, `ApplyMask`, `ApplyAlpha`, `InvertColours`, `StackBlur`, `GetColourScheme(JSON)`,
  `SaveAs`; `gdi.LoadImageAsync(V2)`, `utils.GetAlbumArtV2` / `GetAlbumArtAsyncV2`.
- **Drawing**: translucent shapes and `DrawString` honour the colour's alpha; anti-aliased
  strokes; `FillGradRect` at any angle with alpha and a focus point; `MeasureString`,
  `EstimateLineWrap`, `CalcTextHeight`.
- **ActiveX stand-ins** (both platforms): `Scripting.FileSystemObject`, `Microsoft.XMLHTTP` /
  `Msxml2.XMLHTTP` / `WinHttp.WinHttpRequest.5.1` through foobar2000's HTTP client,
  `WScript.Shell` (`Run`, `RegRead`, `Popup`), `htmlfile` (clipboard and a small HTML DOM for
  scrapers: `innerHTML`, `getElementsByTagName`, `className`, `innerText`, `href`, `src`),
  `UIHacks` (window state, full screen, minimum and maximum size, and a draggable pseudo caption
  for themes that hide the title bar), plus inert `ESLyric`, `Shell.Application` and
  `ScriptControl`.
- `WScript.Shell.Run` of a `cscript` download script (XMLHTTP + `SaveToFile`, as Biography's
  `foo_lastfm_img.vbs`) downloads the URL into the file on both platforms.
- **Globals**: `setTimeout` / `setInterval` / `clear*`, `new FbMetadbHandleList`,
  `new FbTitleFormat`, `new FbProfiler`, a small `Intl`; `include` resolves paths relative to the
  file that calls it.
- **Handles and playlists**: `FbMetadbHandleList[i]`, sorting, searching and set operations,
  `UpdateFileInfoFromJSON` (Navidrome tracks rate through foo_navidrome), the UI selection
  holder, and many more `plman` and `fb` members.
- **Ratings**: `%rating%` falls back to Navidrome's rating on server tracks.
- **`panelsui` object** (with Panels UI 1.19 or later): read and write skin variables and run
  skin actions, so a script can switch the skin's views.
- **Navidrome**: with foo_navidrome 1.38 or later, the server's tracks join
  `fb.GetLibraryItems()` (*View › JS Panel › Navidrome tracks in the library*, on by default),
  and `navidrome.GetArtistInfoAsync(name)` returns the biography and the artist's image.
- **Eole tools** in a source checkout: `tools/eole_patches.py` and `tools/eole_skin.py` (see the
  README), `make theme-check` to run a Columns UI layout's panels offline in a sandbox.

### Fixed
- `FillRoundRect` corners were twice too round and `FillEllipse` half size.
- `GdiDrawText` with `DT_CALCRECT` and `DT_BOTTOM` clipped the text to a box shorter than the
  line (Eole's controls bar cut the track title).
- `gdi.LoadImageAsync` handed the script back a path with `/` instead of the one it asked for,
  so themes that look images up by path (Eole's Biography photos) never showed them.
- `fb.GetLibraryRelativePath` uses `\` separators on macOS too, and Navidrome tracks get an
  *Album artist\Album\NN - Title* path, so folder views (Eole's Library Tree) group them;
  `OrderByPath` sorts them by that path.
- `InitNowPlaying` on a context menu manager with nothing playing returns an empty menu instead
  of stopping the script.
- The Windows unit tests ran again (three tests wrote Windows paths into JavaScript strings
  without escaping them).

## 1.0.0 (2026-10-27)

### Highlights
- **JS Panel**: JavaScript panels for foobar2000 v2 on Windows (x64, x86, ARM64) and macOS, on
  the QuickJS-ng engine, with a first slice of the Spider Monkey Panel API.

### Added
- The JS Panel layout element (Default UI on Windows, layout element on macOS), hostable by
  Panels UI as `$panel(name,JS Panel,…)`.
- Callbacks `on_paint`, `on_size`, mouse, wheel and playback; `gr` shapes, gradients and text;
  `gdi.Font`; `fb` playback state, control and `TitleFormat`; `window` size, repaint and timers;
  `include`, `console.log`.
- A built-in sample script; right-click › *Choose script file…* / *Reload script*.
- Script errors shown in the panel; runaway scripts stopped after 5 s.
- *View › JS Panel › Verbose logging* and *Copy diagnostics*; `foo_js_panel.log` in the profile.
- *Edit script…* in the panel menu: an editor window with live syntax checking, Apply / OK /
  Revert, on Windows and macOS.
- `navidrome` object (with foo_navidrome): albums and artists of your server, play or add them.
- Panels UI can pick each JS Panel's script (`panel.<name>.script`), and its layout wizard offers
  *Album browser + JS Panel*, *Playlist + JS Panel* and *Library + Navidrome (JS)* when JS Panel
  is installed.
- Keyboard: a panel takes the keyboard when clicked; `on_key_down(vkey)` and `utils.IsKeyPressed`;
  Enter plays the selection in the library and Navidrome panels.
- Album art (`utils.GetAlbumArtAsync`, `GdiBitmap`, `gr.DrawImage`, `gdi.Image`), track handles
  and lists, the Media Library (`fb.GetLibraryItems`, library change callbacks) and `plman`.
