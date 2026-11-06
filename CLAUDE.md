# foo_js_panel — JS Panel (foobar2000 v2)

JavaScript panels for **foobar2000 v2 — Windows x64 + ARM64EC + x86, macOS (universal)**, on
QuickJS-ng, with a Spider Monkey Panel compatible API. Born from foo_ui_panels issue #17 (Eole /
JS themes on macOS). Goal: SMP scripts and themes run unchanged on both platforms.

## Working rule: every change is cross-platform
Same rule as foo_ui_panels (its CLAUDE.md "Working rule"): every feature/fix lands on BOTH
platforms, all builds + CI legs green, release zip = `mac/` bundle + `x64/` + `arm64ec/` DLLs + the
x86 DLL at the zip root.
- The engine and the JS API are SDK-free C++ in `src/core` (`js_engine`, `js_api_util`,
  `sample_script`), unit-tested on a recording canvas. fb2k glue is platform-free in `src/core`
  (`panel`, `settings`, `diagnostics_collect`) and only includes `../fb2k.h`. Windowing is per
  platform in `src/platform/{win,mac}` behind `jsp::Surface`; `make check-portable` compiles
  `src/core` without Windows headers.
- **Every change is verified on both platforms before it ships:** (1) `make test` +
  `make check-portable`, new API covered by a test in `tests/test_js_engine.cpp`; (2) `make
  build-all`; (3) runtime: `make mac-vm-smoke` (the terminal-mac VM; `make mac-smoke` = this Mac's own
  foobar2000) and `make win11-arm-smoke
  ARCH=arm64ec|x64|x86` (Windows 11 ARM VM), or Wine (`make run`) on Linux. Exercise the changed
  API from a script, not just "it loads". Anything that couldn't be checked goes in the PR.

## Shared conventions (foo_ui_panels ⇄ foo_navidrome ⇄ foo_js_panel)

foo_ui_panels (Panels UI), foo_navidrome and foo_js_panel (JS Panel) are sister components,
sibling checkouts under one parent (`../foo_ui_panels`, `../foo_navidrome`, `../foo_js_panel`).
They're developed together, installed together and tested in the same foobar2000. **This section
is identical in all three repos' CLAUDE.md** and holds only what's true for more than one;
anything repo-specific stays in the repo's own sections.
- **Editing it:** change all three copies in the same round, keep the subsection order below, one
  terse bullet per fact (`**Topic:** fact`), no narrative or session logs. Check they match:
  `for r in foo_navidrome foo_js_panel; do diff <(awk '/^## Shared conventions/{f=1} /^## /&&!/^## Shared/{f=0} f' ../foo_ui_panels/CLAUDE.md) <(awk '/^## Shared conventions/{f=1} /^## /&&!/^## Shared/{f=0} f' ../$r/CLAUDE.md); done`.
- **What goes here:** a rule, tool, layout or gotcha that's useful in more than one repo (SDK,
  build layout, versioning, logging, release, test environments, troubleshooting). If it's useful
  in only one repo, it goes in that repo's own sections.

### How they work together
- **Contracts:** foo_navidrome publishes `service_base` interfaces, and foo_ui_panels vendors a
  GUID-matched copy of the contract header and finds it with `service_enum_t` at runtime. There's
  no build coupling, and the feature is a no-op when the other component is missing. foo_navidrome's
  "Cross-compatibility with other same-author components" section is the full list: each
  interface, its consumers and its vendored copies.
- **Current links:** `navidrome_rating_api` drives Panels UI's star ratings on `navidrome://`
  tracks, which also read the `NAVIDROME_RATING` field. `navidrome_library_api` feeds the
  Panels UI album grid (`album_list`) and `library_tree`, and JS Panel's `navidrome` script
  object. `navidrome_lyrics_api` feeds Panels UI's `lyrics_panel`.
- **Shared source:** foo_js_panel compiles foo_ui_panels' `src/gfx/canvas.h` and its backends
  (`src/platform/win/gdi_canvas.*`, `src/platform/mac/mac_canvas.*`) from the sibling checkout
  (`PANELS_UI_SRC`); its CI checks foo_ui_panels out next to it. A change to those files is
  built in both repos in the same round.
