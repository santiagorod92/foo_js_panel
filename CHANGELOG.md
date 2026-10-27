# Changelog

All notable changes to foo_js_panel (JS Panel). Versions are the `v*` git tags; the GitHub
release of each version has the downloadable `.fb2k-component`.

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
