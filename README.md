# platform-h

A minimal C++ desktop app platform library.

Provides a platform-independent abstraction (`platform.h`) for building desktop GUI applications — windows, drawing, fonts, menus, input, clipboard, files, networking, and more — with a Win32 implementation in `platform_win.cpp`.

## Architecture

### `platform.h` — Platform-Independent API

Header-only declarations with no OS-specific includes. Provides:

| Category    | Key Types / Functions |
|-------------|----------------------|
| Text        | UTF-8/16/32 conversion, `pop_utf8_char()`, `utf8_truncate()`, surrogate pair handling |
| Windowing   | `window_frame`, `app_init()`, `app_idle()`, `app_destroy()` callbacks |
| Drawing     | `draw_context` — shapes, text, clipping, opacity |
| Fonts       | Font creation, measurement, text layout |
| Menus       | Menu definition tree, accelerator table |
| Input       | Mouse, keyboard, focus, caret management |
| Clipboard   | Copy/paste text |
| Files       | Open/save dialogs |
| Networking  | `web_request`/`web_response` — HTTP GET/POST with SSL |
| Timers      | Performance counters, periodic callbacks |
| Threading   | `run_async()`, `run_ui()` (marshal to UI thread) |

### `platform_win.cpp` — Win32 Implementation

Full implementation using native Win32 APIs:

- **Entry point:** `wWinMain` with command-line parsing
- **Drawing:** Direct2D render targets, geometry, text layout
- **Networking:** WinHTTP (synchronous GET/POST, WebSocket upgrade)
- **Menus:** Runtime `HMENU` + accelerator table construction from declaration tree
- **Files:** `GetOpenFileName` / `GetSaveFileName` dialogs
- **Clipboard:** `OpenClipboard` / `SetClipboardData` with UTF-16 conversion

## Usage

Applications implement three callbacks declared in `platform.h`:

```cpp
app_init_result app_init(const pf::window_frame_ptr& frame,
                         std::span<const std::string_view> args);
void app_idle();
void app_destroy();
```

The platform layer owns the message loop and calls these at the
appropriate times.

