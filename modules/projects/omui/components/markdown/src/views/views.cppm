// omui.ui.markdown.views — view documents run in a Markdown document (the package's feature `views`).
//
//     omui::install_markdown_views();   // at the program's root, once
//
//     ```view
//     { "omui": "0.1", "imports": ["omui.ui.base"],
//       "view": { "signals": {"count": {"type": "int", "value": 0}},
//                 "root": {"type": "button", "id": "add", "props": {"text": {"expr": "format('Pressed {} times', count)"}},
//                          "on": {"activated": [{"set": "count", "expr": "count + 1"}]}} } }
//     ```
//
// A block of the language `view` in a document of the application's own is a view document (omui.viewdoc): data —
// components, signals, expressions, what an event sets — so it runs with no script engine, and what the reader is
// shown as the example's source is what runs. A text that is no view document says what is wrong, at its line and
// column, in the example's place.
export module omui.ui.markdown.views;

import std;
import mlibs.compose;

export namespace omui {

// Installs the provider for `view` at omui.ui.markdown's slot. Again: nothing.
std::expected<void, mlibs::compose::Error> install_markdown_views();

}  // namespace omui