- **Hosting:** Panels UI hosts JS Panel like any foreign UI element, by name
  (`$panel(<name>,JS Panel,…)`), and hands it the skin's `panel.<name>.script` through the
  `js_panel_api.h` contract (foo_js_panel's original, vendored byte-identical in foo_ui_panels):
  Windows = the element's `ui_element_config` data (UTF-8 path), macOS = the `instantiate()`
  dictionary key `script`. The wizard's `albums-js` layout needs it.
- **Skin → script:** the same header carries `js_panel_notify_api`, published by JS Panel: Panels
  UI calls `notify(name, info)` for a `JSPANEL:<name>[:<info>]` action and
  `variable_changed(name, value)` from `save_pvars` for each changed skin variable (not the `_`
  ones); JS Panel hands them to every panel as `on_notify_data` / `on_panelsui_variable_changed`.
- **Changing a contract:** a change to a contract header (GUID, vtable order, sink signatures)
  lands in both repos in the same round, and the vendored copy stays byte-identical to the
  original. A new interface also gets a bullet in foo_navidrome's cross-compatibility list.
- **Runtime check:** test a feature that crosses the boundary with both DLLs installed (Wine,
  macOS VM). Also check that each side still works with the other component missing:
  `ui-test.sh <platform> solo` in either repo moves the other component out of the profile for
  the run (restored after) and runs that repo's smoke steps.

### SDK
- **Source:** `reupen/foobar2000-sdk-unmodified`, tags `SDK-YYYY-MM-DD`. Don't use the
  `marc2k3` or `razielanarki` mirrors: they're stale and missing `helpers-mac/`.
- **Location:** one checkout, shared by all three repos: `../foobar2000/{SDK,helpers,helpers-mac,
  shared,foobar2000_component_client}` + `../pfc`. A bump therefore affects every repo at once. CI
  re-clones upstream's default branch on every run.
- **In use: SDK-2026-10-01.** foobar2000 v2.26; C++20 is required (since 2026-09-16).
- **Bump procedure:**
  1. Clone the tag into a scratch dir.
  2. Run `diff -rq` against the sibling SDK folders.
  3. Read foobar2000.org/changelog-sdk.
  4. `git grep` every repo for every symbol the diff touches.
  5. Copy the changed files into the siblings.
  6. Build every leg of every repo and run every test suite.
  7. Update the version in "In use" above (all copies) and in each README's development table.
- **Sync history:**
  - 2026-09-16: C++20 is required. pfc now redirects to `shared.dll` through
    `wrap_pfc_hooks.h`, and the ARM64EC link needed the hooks. `skipTrack.h` was added.
  - 2026-10-01: pfc's ARM64EC `/alternatename` is fixed (`#` names). `now_playing_album_art_notify`
    may now pass null data.
- **Upstream breakage:** a sync can break a build with no change on our side (a renamed
  target, a new required integration step). Diff the live upstream files and read the SDK
  changelog before debugging our code.

### Build, version, release
- **Toolchain:** local Windows builds are done on Linux with clang-cl + lld-link + xwin (splat
  `--arch x86,x86_64,aarch64`), static `/MT` CRT, x64 + x86. Release builds also include ARM64EC:
  foo_ui_panels and foo_js_panel build it with clang-cl, foo_navidrome with MSBuild in CI. macOS ships as a
  universal bundle, and the x86_64 slice must stay (the VM guest is Intel).
- **Version:** `scripts/version.sh` is the same resolver in every repo: env override
  (`PUI_VERSION` / `NAVIDROME_VERSION` / `JSP_VERSION`) → `git describe` against `v*` → `version.txt`
  (`export-subst`). No version is kept by hand. CI checks out with `fetch-depth: 0`. Run
  `git fetch --tags` after each release.
- **Release:** semantic-release with Conventional Commits (`feat` = minor;
  `fix`/`perf`/`refactor` = patch; `chore`/`docs`/`ci`/`style`/`test` = no release). Releases
  are pushed straight to `main` (the owner bypasses the PR rule) and nothing is committed back.
  `notify-n8n` publishes to foobar2000.org. If the head commit is non-releasing, dispatch the
  workflow by hand.
- **Changelog = release notes:** `CHANGELOG.md` is kept by hand: the release commit adds
  `## [X.Y.Z](compare link) (date)` with `### Highlights`/`Added`/`Fixed`/`Changed`, written for
  users. `scripts/release-notes.sh X.Y.Z` (same file in every repo) prints that section with
  wrapped lines joined; it is the GitHub release body (`generateNotesCmd`) and therefore what
  foobar2000.org gets. `verifyReleaseCmd` runs it first, so a release whose version has no
  section fails before tagging: predict the next version from the commits and write it.
- **Release asset:** one `<component>_<version>.fb2k-component` per release, labelled
  `<component> <version> (macOS + Windows x86/x64/ARM64)`. Every place that uploads it
  (`.releaserc.json`, any `gh release upload --clobber` re-upload) uses that same label.
- **Commits:** author Santiago Rodriguez only, no AI trailers. Push only at the end of an
  iteration.
- **CI path filters:** files that aren't part of the component don't trigger CI builds:
  `**.md`, issue forms, `LICENSE`, `.gitignore`, and local-only dev/test scripts and tools. The
  `paths-ignore` lists live in foo_ui_panels' and foo_js_panel's `build.yml` (push + PR; a new
  PR push cancels the superseded run) and in foo_navidrome's `release.yml` (push). A new
  local-only script or tool is added to that list in the same change. Anything CI builds, tests or packages with stays off
  it. A release-worthy commit that only touches ignored paths doesn't release; dispatch the
  workflow by hand.

### Code style and working rules
- **Cross-platform:** every feature or fix lands on Windows and macOS. Logic goes in shared,
  SDK-free code covered by unit tests; only the wiring is per-platform.
- **No code comments:** none in `src/`, `tests/`, `scripts/`, `tools/`, the `Makefile` or the
  workflows. The *why* goes in CLAUDE.md or the commit message.
- **README:** every user-visible change updates the README in the same change.
- **Make targets:** a command run more than once, or with easy-to-forget flags, becomes a
  `make` target.

### Logging and diagnostics
- **Log file:** `<profile>/<component>.log` (`foo_ui_panels.log`, `foo_navidrome.log`,
  `foo_js_panel.log`). Shape:
  - WARN+ by default; INFO when the component's verbose toggle is on.
  - Rotates to `.1` past 2 MB.
  - Each session opens with an always-written NOTE banner (component, foobar2000, OS, arch,
    Wine versions).
  - Lines from before the log path is known are buffered.
- **Copy Diagnostics:** an in-memory ring of recent lines feeds the Copy Diagnostics report in
  Preferences (JS Panel: *View › JS Panel*), on both platforms. The text is pasted into public issues, so it is always
  redacted: home → `~`, user name, server host, auth/URLs (`scrubAuth`). Issue forms live in
  `.github/ISSUE_TEMPLATE/`.
- **Keep the logging matched:** a new logging or diagnostics feature (level, field, redaction,
  rotation) is mirrored in the other repos unless there's a stated reason not to.

### Test environments
- **Wine:** `~/.foobar2000` (fb2k v2.26 x64), with the components in
  `profile/user-components-x64/<name>/`. Each repo's `win-build`/`deploy` installs its own DLL,
  and the others stay as last installed. `tools/wclick.c` (originally from foo_ui_panels) posts
  clicks and keys without touching the host pointer.
- **macOS VM:** `../macos-devbox` (`mvm`, dockur/macos, Intel guest). **Windows 11 VM:**
  `../windows-devbox` (`wvm`, dockur/windows), for Dark Mode, DPI and native theming that Wine
  fakes. Never `docker prune`/`rm`/`rmi`.
- **macOS VM on a Mac (terminal-mac):** foo_navidrome's `scripts/terminal-mac.sh` over
  github.com/govijr/terminal-mac (VM `default`, latest macOS, user `dev`). Files, config and
  logs go over ssh (Remote Login on in the guest, key put there by `provision`); screenshots
  and input go through terminal-mac's guest helper, in screen points. It has `mvm`'s commands, so
  on a Mac `MVM` and `ui-test.sh mac` (foo_ui_panels, foo_navidrome) and foo_js_panel's
  `ui-test.sh mac-vm` use it, and this Mac's own foobar2000 and desktop stay untouched.
  `provision` also makes Panels UI the UI module and puts two silent tracks in the default
  playlist; `seed` copies foo_navidrome's settings. Its drag is one atomic action, too quick for
  an `NSDraggingSession`. "No guest display is available" = the guest's display slept, which
  also locks the session: `provision` sets `pmset displaysleep 0 sleep 0` so it doesn't, and a
  locked session is unlocked by hand from `terminal-mac open`/`view`. Its menu bar ignores a
  click right after foobar2000 launches, so the mac drivers wait for `lsappinfo front` and retry.
- **Windows 11 ARM VM (Apple Silicon Mac):** foo_navidrome's `scripts/win11-arm.sh` (QEMU +
  HVF, launchd jobs). Every repo drives it with the same `make win11-arm*` targets; foo_ui_panels
  and foo_js_panel point at `../foo_navidrome/scripts/win11-arm.sh` (`WIN11_ARM=`). `deploy <dll>` installs into
  `user-components-arm64ec/<dll name>`, `push` copies a folder in, `COMPONENT=` picks the log
  `fb2k log` reads, `make win11-arm-test` runs each repo's unit tests in the guest. Windows
  ignores the QEMU tablet, so `click [X Y]` and `tap 'X,Y;wait,MS;key,VK'` click from inside
  the desktop session (scheduled task + `SetCursorPos`/`mouse_event`); `tap` runs a whole
  sequence in one go so a popup menu stays open for the next click.
