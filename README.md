# JS Panel (foo_js_panel)

JavaScript panels for **foobar2000 v2** on **Windows (x64, x86, ARM64) and macOS** (universal).
A panel runs a script that draws itself and reacts to the mouse and to playback, with an API
modelled on **Spider Monkey Panel** (`on_paint(gr)`, `gr.FillSolidRect`, `fb.TitleFormat`,
`window.SetInterval`, …). The goal is for themes built on Spider Monkey Panel / JScript Panel,
which only ever ran on Windows, to run on macOS too.

The engine is [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (ES2023), built into the
component. Nothing else needs installing.

> **Status: early (phase 0).** The engine, the panel on both platforms and a first slice of
> the API work. Full themes such as Eole need much more of the API (see [Roadmap](#roadmap)).

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

## What's implemented

| Object | Members |
|---|---|
| callbacks | `on_paint(gr)`, `on_size(w, h)`, `on_key_down(vkey)` (Windows virtual-key codes on both platforms; return `true` when handled, other keys go on to foobar2000's shortcuts), `on_get_album_art_done(handle, art_id, image, path)`, `on_library_items_added / removed / changed(handles)`, `on_mouse_move / lbtn_down / lbtn_up / lbtn_dblclk / rbtn_down / rbtn_up (x, y, mask)` (return `true` from `on_mouse_rbtn_up` to skip the default menu), `on_mouse_leave()`, `on_mouse_wheel(step)`, `on_playback_starting(cmd, paused)`, `on_playback_new_track(handle)`, `on_playback_stop(reason)`, `on_playback_pause(state)`, `on_playback_time(time)`, `on_script_unload()` |
| `gr` | `FillSolidRect`, `DrawRect`, `DrawLine`, `FillRoundRect`, `FillEllipse`, `FillGradRect`, `DrawString` (StringFormat flags), `GdiDrawText` (`DT_*` flags), `CalcTextWidth`, `CalcTextHeight`, `DrawImage(img, dx, dy, dw, dh, sx, sy, sw, sh, angle, alpha)`; `SetSmoothingMode`, `SetTextRenderingHint`, `SetInterpolationMode` are accepted |
| `gdi` | `Font(name, pxSize, style)` → `Name`, `Size`, `Style`; `Image(path)` → `GdiBitmap` (`Width`, `Height`, `Dispose()`), relative to the script's folder |
| `utils` | `IsKeyPressed(vk)` (on macOS ⌘ counts as `VK_CONTROL`), `GetAlbumArtAsync(window.ID, handle, art_id)` (0 front, 1 back, 2 disc, 3 icon, 4 artist; the now-playing track's front cover comes from foobar2000's shared loader, so Navidrome tracks get theirs) |
| handles | `fb.GetNowPlaying()`, `fb.GetLibraryItems()`, `fb.CreateHandleList()`; `FbMetadbHandle`: `Path`, `RawPath`, `Compare(h)`; `FbMetadbHandleList`: `Count`, `Item(i)`, `Add`, `AddRange`, `Clone`, `Convert()` |
| `plman` | `PlaylistCount`, `ActivePlaylist` (get/set), `PlayingPlaylist`, `GetPlaylistName`, `FindOrCreatePlaylist(name, unlocked)`, `ClearPlaylist`, `InsertPlaylistItems(p, base, handles, select)`, `ExecutePlaylistDefaultAction(p, i)` |
| `fb` | `TitleFormat(expr).Eval()` (now playing, else the focused track), `.EvalWithMetadb(h)`, `.EvalWithMetadbs(list)` (an array), `IsPlaying`, `IsPaused`, `PlaybackTime`, `PlaybackLength`, `Play`, `Pause`, `PlayOrPause`, `Stop`, `Next`, `Prev`, `trace` |
| `window` | `ID`, `Width`, `Height`, `Repaint`, `RepaintRect`, `SetInterval`, `SetTimeout`, `ClearInterval`, `ClearTimeout` |
| global | `include(path)` (relative to the script's folder), `console.log`, Promises |
| `navidrome` | With [foo_navidrome](https://github.com/santiagorod92/foo_navidrome): `IsAvailable`, `IsConfigured`, `GetAlbumsAsync()` → `[{ Id, Name, Artist, ArtistId, CoverArtId, Year, SongCount, Created }]`, `GetArtistsAsync()` → `[{ Id, Name, AlbumCount, SongCount }]` (both Promises, rejected with the error when the server can't be reached), `PlayAlbum(id, replace = true, play = true)`, `AddAlbum(id)`, `PlayArtist(id, replace, play)`, `AddArtist(id)` |

Colours are ARGB numbers as in Spider Monkey Panel (`0xffRRGGBB`); fills honour alpha.

A script error stops the script and the panel shows the message and stack. A script that runs
for more than 5 s in one go is stopped, and one runtime is capped at 512 MB.

**Diagnostics:** *View › JS Panel › Verbose logging* and *Copy diagnostics*. The log is
`foo_js_panel.log` in the foobar2000 profile folder.

## Roadmap

1. **Phase 0 (this):** engine, panel on both platforms, first API slice, embedded in Panels UI.
2. **Phase 1, the Spider Monkey Panel core:** `FbMetadbHandle(List)`, `plman`, `GdiBitmap`
   and images, album art (async), menus (`window.CreatePopupMenu`, context and main menu
   managers), `window.GetProperty/SetProperty`, `utils`, the rest of the callbacks. Target: the
   Spider Monkey Panel samples run unchanged.
3. **Phase 2, themes:** stand-ins for the `ActiveXObject`s themes use (`Scripting.FileSystemObject`,
   `WScript.Shell`, `Microsoft.XMLHTTP`), font substitution on macOS. Target: Eole.
4. **Panels UI bridge:** JS reads and writes Panels UI variables and runs its actions, and skins
   send messages to scripts.
5. **More from foo_navidrome:** songs, search and covers need a new version of its library contract
   (today it publishes albums, plus playing/adding an album or an artist).
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
make win11-arm-smoke ARCH=arm64ec   # same in the Windows 11 ARM VM (also x64, x86)
```

## Credits

- [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (MIT), the JavaScript engine.
- The API follows [Spider Monkey Panel](https://github.com/TheQwertiest/foo_spider_monkey_panel)
  by TheQwertiest, itself after WSH Panel Mod and JScript Panel. No code is taken from them.

## License

MIT, see [LICENSE](LICENSE).
