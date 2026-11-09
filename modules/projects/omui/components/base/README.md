# omui.ui.base

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
made into code when the package builds — `build.mcpp`, omui.build.contracts):

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
| `actor_log` | `log` | who else acts here (agent connect §3.2): the person's agent link (waiting with its code, linked, lost) and its actors' actions from the runtime's journal, newest last, each a node (`<id>/<entry>`); the last its history holds taken back from here (Undo, or the item's `undo`, as whoever asks); a record hovered or focused points out the node it acted on — an outline over the frame, not a cursor; ending the link is the application's control, the person's own (S-10); its words in another language through `phrases` |
| `agent_presence` | `status` | another actor at work, seen (site plan §9.5): over the page and taking no pointer, the agent's corners round the node it acts on now with its name and the action, the steps before it as a dotted trail, and — when its request waits for the person — a card saying so, with nothing on it that answers (S-10). The model under it, `agent_presence_state(services, actors)`, is what every skin draws (`skin = "none"`: another draws it, the site's Mui) and what the tree says; `parts::agent_corners` is the one mark of an agent's attention (the promo's agent, breakout's intent) |
| `segmented` | `radio_group` | one of a few choices side by side, the chosen one raised; each a radio of its own (`<id>/<value>`); pressed, arrowed, chosen by an agent, taken back |
| `chip` | `button`, or `toggle_button` when `selectable` | a compact label pressed; a filter turned on and off (taken back by pressing again); a tag removed by its × (`<id>/remove`) |
| `range_slider` | `group` | two thumbs on one track, the low never past the high; each a slider of its own (`<id>/low`, `<id>/high`); dragged (taken back whole), stepped (Tab between them), set |
| `alert` | `status` | a message in the page that needs attention: its tone said, a title, one action at most, dismissible when it may be (then hidden to every reader) |
| `stepper` | `navigation` | the steps of a sequence: done (checked), current, ahead; a done one gone back to (`<id>/<index>`), ahead only when `free`; taken back |
| `date_picker` | `combo_box` | an ISO date chosen from a month's calendar (arrows a day or a week, PageUp/PageDown a month, Enter chooses); any date an item of the picker an agent chooses without opening it (`<id>/2026-11-05`), within `min`–`max`; the month's days and its arrows items while open |
| `time_picker` | `group` | a time of day, its hours and minutes spin buttons of their own (`<id>/hours`, `<id>/minutes`; `<id>/period` in 12-hour), stepped (Up, Down; Left, Right between them) or typed, kept on `step` minutes |
| `color_picker` | `combo_box` | a colour (#rrggbb) from a saturation–brightness field, a hue strip and the application's swatches — items of its own while open (`<id>/field`, `<id>/hue`, `<id>/swatch_<n>`); a grey keeps its hue; a drag taken back whole |
| `split_view` | `group` | two panes (its children) sharing their room as `ratio` says; its handle a splitter of its own (`<id>/handle`, a percentage) dragged (taken back whole), stepped by the arrows, set by an agent, kept `min` from either edge |
| `drawer` | `dialog` | a sheet along the window's left or right edge, as tall as the window (a placement of its own: `left`, `right`), over the page — modal by default, its first control focused; Escape, a press outside, its × or an agent closes it |
| `tree_view` | `tree` | a model's rows as a hierarchy (each row's `parent` field); each a tree item of its own (`<id>/<key>`) opened, closed and chosen — Right opens or goes in, Left closes or goes up; a row inside closed ones chosen by an agent opens them; closed ones' rows not stated; drawn as they come into view |
| `data_grid` | `grid` | a model's rows in columns (its `fields`; pixels or shares); sorted by a header pressed (ascending, descending, none; numbers by value, text regardless of case); rows chosen one or several (Ctrl, Space); each row (`<id>/<key>`, its fields its value) and header (`<id>/col_<field>`) an item; drawn as they come into view |
| `combobox` | `combo_box` | one value typed or chosen: its text box filters the options to those whose labels hold what is typed (regardless of case), the arrows move among them, Enter or a press chooses, Escape closes; each option an item (`<id>/<value>`); a value typed that is no option stands only when `free` |
| `tag_input` | `group` | tags gathered by typing (Enter or a comma keeps one) or choosing a suggestion that holds what is typed; each a chip (`<id>/tag_<n>`) removed by its ×; `add`, `remove` by name, each taken back exactly (its place kept) |
| `code_editor` | `group` | code edited in its field (`<id>/text`, a text box in its code mode): coloured by what each stretch is (the language's words, strings, numbers, comments — C++, TypeScript, Python, Lua, JSON, TOML), its lines numbered, Tab indenting (Escape, then Tab, leaves), Enter keeping the indent; set whole with `set_value`, taken back |
| `kbd` | `text` | a key or a chord drawn as the keys themselves, each a cap (`Ctrl` `Shift` `P`), chords pressed in turn spaced apart (`G G`); `Mod` is the system's command key (⌘ or Ctrl, from the host's primary modifier); read as the keys' names ("Control+Shift+P") |
| `separator` | `separator` | a line across as wide as it is given, or down; a word in its middle (`or`) when it has one |
| `link` | `link` | text that goes somewhere: an address the system opens (http, https, mailto — external: an agent answers "yes", a person's click or Enter is their answer) or the application's verb (its effect); underlined in the accent |
| `description_list` | `list` | terms beside what they mean (baselines aligned) or each above its own; each pair an item of its own (`<id>/<index>`: its name the term, its value the description) |
| `spinner` | `progress` | something happening, how long unknown: an arc turning on the runtime's clock, `busy`; still when motion is reduced |
| `rating` | `slider` | how much out of a few stars (whole or half steps); the pointer shows what a press would set, pressing its value clears it; keys step it; read-only only shows |
| `otp_input` | `text_box` | a one-time code, a box a character: typed one by one, pasted whole (what a box cannot hold dropped), Backspace, arrows; `completed` when full |
| `form_field` | `group` | a control with its label above (`*` when required), its help under it and an error in the help's place; the group's description is the help or the error, `required` and `invalid` said |
| `search_box` | `search_box` | a magnifier, a bare text box (its words), a × button of its own while there are words (`<id>/clear`); Enter searches (`submitted`), Escape clears |
| `toggle_group` | `toolbar` | a few options each on or off on its own, each a toggle button of its own (`<id>/<value>`); pressed, Space, turned by an agent; kept in the options' order |
| `context_menu` | `menu` | the choices for the element before it (its items menu_items): a secondary press over it opens it at the pointer, the Menu key at the element, `show {x, y}` anywhere; kept inside the window (flipped); a choice, Escape or a press outside closes it, focus given back |
| `hover_card` | `dialog` | more about the element before it (anything inside): shown after `delay` of pointing or focus, kept while the pointer is on the card, gone `linger` after it leaves both; not modal |
| `command_palette` | `dialog` | the application's commands (its verbs that take no arguments, or those it is given) found by their words, over the window's middle; arrows choose, Enter runs (as the person's choice: their answer to what reaches outside), Escape closes; each command an option of its own an agent runs |
| `sidebar` | `navigation` | the places down an application's side, each a link of its own (`<id>/<value>`), the current one marked; pressed, arrowed (each a step taken back), chosen by an agent; collapsed to a rail of first letters |
| `toolbar` | `toolbar` | a row (or column) of controls; the arrows move the focus between them, Home and End to the ends |
| `chart` | `figure` | numbers over labelled steps as lines, bars or areas, a scale in round numbers (1, 2, 2.5, 5 × 10ⁿ), a legend for more than one series; pointing shows a step's values; each point an item of its own (`<id>/<series>/<step>`: its name the series and the step, its value the number), its description says what it shows |
| `carousel` | `region` | one of several slides at a time (each as wide as it, the rest clipped); its arrows, dots (`<id>/<index>`), the arrow keys and a swipe move between them, each a step taken back; how the move looks is its slot's |

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
import omui.ui.base;

omui::register_base_components();   // view documents and immediate mode find them by name
rt.mount("main", omui::column({.padding = 16, .gap = 8}, {
    omui::text_box({.id = "name", .label = "Name", .text = name}),
    omui::button({.id = "save", .text = "Save", .variant = omui::ButtonVariant::primary,
                  .on_activated = [&] { save(); }}),
}));
```

The gallery test pins the pixels of every component in its states: the same
bytes with GCC and LLVM, on Linux, macOS and Windows.
