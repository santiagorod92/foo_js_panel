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
  build-all`; (3) runtime: `make mac-smoke` (this Mac's foobar2000) and `make win11-arm-smoke
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
  `fix`/`perf`/`refactor` = patch; `chore`/`docs`/`ci`/`style`/`test` = no release). `main`
  is protected (PRs only) and nothing is committed back. `CHANGELOG.md` is kept by hand in the
  release PR. `notify-n8n` publishes to foobar2000.org. If the head commit is non-releasing,
  dispatch the workflow by hand.
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
- **Windows 11 ARM VM (Apple Silicon Mac):** foo_navidrome's `scripts/win11-arm.sh` (QEMU +
  HVF, launchd jobs). Every repo drives it with the same `make win11-arm*` targets; foo_ui_panels
  and foo_js_panel point at `../foo_navidrome/scripts/win11-arm.sh` (`WIN11_ARM=`). `deploy <dll>` installs into
  `user-components-arm64ec/<dll name>`, `push` copies a folder in, `COMPONENT=` picks the log
  `fb2k log` reads, `make win11-arm-test` runs each repo's unit tests in the guest.
- **All three Windows builds run in that one VM:** ARM64EC natively, x64 in the same ARM
  foobar2000 (an ARM64EC process loads x64 DLLs), x86 in a portable 32-bit foobar2000 at
  `C:\fb2k-x86` (`FB2K_ARCH=x86`, foo_ui_panels and foo_js_panel: `ARCH=x86`; `provision` installs it once and
  seeds its profile from the ARM one). `fb2k stop` closes whichever build is running.
- **UI tests:** every repo drives the real foobar2000 with one script,
  `scripts/ui-test.sh <platform> <scenario>` (foo_ui_panels and foo_navidrome: `wine|win11|mac`;
  foo_js_panel so far: `mac` = this Mac's foobar2000, `win11-arm`): each scenario is written once over a small
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
- **Edit script:** `Surface::open_editor` (one editor window per panel, closed with it; Windows
  `win_editor.cpp` = plain `EDIT` control, macOS `NSTextView` in `mac_element.mm`). Applying writes
  the file and reloads; the built-in sample is saved as `<profile>/foo_js_panel/script[-N].js`.
  The syntax check is `check_syntax()` (QuickJS compile-only, own small runtime).
- **foo_navidrome:** `navidrome` script object over the vendored `navidrome_library_api.h`
  (identical to Panels UI's copy). `list_albums[_v2]` blocks, so `Panel::navidrome_albums` runs it on
  a detached thread with the panel's `abort_callback` and hands the result back with
  `fb2k::inMainThread`; the engine resolves the Promise only while it's alive (`Impl::alive`).
  Artists are derived from the albums (`library_artists`). Songs/search need a new contract
  version in foo_navidrome.
- **Hardening:** 5 s budget per call (interrupt handler), 512 MB runtime memory, 1 MB stack. A
  script error stops the script (timers killed) and the panel paints the error + stack, like SMP.
- Panels UI bridge (variables/actions/notify) and drawing onto the Panels UI canvas are planned
  contracts; they follow the `service_base` contract pattern in Shared conventions.

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

## Code map
- `src/core/js_engine.{h,cpp}` — `JsEngine` (pimpl over QuickJS): globals `window`, `fb`, `gdi`,
  `console`, `utils`, `include`; classes `GdiGraphics` (`gr`, valid only inside `on_paint`),
  `GdiFont`, `FbTitleFormat`; callback dispatch, timers, promise jobs, error capture. Everything
  fb2k/platform goes through `JsHost`. New API = a `JSCFunctionListEntry` row + function + test.
- `src/core/js_api_util.h` — SMP conventions → canvas: ARGB, `DT_*`, StringFormat, font style.
- `src/core/sample_script.cpp` — built-in sample (tested in both states).
- `src/core/panel.{h,cpp}` — `Panel`: one engine per element, `JsHost` over the SDK
  (titleformat cache, playback_control, play_callback), context menu, script loading; `Surface`
  is what a platform window does (invalidate, timers, file picker, popup menu).
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
- `make win11-arm-smoke ARCH=…`: same in the Windows 11 ARM VM (config via `win11-arm.sh cfg`,
  x86 in `C:\fb2k-x86`), screenshot in `build/ui-test/`.
- Panels UI only saves a window capture (`foo_ui_panels-previews/`) 60 s after a skin loads when
  nothing plays — handy for a mac screenshot without screen-recording permission.