- **All three Windows builds run in that one VM:** ARM64EC natively, x64 in the same ARM
  foobar2000 (an ARM64EC process loads x64 DLLs), x86 in a portable 32-bit foobar2000 at
  `C:\fb2k-x86` (`FB2K_ARCH=x86`, foo_ui_panels and foo_js_panel: `ARCH=x86`; `provision` installs it once and
  seeds its profile from the ARM one). `fb2k stop` closes whichever build is running.
- **UI tests:** every repo drives the real foobar2000 with one script,
  `scripts/ui-test.sh <platform> <scenario>` (foo_ui_panels and foo_navidrome: `wine|win11|mac`;
  foo_js_panel so far: `mac` = this Mac's foobar2000, `mac-vm`, `win11-arm`): each scenario is written once over a small
  per-platform driver and asserts on the component log, the process and crash reports.
  `ui-test.sh compare <scenario> [platforms]` prints a step matrix that marks where platforms
  differ (`PARALLEL=1` runs them at once; one test agent per platform works the same way).
  Run Windows changes on Wine *and* the Win11 VM: Wine is an emulation and real Windows
  differs (dark-mode theming, popups, focus). Check the screenshots, not only the log.

### Troubleshooting
- **DLL fails to load** ("No access to memory location"; fb2k falls back to the Default UI):
  usually a foobar2000 object created at static init. Create it lazily.
- **Is the DLL loaded under Wine?** `grep -i foo_ /proc/$(pgrep -f 'foobar2000[.]exe')/maps`.
  Check the component log for a new session banner.
- **`pkill -f`/`pgrep -f`:** a pattern that also appears in your own command line kills your
  shell. Use `'foobar2000[.]exe'`.
- **No debugger:** logging is the diagnostic channel. Add log lines at decision points instead
  of guessing.
- **Build breaks right after an SDK sync:** see SDK › "Upstream breakage" before touching our
  code.

## Key decisions
- **Engine: QuickJS-ng**, vendored in `third_party/quickjs` (version in `VERSION.txt`, upstream
  files unmodified). Plain C, builds with clang-cl for every Windows arch and with Apple clang;
  SpiderMonkey/V8 don't fit the cross toolchain and JavaScriptCore is mac-only. No JIT: fine for
  SMP-era scripts.
- **API target: Spider Monkey Panel 1.6** (names, argument order, ARGB colours, `DT_*` and packed
  StringFormat flags, pixel font sizes). JScript Panel 3 may come later as a shim over it.
- **A layout element, not part of Panels UI:** `ui_element` (DUI) on Windows, `ui_element_mac` on
  macOS, both named "JS Panel" with GUID `5EB08677-…`. Works in Default UI, Columns UI (DUI
  wrapper) and Panels UI. Never depend on Panels UI at runtime.
- **Drawing is Panels UI's `gfx::Canvas`**, compiled from `../foo_ui_panels/src` (never copied):
  `gdi_canvas.cpp` on Windows, `mac_canvas.mm` on macOS. Engine-side needs that the canvas lacks
  get a new `Canvas` method in foo_ui_panels (both backends) in the same round.
- **Script choice:** Windows keeps it in the element's config (per instance, in the layout); macOS
  layout elements have no config, so the global default script (`cfg_var.4876F6EE-…`) is used.
  A host hands a panel its own script through `src/core/js_panel_api.h` (the original of the
  contract Panels UI vendors, see Shared conventions › Hosting): config data on Windows, the
  `script` dictionary key on macOS; such a script is never written back as the default.
- **Panel properties:** `window.GetProperty/SetProperty` live in
  `<profile>/foo_js_panel/properties/<FNV-1a of the script path>.json` (`sample.json` for the
  built-in one), written once after a callback that changed them. Panels running the same script
  share them; a converted theme gives each panel its own script file.
- **Deferred dispatch:** `NotifyOthers` (info cloned as JSON), the playlist/metadb callbacks and
  `window.Reload`/`fb.Exit` go through `Panel::later` (`fb2k::inMainThread` + the panel's abort),
  like SMP's posted messages: scripts may modify playlists from them and never destroy the engine
  they're running in. Playback callbacks stay synchronous.
- **Menus:** `MenuObject` keeps SMP's mutable tree (`AppendTo` links the child, so later edits
  show) and `flatten`s it to a `jsp::Menu` value tree for `Surface::popup_menu` (depth-capped, a
  menu appended to itself is fine). The managers are host `MenuSource`s: context = the
  `contextmenu_manager` node tree, main = `mainmenu_manager_v2::generate_menu()` (portable since
  fb2k 2.0). Ids are `base + n` in build order; `ExecuteByID(n)` runs the n-th command.
- **Modal calls:** `TrackPopupMenu` and `InputBox` run through `Impl::modal` (the time spent
  doesn't count against the 5 s budget; `Budget` is RAII so nested callbacks in the modal loop keep
  the outer deadline) and `Panel::reload` waits while `in_modal()`.
- **No `SetForegroundWindow` before `TrackPopupMenu`:** on a window that isn't foreground it
  hides the menu and stops the panels painting. A right-click already makes the window
  foreground; the VM UI test clicks first (`win11-arm.sh move` + `click`) for the same reason.
- **Off-screen images:** pixels are `pui::gfx::Pixels` (premultiplied `0xAARRGGBB`), with
  `image_pixels` / `image_from_pixels` / `pixels_canvas(px, scale)` in Panels UI's canvas
  backends. GDI can't write alpha, so the Windows pixel canvas draws every call on a copy over
  black and one over white and recovers alpha from the difference; CoreGraphics draws straight
  into the buffer. A `GdiBitmap` holds a drawable image and/or pixels (`pixels_of` reads them on
  first use, `drawable` rebuilds the image after an edit); the pixel operations are SDK-free in
  `image_ops.cpp`. Each `gr` carries its canvas (`Graphics`): `on_paint`'s is cleared after the
  call, `GetGraphics`' writes back on `ReleaseGraphics` or when collected.
- **Alpha:** `Canvas` only has alpha for rectangles and images, so `with_alpha` draws a
  translucent shape or `DrawString` opaque into a box-sized pixel canvas (at the device scale)
  and composites it with the colour's alpha. `GdiDrawText` stays opaque, as in SMP.
- **Arcs:** SMP's `FillRoundRect` arcs are radii; `Canvas::fill_round_rect` takes the corner
  ellipse's size (GDI `RoundRect`), so the engine passes `2 * arc` and `FillEllipse` the full size.
- **ActiveX stand-ins:** `ActiveXObject` is JS in `kPrelude`, dispatching on the ProgID to
  stand-ins over the hidden native `__jsp` object (disk via `std::filesystem` straight, not through
  the host, so FSO and `stat`/`list` agree; HTTP, shell, clipboard and message box through the
  host). HTTP is the SDK's `http_client` (`Panel::run_http`, a detached thread + `inMainThread` for
  async); `WScript.Shell.Run` is `ui::shell_run` (`ShellExecuteW` / `NSWorkspace`; Windows-only
  commands do nothing on macOS), except `cscript` of a download VBS (XMLHTTP + `SaveToFile`), which
  becomes `__jsp.download`. `htmlfile` is a small DOM: elements keep offsets into the parsed
  source, so `innerHTML` is the original markup, not a re-serialisation, except that numeric character
  references are decoded as IE's serialiser does (not `&<>"` or NBSP): Eole's Biography reads
  last.fm's `innerHTML` and only decodes `&#39;`, while last.fm now writes `&#x27;`. `SetPseudoCaption` is a
  rect in main-window client coordinates; a left-down there drags the window
  (`WM_NCLBUTTONDOWN HTCAPTION` / `performWindowDragWithEvent`) instead of reaching the script.
  Unknown ProgIDs throw, as in JScript.
