# openmui.ui.base

The Core components (spec §11), contract-first (component-binding §2):

| Component | Contract | Behaviour |
|---|---|---|
| `button` | `button` | four variants; hover and press feedback (motion tokens); the verb it invokes classifies `activate` (S-1) |
| `toggle` | `toggle` (role `switch`) | the thumb springs; two-way `checked` |
| `checkbox` | `checkbox` | the mark draws itself in; indeterminate (`mixed`) |
| `radio` | `radio` | one value per group: the group is a signal its radios share |
| `slider` | `slider` | drag with capture, keys (arrows, Home, End), actions with `min`/`max`; `changed` while moving, `committed` at the end |
| `text_box` | `text_box` | the editing model of openmui.text: graphemes, words, undo; selection by pointer and keys; clipboard; input methods; blinking caret; masked passwords; actions in grapheme offsets |
| `progress` | `progress` | determinate with a gliding fill, indeterminate (`busy`) with a sweep on the runtime's clock |
| `tabs`, `tab` | `tabs`, `tab` | `selected` is the tab's id; the indicator springs; arrows walk the strip, skipping disabled tabs |
| `select` | `select` (role `combo_box`) | a composite: it builds its popup (an overlay as wide as the field) and its options; keys, pointer and `set_value` (checked against the options) |
| `menu`, `menu_item` | `menu`, `menu_item` | an overlay below the element before it; the first item takes focus; arrows, Enter, Escape, a press outside; focus returns to the trigger |
| `dialog` | `dialog` | modal: scrim, focus kept inside and given back, Escape or the scrim dismisses (`closed` says how) |
| `list`, `list_view` | `list` | virtualised (S-7): the rows in view are nodes, the rest are stated by the semantic tree and materialised when acted on; measured heights; roving focus; none, single or multiple selection |
| `tooltip` | `tooltip` | describes the element before it; shown after `delay` on hover or focus, on the runtime's timers |

Beside them, the package's own components (their contracts in `contract/`,
made into code when the package builds — `build.mcpp`, openmui.build.contracts):

| Component | Role | Behaviour |
|---|---|---|
| `badge` | `status` | a label, a count (`99+` past `max`) or a dot, in a tone (neutral, accent, success, warning, danger) |
| `avatar` | `image` | a stored picture (`image:<n>`), else initials on the name's own colour (the same name, the same hue); a status dot said as its description |
| `skeleton` | `progress` | the shape of what is coming (lines, a block, a circle), `busy`; its bones are its effect shapes (a style's shimmer) |
| `card` | `group`, or `button` when `interactive` | a title, a subtitle and its content on a raised, outlined or filled surface; activated whole by pointer, keys or agents |
| `accordion` | `region` | its header always shown, its content while `expanded`; only the header opens it; `expand`, `collapse` taken back by each other |
| `toast` | `status` | over a corner of the window (toasts at one corner stack), gone after `duration` or dismissed; one action at most |
| `popover` | `dialog` | beside the element before it, not modal; Escape, a press outside it and its button, or `dismiss` closes it |
| `breadcrumb` | `navigation` | each place a link of its own in the semantic tree (`<id>/<index>`); going up leaves the places below |
| `pagination` | `navigation` | previous, the first, the pages around the current one, the last, gaps (…), next; each page a link (`<id>/<page>`); stepped, set, taken back |
| `number_input` | `spin_button` | typed (Enter or leaving keeps it, Escape drops it) or stepped (−, +, arrows), kept within `min`–`max` on `step`; undo restores it exactly |
| `actor_log` | `log` | who else acts here (agent connect §3.2): the person's agent link (waiting with its code, linked, lost) and its actors' actions from the runtime's journal, newest last, each a node (`<id>/<entry>`); the last its history holds taken back from here (Undo, or the item's `undo`, as whoever asks); a record hovered or focused points out the node it acted on — an outline over the frame, not a cursor; `unlink` ends the link, touching no history; its words in another language through `phrases` |

They draw what they are now: how a change of them moves is their slots'
(universal motion), never their own code. Their gallery (`tests/kit_test.cpp`)
is pinned like the Core's.

Each class derives from the CRTP base `omui-gen cpp` makes from its contract
(`src/generated`, committed and checked by the generator's tests); a
contract action without an implementation does not compile. Components use
the runtime's public API only. Every colour, text style, corner, spacing and
curve comes from theme tokens; a control's own metrics (a button's 36 px
height, a slider's thumb) are the component's, scaled by the theme's
`density` (a compact theme's 0.8 makes a button 29 px high).

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
