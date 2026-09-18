# AGENTS.md

platform-h is the platform layer shared by every application in this workspace
(`list0`, `ovrwin`, `potato`, `rethinkify-app`, `crypto-app`, `stuntcarracer`,
`equity-app`). It is a library with no application of its own.

It ships **two** static libraries:

| Target | Alias | Source | Contents |
|---|---|---|---|
| `platform-core` | `platform::core`, `platform::platform` | `src/core` | The OS abstraction. Every app links it. |
| `platform-ui` | `platform::ui` | `src/ui` | Reusable presentation: widgets, text views, markdown, the agent chat panel. Opt-in. |

`platform::platform` remains an alias of `platform-core` because seven repositories
pin this package and acquire it four different ways. Do not remove it.

platform-ui operates on a UTF-8 text buffer the application hands it. It never
opens a file, never knows a path, and never names an application.

## Non-negotiables

1. **`src/core/platform.h` includes no OS headers.** Not `windows.h`, not
   transitively. The Windows SDK appears only in `src/core/platform_win.cpp` and
   `src/core/platform_win_audio.cpp`.
2. **The API must be expressible on another OS.** Nothing Win32-shaped may leak
   into a signature: no `HWND`, no `HRESULT`, no hundredths-of-a-decibel volume,
   no resource IDs. Prefer neutral units — linear gain, normalised pan, Hz,
   UTF-8 `string_view`. If a concept only makes sense on one platform, it
   belongs in the app.
3. **App-specific knowledge stays out.** No app names, no resource identifiers,
   no game constants. `config_set_app_name` exists precisely so the layer never
   has to know who is calling.
4. **A change here breaks seven repos.** Adding to `window_frame` or
   `frame_reactor` is a breaking change for every app and their test stubs.
   Build all of them before you consider a change done — see below.
   `platform::platform` is an alias every one of them links; it does not move.
5. **Add a test with a capability.** `tests/platform_tests.cpp` and
   `tests/ui_tests.cpp` are console programs; anything checkable without a
   window belongs in one of them.

## Layout

| File | Contents |
|---|---|
| `src/core/platform.h` | The whole `pf::` surface. Declarations, plus inline text/geometry helpers |
| `src/core/platform_common.cpp` | Backend-independent implementation (the embedded-resource registry, `line_splitter`) |
| `src/core/platform_win.cpp` | Win32 backend: `WinMain`, message loop, GDI drawing, WIC, WinINet/WinHTTP, dialogs, clipboard, spell check, child processes |
| `src/core/platform_win_audio.cpp` | XAudio2 backend, isolated from the rest |
| `src/ui/ui.h` | platform-ui umbrella header |
| `src/ui/theme.h` | The `pf::ui::colors` table |
| `src/ui/widgets.h` | `edit_box`, `caret_blinker`, `splitter`, `custom_scrollbar`, `edit_box_widget` |
| `src/ui/table_layout.h` / `.cpp` | Table cell parsing and rendering, shared by the markdown and CSV views |
| `cmake/platform_app.cmake` | `platform_add_app()` — the function apps use to declare themselves |
| `cmake/embed_resources.cmake` | Turns data files into a generated C++ byte-array TU |
| `cmake/app.manifest.in` | The manifest every app gets |
| `tests/platform_tests.cpp` | The platform-core suite (CTest label `platform`) |
| `tests/ui_tests.cpp` | The platform-ui suite (CTest label `platform-ui`) |

## Build and test

This repo uses the vendored dd build system. dd has two modes:

**CLI mode** — the default; each verb runs once and exits:

```pwsh
.\dd.ps1 test                  # build both configs and run the suite
.\dd.ps1 build debug
.\dd.ps1 doctor --json
```

**MCP mode** — `.\dd.ps1 mcp` turns the process into a stdio JSON-RPC server for an
MCP client, adapting typed requests onto CLI mode. It owns stdout for protocol
messages, so it prints no result envelope and rejects `--json`. Register it with
`.\dd.ps1 ide --mcp`.

Project settings live in `dd.psd1`; dependency pins live in
`cmake/dd-dependencies.json` (`dependencies.owner = 'dd'`).

To check nothing downstream broke, from the workspace root:

```pwsh
foreach ($r in 'platform-h','list0','ovrwin','potato','rethinkify-app','crypto-app','stuntcarracer') {
    Push-Location $r; .\dd.ps1 build release; Pop-Location
}
```

`equity-app` also consumes this package, but through its own pinned acquisition
(`EQUITY_PLATFORM_SOURCE_DIR`), which requires a clean tree at the declared pin.
Build it separately with `.\dd.ps1 build both`.

Each app finds a sibling `../platform-h` checkout automatically, so a local edit
here is picked up by all of them with no publish step.

## Adding a capability

1. Declare it in `src/core/platform.h`, in neutral terms (see rule 2).
2. Implement it in the backend.
3. Add a case to `tests/platform_tests.cpp` if it can run without a window.
4. Rebuild every app. If you added a pure virtual to `window_frame`, expect to
   update `stub_window_frame` in `rethinkify-app/src/tests.cpp` too.

## Adding to platform-ui

1. It goes in `src/ui/`, in `pf::ui`, reached as `#include "ui/<file>.h"`.
2. It may use `pf::`. It may not name an application, open a file, know a path,
   or speak a protocol — those belong to the app.
3. Anything pure gets a case in `tests/ui_tests.cpp`. Layout and hit testing are
   testable without a window because `pf::measure_context` is an interface.
4. An app opts in with `platform_add_app(<target> UI ...)`.

## Conventions

- Modern C++20 in `snake_case`; `pf::` for everything public.
- Comments explain *why*, in one line. Do not narrate the next line.
- Diagnose failures with `pf::debug_trace` and the real error code rather than
  failing silently.
- `dd.ps1` is duplicated in each repo by design — it has to run before CMake, so
  it cannot come from this package.