- **`UIHacks.MainWindowState` on macOS:** 2 (maximized) only when the window is zoomed *and* fills
  the screen's visible frame. `NSWindow.zoomed` alone is also true for a window shrunk to its
  `contentMaxSize` (the standard frame is capped by it), and Eole's `on_size` would read that as
  maximized and leave mini mode right after entering it.
- **`UIHacks.MinSize` / `MaxSize`** clamp the main window's content area (macOS `contentRect`,
  Windows client rect, so without the frame and Panels UI's menu bar): Eole computes them from
  its own panel heights and saves the mini playlist height back, so a frame counted in would
  shrink it on every round trip.
- **Eole without tools:** Panels UI's *Import Columns UI layout…* (`SKIN:IMPORT_FCL`) builds the
  skin and copies `themes/` + `wsh-data/` into the profile; JS Panel adapts Eole's files as it
  evaluates them (`patch_theme_source` in `theme_patches.cpp`: only paths under `/eole/`, each
  patch only when its anchor is found exactly once and its text isn't there yet, nothing written to
  disk). Eole branches on `window.Name`, so panels keep their FCL names. `UIHacks.FrameStyle` ≥ 2
  runs `WINDOWSTYLE:hidetitlebar` through `panelsui`.
- **`window.IsVisible`** is the host window's visibility: Panels UI hides panels outside the
  current view without destroying them, and Eole only lets the visible one answer notifications.
