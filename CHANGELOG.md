# Changelog

All notable changes to foo_js_panel (JS Panel). Versions are the `v*` git tags; the GitHub
release of each version has the downloadable `.fb2k-component`.

## [1.9.5](https://github.com/santiagorod92/foo_js_panel/compare/v1.9.4...v1.9.5) (2026-11-24)

### Fixed
- Scrolling with a touchpad or a smooth-scroll mouse wheel works on Windows: their small wheel
  movements were each rounded down to no step at all, so lists didn't move or stopped after a
  few lines (Playlist Organizer, [#5](https://github.com/santiagorod92/foo_js_panel/issues/5)).
  They now add up to whole steps, as a mouse wheel notch does. Windowless panels need Panels UI
  1.27.2 for the same fix.

## [1.9.4](https://github.com/santiagorod92/foo_js_panel/compare/v1.9.3...v1.9.4) (2026-11-24)

### Fixed
- foobar2000 no longer crashes on exit, on Windows, when a script left an image's `GetGraphics()`
  open: the drawing was written back after the image had been freed (Playlist Organizer).
- `img.ReleaseGraphics()` without an argument, as JScript Panel 3 scripts call it, releases the
  image's open `gr` and writes its drawing to the image right away.

## [1.9.3](https://github.com/santiagorod92/foo_js_panel/compare/v1.9.2...v1.9.3) (2026-11-23)

### Fixed
- JScript Panel 3 scripts that import `%fb2k_component_path%samples\js\lodash.min.js` get the
  built-in lodash 3, the version JScript Panel 3 shipped there, even when a lodash 4 copy sits in
  that folder: lodash 4 drops `_.forEach`'s `this` argument (Playlist Organizer's context menu
  stopped with "cannot read property 'AppendMenuItem' of undefined").

## [1.9.2](https://github.com/santiagorod92/foo_js_panel/compare/v1.9.1...v1.9.2) (2026-11-19)

### Fixed
- JScript Panel 3 scripts find API names whatever their case, as JScript Panel 3 did:
  `window.repaint()` and `gr.fillrectangle` work (Playlist Organizer stopped when releasing its
  scrollbar).

## [1.9.1](https://github.com/santiagorod92/foo_js_panel/compare/v1.9.0...v1.9.1) (2026-11-18)

### Fixed
- JScript Panel 3 scripts that track playlists by GUID load again: `plman.GetGUID(p)` and
  `plman.FindByGUID(guid)` are implemented (Playlist Organizer stopped at `plman.GetGUID`).

## [1.9.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.8.0...v1.9.0) (2026-11-16)

### Highlights
- **Real HTML dialogs**: `utils.ShowHtmlDialog` opens the theme's page in a real window, the
  Internet Explorer engine Spider Monkey Panel used on Windows and WebKit on macOS. Forms work now
  (Eole's input and radio dialogs used to be answered Cancel), and the page's buttons call back into
  the script.
- **Windowless panels take drag and drop**: with Panels UI 1.26.0, tracks and files dragged onto a
  `panel.<name>.windowless = 1` panel reach its script, `fb.DoDragDrop` starts drags from it, and
  its `SetPseudoCaption` area moves the window.

### Added
- `window.IsDark` for every script: foobar2000's dark mode (Georgia-ReBORN switches *View › Mode*
  with it).
- `utils.HTTPRequestAsync` for every script, with or without a window id, answered by
  `on_http_request_done` with the response headers.
- Response headers: `fetch`'s `Response.headers`, XMLHTTP's `getResponseHeader` and
  `getAllResponseHeaders` (the common ones: content type, length, dates, `etag`, caching,
  `location`, cookies, rate limits).
- `UIWizard` (Georgia-ReBORN's window control) moves, sizes, maximizes and snaps the main window
  instead of doing nothing: window state, position and size, grid positions, size limits, the
  caption area and full screen.
- JScript Panel 3: the playlist recycler (`RecyclerCount`, `GetRecyclerName`, `GetRecyclerItems`,
  `RecyclerRestore`, `RecyclerPurge`) and `fb.GetAudioChunk`, so VU meters move.
- macOS: Segoe Fluent Icons and Segoe MDL2 Assets icons (JScript Panel 3's playback, rating,
  check box and menu buttons) draw as symbols instead of boxes.

## [1.8.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.7.2...v1.8.0) (2026-11-16)

### Highlights
- **CaTRoX runs whole**: imported with Panels UI 1.25.0, the CaTRoX theme's 16 panels load and
  paint on macOS and Windows, menu bar and icons included.
- **Theme fonts without installing them**: the fonts in `<profile>/foo_js_panel/fonts/` are
  loaded for foobar2000 alone when a panel starts. Panels UI's *Import Columns UI layout…* copies a
  theme's `fonts/` folder there.

### Added
- `fb.IsLibraryEnabled()` (it only existed for JScript Panel 3 scripts, always true).
- macOS: Marlett, the Windows caption-button font, draws its minimize, maximize, restore, close,
  arrow and check glyphs as symbols, and `utils.CheckFont('Marlett')` is true.

### Fixed
- A playback callback set off by the script's own code (`fb.RunMainMenuCommand('Playback/Play')`
  while it loads, as CaTRoX's panels do) no longer runs in the middle of that code: it waits until
  the code returns, as Spider Monkey Panel's posted messages do. CaTRoX's seekbar failed on it.
- `gr.MeasureString` with a width of 0 measures without wrapping, as GDI+ does (it measured each
  letter on its own line, so CaTRoX's menu showed "Fil Ed Vi").
- Georgia-ReBORN on macOS: its cache and settings folders are made where it expects them (its file
  name cleaner turned the path's `/` into `_`).

## [1.7.2](https://github.com/santiagorod92/foo_js_panel/compare/v1.7.1...v1.7.2) (2026-11-15)

### Fixed
- **macOS: punctuation keys reach `on_key_down`**: the punctuation keys (minus, equals, brackets,
  backslash, semicolon, quote, comma, period, slash, backtick) and the keypad's operators now send
  their Windows codes (`VK_OEM_*`, `VK_ADD`…), so a script can handle them, and typing one into a
  script's text box no longer beeps.

## [1.7.1](https://github.com/santiagorod92/foo_js_panel/compare/v1.7.0...v1.7.1) (2026-11-15)

### Fixed
- **macOS: `on_focus(false)` when switching to another app**: a panel now loses focus when its
  window stops being the key window (another app, or another foobar2000 window) and gets it back
  on return, as on Windows. A script's text box no longer keeps its caret blinking while typing
  goes elsewhere.

## [1.7.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.6.1...v1.7.0) (2026-11-15)

### Highlights
- **Panels drawn straight into Panels UI**: a skin with `panel.<name>.windowless = 1` (Panels UI
  1.24.0) draws the JS Panel into its own window. The skin can draw over the panel, a transparent
  panel sees the live skin beneath it, and mouse, wheel, keys, cursor, tooltips, menus and timers
  work as usual. Drag and drop and `SetPseudoCaption` don't work in such a panel yet.

### Fixed
- `window.ScriptInfo.PackageId` names the package when a script `include`s a package's `main.js`
  (as the layouts Panels UI imports from Spider Monkey Panel 1.6 do), not only when `main.js` is
  the panel's script.

## [1.6.1](https://github.com/santiagorod92/foo_js_panel/compare/v1.6.0...v1.6.1) (2026-11-12)

### Fixed
- **macOS: scripts can tell ⌘ from ⌃**: `utils.IsKeyPressed` also answers `VK_LWIN`/`VK_RWIN`
  (91/92) for ⌘, which still counts as `VK_CONTROL`, so `IsKeyPressed(17) && !IsKeyPressed(91)`
  is ⌃ while Windows scripts keep taking ⌘C as Ctrl+C.
- **macOS: `on_key_up` for keys let go while ⌘ is held**: AppKit doesn't send those to views, so
  shortcuts like ⌘A used to leave the key stuck down for the script.

## [1.6.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.5.1...v1.6.0) (2026-11-10)

### Highlights
- **WSH Panel Mod scripts run**: scripts and themes made for WSH Panel Mod (the panel behind most
  Columns UI themes before Spider Monkey Panel) are recognised by their `@feature` header or the
  names only WSH Panel Mod had, and get its API: `CollectGarbage()`, `VBArray` and `.toArray()`,
  `Enumerator`, `GetColorCUI`/`GetColorDUI`, `utils.GetSysColor`, timer objects with `on_timer`,
  `WatchMetadb`, the playlist functions on `fb`, `gdi.CreateStyleTextRender()` and `@import`.
- **Copy diagnostics names what a script is missing**: each panel lists the API names its script
  used that JS Panel doesn't have (and logs each once as a warning), so a report about a script
  that doesn't work says exactly what's needed.

### Added
- `fb.AddFiles`, `AddDirectory`, `ClearPlaylist`, `LoadPlaylist`, `SavePlaylist`, `ShowConsole`,
  `ShowPreferences` and `GetSelectionType` for Spider Monkey Panel scripts too (they were only
  there for JScript Panel 3 scripts).
- `gr.GdiDrawText` returns the box it drew in and the characters drawn, as WSH Panel Mod did
  (scripts measure text with `DT_CALCRECT` this way).
- **Other authors' themes**: Georgia-ReBORN, CaTRoX and Wil-B's Library Tree and Biography load
  and paint. For them: `fb.IsMainMenuCommandChecked`, `plman.FindPlaylist`, fonts' `Height`,
  `FileSystemObject` files' `OpenAsTextStream`/`Copy`/`Move`/`Delete`, and stand-ins for
  Georgia-ReBORN's `AudioWizard` and `UIWizard`.

### Fixed
- **Centred and bottom-aligned text that wraps** (`gr.DrawString` with a vertical alignment) is
  placed in the middle or at the bottom of its box instead of at the top, on both platforms.
- **`include` works like Spider Monkey Panel's**: a relative path is found next to the file whose
  code calls it (a helper in another file that includes its neighbours works), then in the running
  package's `scripts` folder, and each file runs only once unless `{ always_evaluate: true }` is
  passed, so themes that include the same file from several places no longer stop with
  "redeclaration".
- **Text files saved as UTF-16** (with a byte order mark, as Windows tools often save them) are
  read correctly by `include`, `utils.ReadTextFile` and `FileSystemObject`.

## [1.5.1](https://github.com/santiagorod92/foo_js_panel/compare/v1.5.0...v1.5.1) (2026-11-09)

### Fixed
- **macOS: popup menus opened near the left or top edge of a screen** (like Eole's main menu
  button) no longer jump to the middle of the window when another display sits next to that edge.

## [1.5.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.4.0...v1.5.0) (2026-11-07)

### Highlights
- **See-through panels in Panels UI**: with `panel.<name>.transparent = 1` in the skin's
  `foo_ui_panels.ini`, a panel is drawn over the skin beneath it instead of its dark background,
  and `window.IsTransparent` is true.
- **Scripts follow the skin**: inside Panels UI, `window.GetColourDUI`/`GetColourCUI` and
  `GetFontDUI`/`GetFontCUI` return the skin's `color.*` and `font.*` settings first.

### Added
- **More of the `panelsui` object** (Panels UI 1.22 or later): `PanelName`, `GetPanelRect([name])`,
  `ShowPanel`, `TogglePanel`, `MovePanel`, `ResetPanel` (hide, show or move skin panels),
  `SetValue` / `GetValue` (values the skin reads with `$getvalue`, not saved) and `Eval(expr)`
  (titleformat run the way the skin runs it).
- **Callbacks** `on_panelsui_mini_mode(on)` and `on_panelsui_zoom(percent)`.

## [1.4.0](https://github.com/santiagorod92/foo_js_panel/compare/v1.3.0...v1.4.0) (2026-11-06)

### Highlights
- **Syntax colours in the editor**, on both platforms: keywords, strings, numbers, comments and
  regular expressions, next to the line numbers and the live syntax check. Pasting always pastes
  plain text.
- **Modern JavaScript where you want it**: `fetch()` (with `Response` and `Headers`), and ES
  modules: a script with `import`/`export` runs as a module, imports resolve next to the file that
  imports them and reload with it, and `export function on_paint(gr) {…}` is a callback.
- **Skins talk to scripts**: with Panels UI 1.21, a skin button's `JSPANEL:<name>[:<info>]`
  action reaches every panel as `on_notify_data(name, info)`, and every skin variable change as
  `on_panelsui_variable_changed(name, value)`.
- **Search your Navidrome server**: `navidrome.SearchAsync(query)` → `{ Artists, Albums, Tracks }`
  (foo_navidrome 1.40 or later).

### Added
- **More of Spider Monkey Panel 1.6**: the playback queue (`plman.GetPlaybackQueueContents`,
  `FindPlaybackQueueItemIndex`, `RemoveItemFromPlaybackQueue`, `RemoveItemsFromPlaybackQueue`,
  `on_playback_queue_changed`), `plman.AddLocations`, `on_mouse_wheel_h` (also a sideways
  trackpad swipe on macOS), `on_mouse_rbtn_dblclk`, `utils.ReadINI` / `WriteINI`, and packages
  (`utils.GetPackageInfo`, `GetPackagePath`, `window.ScriptInfo.PackageId`).
- **Copy diagnostics** lists each panel's memory and its five most expensive callbacks.
- Screenshots in the README, Eole's views among them.

### Fixed
- Eole's Biography showed `&#x27;` instead of apostrophes in last.fm biographies: the `htmlfile`
  stand-in now decodes character references in `innerHTML` as Internet Explorer did, and
  `outerHTML` includes the closing tag.

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
