# openmui-ui-base

The Core components (spec §11), contract-first (component-binding §2):

| Component | Contract | Behaviour |
|---|---|---|
| `button` | `button` | four variants; hover and press feedback (motion tokens); the verb it invokes classifies `activate` (S-1) |
| `toggle` | `toggle` (role `switch`) | the thumb springs; two-way `checked` |
| `checkbox` | `checkbox` | the mark draws itself in; indeterminate (`mixed`) |
| `slider` | `slider` | drag with capture, keys (arrows, Home, End), actions with `min`/`max`; `changed` while moving, `committed` at the end |
| `text_box` | `text_box` | the editing model of openmui-text: graphemes, words, undo; selection by pointer and keys; clipboard; input methods; blinking caret; masked passwords; actions in grapheme offsets |

Each class derives from the CRTP base `omui-gen cpp` makes from its contract
(`src/generated`, committed and checked by the generator's tests); a
contract action without an implementation does not compile. Components use
the runtime's public API only, and every colour, size and curve comes from
theme tokens.

```cpp
import openmui.ui.base;

omui::register_base_components();   // view documents and immediate mode find them by name
rt.mount("main", omui::column({.padding = 16, .gap = 8}, {
    omui::text_box({.id = "name", .label = "Name", .text = name}),
    omui::button({.id = "save", .text = "Save", .variant = omui::ButtonVariant::primary,
                  .on_activated = [&] { save(); }}),
}));
```

The gallery test pins the pixels of every component in its states: the same
bytes with GCC and LLVM, on Linux, macOS and Windows.