- **Theme checks run third-party code:** `make theme-check` executes a theme's scripts, so
  `render_script --sandbox PROFILE` refuses every write, move or delete outside the theme's
  profile (`JsHost::may_write`, checked by every FSO/`utils` write) and the renderer has no shell,
  network or clipboard. Still run it only on a theme the user asked for, never from CI.
- **HTML dialogs:** `utils.ShowHtmlDialog` has no HTML engine on either platform; it reads the
  dialog kind from the HTML's element ids (`btn_cancel`, `btn_later`, `input_container`,
  `radio_container`, as Eole's dialogs use) and answers through `message_box`, or Cancel for forms.
- **Windows paths in scripts:** `fix_path` turns `\` into `/` off Windows for `include`,
  `gdi.Image` and the `utils` file functions (themes build paths as `fb.ProfilePath + 'a\\b.js'`).
- **Edit script:** `Surface::open_editor` (one editor window per panel, closed with it; Windows
  `win_editor.cpp` = RichEdit (see Editor colours), macOS `NSTextView` in `mac_element.mm`). Applying writes
  the file and reloads; the built-in sample is saved as `<profile>/foo_js_panel/script[-N].js`.
  The syntax check is `check_syntax()` (QuickJS compile-only, own small runtime).
- **foo_navidrome:** `navidrome` script object over the vendored `navidrome_library_api.h`
  (identical to Panels UI's copy). `list_albums[_v2]` blocks, so `Panel::navidrome_albums` runs it on
  a detached thread with the panel's `abort_callback` and hands the result back with
  `fb2k::inMainThread`; the engine resolves the Promise only while it's alive (`Impl::alive`).
  Artists are derived from the albums (`library_artists`). v3 adds `list_tracks` (merged into
  `fb.GetLibraryItems()`) and `artist_info` (`GetArtistInfoAsync`; `theme_patches.cpp` makes
  Eole's Biography fetch artist photos from it, since last.fm's photo pages now answer non-browser
  clients with a JS challenge).
- **SDK exceptions vs. hosts:** an SDK call that throws (e.g. `config_object::g_get_data_bool_simple`
  throws `exception_service_not_found` for an object that doesn't exist, like
  `bool_ui_always_on_top` on macOS) must not escape `Panel`'s constructor: Panels UI swallows the
  exception from `instantiate()` and the half-built `Panel` stays in `live()`, so the next
  broadcast crashes. Host methods look services up with `g_find`/`tryGet`, and `Panel::reload`
  catches what still gets through and logs it as an ERROR.
- **State callbacks:** config flags (`config_object_notify`), DSP (`dsp_config_callback`),
  ReplayGain (`replaygain_manager_v2::add_notify`) and output (`output_manager_v2::addCallback`)
  are global services that `Panel::broadcast` to every live panel through `later`.
  `fb.AlwaysOnTop` falls back to `Surface::set_always_on_top` (NSWindow level) where foobar2000
  has no such setting, and broadcasts the callback itself. `on_main_menu(n)` comes from the
  *View › JS Panel › Script commands* group (10 commands, as SMP).
- **Playlist locks:** `SetPlaylistLockedActions` installs our own `playlist_lock` (one per
  playlist, kept in `js_locks()`, index tracked by `on_playlist_index_change`); a playlist locked
  by another component is refused. Locks aren't persisted: scripts reapply them on load, as in SMP.
- **Drag and drop:** the engine passes `DropTargetAction` as a plain JS object and reads its fields
  back; the platform asks the panel at enter/over/drop and inserts on a drop with an effect and a
  playlist through `Panel::drop_notify` (abort-guarded). Windows: `IDropTarget` per HWND +
  `ole_interaction` data objects for `DoDragDrop` and the clipboard (`win_dragdrop.cpp`). macOS
  has no SDK pasteboard format, so the view writes our own type (`subsong\tpath` lines, any scheme,
  so `navidrome://` survives) plus file URLs, and reads ours back with `metadb::handle_create`
  (the SDK's synchronous `process_locations` crashes on macOS); Finder files go through
  `process_locations_async`. `NSDraggingSession` is asynchronous, so `DoDragDrop` returns 0 there.
  `make win11-arm-dragdrop` drives a real drag (`win11-arm.sh tap drag,…`); `make mac-dragdrop`
  does the same with `mac-click.sh … drag` and moves this Mac's pointer.
- **UI colours and fonts:** the engine maps DUI and CUI kinds to `UiColour` (CUI kinds go to their DUI
  equivalents: there's no Columns UI SDK here) and keeps a dark table as the fallback. Windows asks
  the element's `ui_element_instance_callback` (`query_std_color`, `getSysColor`, `query_font_ex`);
  macOS resolves semantic `NSColor`s under the view's effective appearance and answers "Segoe UI"
  (the canvas maps it to the system font). `ui_config_callback_impl` sends
  `on_colours_changed`/`on_font_changed`; macOS also sends the first on an appearance change.
- **JScript Panel 3 layer:** `is_jsp3_script` (a `==PREPROCESSOR==` import of `lodash` or
  `%fb2k_component_path%`, or a JSP3-only call) makes `JsEngine::load` eval `kJsp3Prelude`, then the
  `@import`s, then the script. The prelude adds names to the class prototypes (`__jsp.proto`) and
  replaces the few that clash with SMP (`FillEllipse`/`DrawEllipse` take centre + radii,
  `GetFontDUI/CUI` return JSON strings, `InputBox`/`ColourPicker` have no window id), so SMP scripts
  never see it. lodash is vendored and embedded at configure time (`cmake/embed_lodash.cmake` →
  `lodash_js()`, handed out by `JsHost::bundled_import`). `%fb2k_component_path%` is
  `<profile>/foo_js_panel/jsp3/`; JSP3's own helpers and samples (marc2003's) are never shipped.
  lodash is **3.10.1**, not 4: JSP3 scripts rely on 3.x (`forEach(list, fn, thisArg)`,
  `sortByOrder`); `kLodashMixins` adds JSP3's `nest`. `PushLayer`/`PopLayer` are
  `Canvas::push_clip`/`pop_clip` (shared canvas, both backends; GDI's `RestoreDC` also drops the
  selected font, so `pop_clip` re-selects it), and clips left open are popped after `on_paint`.
  `make jsp3-check JSP3=<unzipped component>` runs every JSP3 sample in `render_script --sandbox`
  (37/37 for 3.8.5).
- **Columns UI layout converter:** lives in Panels UI (`src/core/fcl_import.cpp`, its CLAUDE.md
  has the `.fcl` and Panel Stack Splitter formats). JS Panel's side is what the converted scripts
  call: `RefreshPSS()` / the "Panel Stack Splitter" main-menu command toggle the `pss.refresh`
  pvar through `panelsui`. Panels UI defers `SKIN:`/`WIZARD:`/`MENU` actions asked through the
  contract, so a script that switches skin is never destroyed inside its own callback.
- **Hot reload:** the engine records every file it evaluates (`source_files()`: the script, its
  `include()`s, its `@import`s, its ES `import`s); `Panel::watch_files` stamps their mtimes after each load and polls them
  on a `Surface` timer with an id no JS timer reaches (`kWatchTimer`), reloading on a change unless a
  modal call is running. Off with the *Reload scripts when their files change* toggle.
- **ES modules:** a script is a module when a line starts with an `import`/`export` statement
  (`is_module_source`; QuickJS-ng's `JS_DetectModule` says yes to almost any script that also parses as
  a module, so it isn't used). `module_normalize` resolves relative to the importing file, `module_load`
  reads through the host and records the file for hot reload; the module's exports are copied onto the
  global object so `export function on_*` are callbacks. QuickJS-ng resolves imports already at
  compile time, so `check_syntax` ignores "could not load module".
- **`fetch`:** JS in `kPrelude` over `native.httpAsync` (the XMLHTTP stand-in's path); the host gives no
  response headers, so `Response.headers` is empty.
- **Profiling:** `Impl::Timed` wraps every `call()` and timer (keyed by callback name, timers as
  `timers`), minus `modalUs` (time inside `Impl::modal`); `Panel::info()` hands it with the memory to
  Copy Diagnostics (top five per panel).
- **Editor colours:** `highlight_js` (`js_highlight.cpp`, SDK-free, tested) returns spans in UTF-16
  units. macOS paints them as `NSLayoutManager` temporary attributes (not in the undo stack); Windows'
  editor is RichEdit (`Msftedit.dll`) and paints through TOM `ITextDocument` with `tomSuspend` around
  it, only the visible lines ±40 (repainted on scroll), so big themes stay fast. RichEdit counts a line
  break as one character (CR), so the text is read with `EM_GETTEXTEX GT_DEFAULT` and CR → LF;
  Ctrl+V pastes plain text and its formatting shortcuts are swallowed.
- **Hardening:** 5 s budget per call (interrupt handler), 512 MB runtime memory, 1 MB stack. A
  script error stops the script (timers killed) and the panel paints the error + stack, like SMP.
- Drawing onto the Panels UI canvas is a planned contract; it follows the `service_base` contract
  pattern in Shared conventions (the variables/actions/notify bridge is done, see Hosting).

## Release publishing
- `release.yml`'s `notify-n8n` posts each release to the n8n workflow
  `infra-foundations/n8n/foo_js_panel/` (webhook `/webhook/foo-js-panel-release`), which uploads it
  to foobar2000.org component **359**. The id lives only in that workflow (one workflow per
  component, so a release can't land on another component). Needs the repo variable
  `FOOBAR_ORG_PUBLISH=true`, the secrets `N8N_FOOBAR_RELEASE_WEBHOOK_URL` (that webhook's LAN URL)
  and `N8N_FOOBAR_RELEASE_WEBHOOK_SECRET` (the shared one), and the homelab runner
  `github-runner-js-panel` (infra-foundations `github_runner_instances`).

## Repo conventions
- Remote: `origin` → github.com/santiagorod92/foo_js_panel. Branch `main`.
- **Commits: author Santiago Rodriguez <santiagom9992@gmail.com> ONLY — no Co-Authored-By trailer.**
- **No code comments** anywhere in `src/`, `tests/`, `scripts/`, `cmake/`, `CMakeLists.txt`,
  `build.sh`, the `Makefile` and `.github/` (same rule as the sister repos). JS inside raw strings
  (`sample_script.cpp`) is data.
- **Every user-visible change updates `README.md`** ("What's implemented" API table, Roadmap).
- **This file is for stable rules/decisions only**, no session logs.
- Never commit build outputs or third-party themes (Eole etc.): test against them locally, never
  redistribute.
- **README screenshots** live in `docs/screenshots/`, cropped (`sips`) from the UI tests'
  captures in `build/ui-test/` to the foobar2000 or editor window: never a server name or the
  guest's desktop widgets, and album art or artist photos only in the Eole demo (the owner's
  choice; the VM's library is their Navidrome server).

## Code map
- `src/core/js_engine.{h,cpp}` — `JsEngine` (pimpl over QuickJS): globals `window`, `fb`, `gdi`,
  `console`, `utils`, `include`; classes `GdiGraphics` (`gr`, valid only inside `on_paint`),
  `GdiFont`, `FbTitleFormat`; callback dispatch, timers, promise jobs, error capture. Everything
  fb2k/platform goes through `JsHost`. New API = a `JSCFunctionListEntry` row + function + test.
  New callbacks with plain arguments go through `JsEngine::invoke(name, {Arg…})`; small stubs
  (`CreateTooltip`, `fb.CreateProfiler`) are JS in `kPrelude`.
- `src/core/image_ops.{h,cpp}` — SDK-free pixel operations (crop, resize, mask, alpha, invert,
  rotate/flip, stack blur, colour scheme), tested in `tests/test_image_ops.cpp`.
- `src/core/js_api_util.h` — SMP conventions → canvas: ARGB, `DT_*`, StringFormat, font style.
- `src/core/sample_script.cpp` — built-in sample (tested in both states).
- `src/core/panel.{h,cpp}` — `Panel`: one engine per element, `JsHost` over the SDK
  (titleformat cache, playback_control, play_callback), context menu, script loading; `Surface`
  is what a platform window does (invalidate, timers, file picker, popup menu tree, cursor,
  input box: `win_input.cpp` / `NSAlert`).
- `src/core/settings.cpp` — `cfg_var`s (verbose, default script) + *View › JS Panel* menu.
- `src/core/{log,diagnostics,fs_util}` — same shape as the sister repos (session banner, ring,
  rotation, redaction); `diagnostics_collect.cpp` starts the log (`initquit`) and builds the report.
- `src/platform/win/win_element.cpp` — DUI element: child HWND, double-buffered `GdiCanvas`, mouse
  (capture, leave tracking), `SetTimer`, `TrackPopupMenu`, `GetOpenFileNameW`.
  `win_platform.cpp` — OS/Wine version, clipboard, GDI+ shutdown at quit.
- Keyboard: the hosts take focus on a click (Windows `SetFocus` + `WM_GETDLGCODE` = all keys;
  macOS `makeFirstResponder`) and send `on_key_down` Windows VK codes (`mac_key_to_vk` in
  `js_api_util.h`, tested); a key the script doesn't handle goes to
  `keyboard_shortcut_manager::on_keydown_auto` (Windows) / `super keyDown` (macOS).
- `src/platform/mac/mac_element.mm` — `ui_element_mac`: flipped `NSView`, `CGCanvas` at backing
  scale, tracking area, `NSTimer`s, `NSMenu`, `NSOpenPanel`, plus the `ui::` platform functions.
- `tests/` — `test_js_engine.cpp` (`FakeHost` + `RecordingCanvas`), log, diagnostics. `make test`
  builds QuickJS once without sanitizers and our code with ASan/UBSan.
- `tests/scripts/smoke.js` + `tests/skins/panels-ui-embed` — runtime smoke (`scripts/ui-test.sh`).
- The Panels UI wizard's JS layouts' scripts live in `../foo_ui_panels/src/core/skin_templates.cpp`
  (raw strings `JSNOW` now_playing, `JSPAL` lib/palette, `JSLIB` library_tree, `JSNDA`
  navidrome_artists); `tests/test_js_engine.cpp` extracts and runs them (`PANELS_UI_SRC_DIR`), so
  there is one copy and both repos test it.
- Tracks cross the SDK-free boundary as `jsp::Track` (Panel's `SdkTrack` wraps a
  `metadb_handle_ptr`); `FbMetadbHandle`/`FbMetadbHandleList` hold `TrackPtr`s. Album art:
  `Panel::album_art` (worker `album_art_manager_v2`; for the now-playing front cover first
  `now_playing_album_art_notify_manager::current()`, else it waits for that manager's notify —
  the only path that has Navidrome covers). Results go through `fb2k::inMainThread`, guarded by
  the panel's abort and the engine's `alive` token. The library tree starts from
  `fb.GetLibraryItems()` and rebuilds on the library callbacks (the library is still loading
  when a panel starts with foobar2000).
- `src/core/js_highlight.{h,cpp}` — the editors' JS tokenizer (UTF-16 spans), tested in `tests/test_js_highlight.cpp`.
- `src/core/theme_patches.{h,cpp}` — Eole's in-memory patches, applied by `include()` and `JsEngine::load`.
- `tools/fcl_extract.py` + `scripts/theme-check.sh` (`make theme-check`) — pull every Spider
  Monkey Panel panel (SMP 1.x binary config: version, GUID, edge style, properties, window
  placement, script) out of a Columns UI `.fcl` export and run each with `render_script`
  (`--profile`, `--properties`).
- `tools/render_script.mm` (`make render`, macOS) — runs a script once on a `CGCanvas` (pending
  timers fired, `--click`s, `--art`, a TSV `--library`/`--navidrome`) and writes the frame as a
  PNG. The JS layouts' wizard previews are Panels UI's `albums.png`/`playlist.png` (demo library)
  with these renders composited in at the 1170×600 → 600×308 scale.

## Build
- `./build.sh` (`WIN_ARCH=x64|arm64ec|x86`) and `./scripts/mac-build.sh`, same toolchain, flags
  and gotchas as foo_ui_panels (its CLAUDE.md "Cross-build gotchas"; `cmake/` and
  `scripts/ucrt-arm64ec-lib.sh` are copies). `scripts/toolchain.sh` also resolves `PANELS_UI_SRC`.
- `third_party/quickjs/VERSION` would shadow `<version>` on case-insensitive file systems (macOS),
  breaking libc++: the file is `VERSION.txt`.
- QuickJS compiles as C (`qjs` static lib, `/w`/`-w`); the unit-test Windows modes pass it with
  `/Tc`.

## Runtime testing
- `make mac-demo TEMPLATE=albums-js|playlist-js|library-js`: both bundles + that Panels UI wizard layout (written by Panels UI's
  `tools/install_templates.cpp`, built here) as the active skin; `make mac-restore` undoes it.
- `make mac-smoke`: installs this repo's bundle + `../foo_ui_panels/build-mac` into
  `~/Library/foobar2000-v2`, makes `panels-ui-embed` the active Panels UI skin, sets the default
  script to `tests/scripts/smoke.js`, turns verbose on, launches, asserts the log, then restores
  the skin/settings. `mac-local.sh` moves any component folder it replaces to
  `build/mac-local-backup/` and `--restore` puts it back: the user's own installs (e.g. a
  release Panels UI from the component updater) must survive a test run. Needs Panels UI as the
  mac UI module.
- `make mac-vm-smoke` / `mac-vm-jsp3` / `mac-vm-reload`: the same scenarios in the terminal-mac VM
  (`ui-test.sh mac-vm`): `scripts/`, `tests/` and both bundles go to `~/jsp-test` in the guest and
  `mac-local.sh` runs there over ssh, so the restore logic is the same code. No `dragdrop` (see
  Test environments) and no `import` (no Eole in the guest).
- `make win11-arm-smoke ARCH=…`: same in the Windows 11 ARM VM (config via `win11-arm.sh cfg`,
  x86 in `C:\fb2k-x86`), screenshot in `build/ui-test/`.
- Panels UI only saves a window capture (`foo_ui_panels-previews/`) 60 s after a skin loads when
  nothing plays — handy for a mac screenshot without screen-recording permission.
