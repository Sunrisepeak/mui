# openmui.ui.window

The framework's own window frame (D-75).

```cpp
import openmui.ui.window;

omui::window_frame({.title = "Notes"}, {content…});                                  // Windows / Linux style
omui::window_frame({.title = "Notes", .controls = "start", .look = "traffic"}, {…});  // macOS style
```

| | |
|---|---|
| `window_frame` | shadow, rounded edge, title bar and buttons in a framework-drawn window; only its content in a native one. Its title bar moves the window and its edges resize it, through the system (`window_area_at`), so snapping still works. Maximized: no shadow, no corners, a restore button; inactive: muted. Actions `minimize`, `toggle_maximize`, `close`. |
| `caption_button` | minimize, maximize, restore, close — a glyph button (`standard`) or a coloured circle (`traffic`); named in the person's language (en, zh-Hans, zh-Hant). |

Every button and action is a `window.*` verb (`openmui.view:window`), so a click, a shortcut and an agent do the same thing. Contracts: [contract/](contract/). Tests: [tests/window_test.cpp](tests/window_test.cpp), with both looks' pixels pinned on every platform.
