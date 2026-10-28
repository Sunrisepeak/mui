# openmui.viewdoc

View documents, `*.view.omui.json` (view-data-files §3). A document is a view written as data. It suits designers, agents and generative interfaces, components that every language shares, and C++ applications whose interface changes without a rebuild.

```cpp
auto view = omui::viewdoc::View::load_file(rt, "ui/counter.view.omui.json");
if (!view) for (const auto& d : view.error()) std::println("{}", d.text());
view->slot("toolbar", my_toolbar());   // code fills the document's slots
view->mount(rt, "main");
view->reload(new_text);                // in place: signals keep their values
```

- **Reading and checking** (`read`). Every name resolves against what is registered when the document runs:
  - component types and their contracts;
  - the runtime's own properties and events;
  - the document's signals, models and interface.
- **What the checks catch.** Unknown components, properties, events, signals, models, names and functions get a suggestion (`did you mean "button"?`, `component names are lower case: "text_box"`). The checker also reports:
  - a constant of the wrong type, or a choice outside its values;
  - `bind` on a property that is not bindable;
  - invalid, repeated and missing ids (S-8);
  - two verbs on one activation;
  - expressions that do not parse, with the diagnostic placed inside the JSON string.

  Each diagnostic has a JSON pointer, a line and a column, a fix where one is known, and a structured form (`omui check --json`). An unknown field is a warning (R7).
- **Running** (`View`). A document becomes the Elements a builder makes, so its semantic tree is the builder's (tested against example 03 and a counter, pixels included):
  - a constant is a property's value;
  - `{"expr": …}` is a reactive source over signals, models (their rows), interface properties and, in handlers, `event`;
  - `{"bind": …}` is two-way;
  - `{"tr": …}` reads the catalogue.

  An expression that fails shows the property's contract default and logs once, with its place.
- **Events** run their operations in one batch: `set` a signal, invoke a `verb`, `emit` an event (a view's go to `View::on`; a component's to its node). A verb on an activation becomes the node's `verb` and `verb_args`. The runtime then classifies the action by the verb's effect and asks for its answer (S-1, S-2), as for any button. A row's delete button passes its own row's key, and a caller cannot redirect it.
- **`if` and `for`** are regions: they add no node, and are reconciled by key. A loop's rows are named by their keys (S-8). Inside a list, a loop is the list's item view and is virtualised (S-7). With `where`, the list gets a filtered model that follows the rows and whatever the filter reads.
- **Slots** are filled by code, before or after mounting.
- **Reload** (`reload`, `reload_file`) checks first, so a text with errors changes nothing. Signals of the same name and type keep their values and identity. Nodes that stay keep their state, focus and animations. `element()` embeds a document among other elements without a node of its own, and follows reloads too.
- **Component documents** (`define_component`). A document with an interface (properties, some bindable; events with arguments; a role) defines a component. Its contract is generated from the interface, so builders in every language, `make("search_box", …)` and other documents use it like any component. Bindable properties write back to their outer binding. A redefinition rebuilds every instance in place, and each instance keeps its properties and state.
- **Schema and format.** `view_schema()` is the documents' JSON Schema (draft 2020-12), generated from the registered components and committed as `spec/schema/view.schema.json` (`omui schema`). `format_document` is the one canonical text (`omui fmt`). It is idempotent and keeps every value.
- **Fuzzed.** Random documents never crash the reader or the runtime. Their diagnostics are one pinned hash on every platform.

Observability: the log category `viewdoc`, and the trace spans `viewdoc.load`, `viewdoc.reload` and `viewdoc.define`.

Depends on `openmui.core`, `openmui.view`, `openmui.expr` and `openmui.trace`.
