# Interfaces

An interface is a tree: each node has a role, a name, a value, states and the actions it offers. You describe the tree in code; the framework draws it, gives it to screen readers, and gives it to agents.

![The mark: a circle around the word mui](media/mark.svg "omui's mark")

> One semantic tree, many renderings: pixels, accessibility, agents and tests are views of the same tree.

## Three ways to write it

In TypeScript, the default:

```ts
import { column, text, button, mount } from "openmui";

mount("main", column({ gap: 8 }, [text({ text: "Saved" }), button({ id: "undo", text: "Undo" })]));
```

In C++, for what must be fast:

```cpp
import omui;

omui::Element view() {
    return omui::column({.gap = 8}, {omui::text({.text = "Saved"}), omui::button({.id = "undo", .text = "Undo"})});
}
```

As a view document, for interfaces a tool generates:

```json
{ "type": "column", "gap": 8, "children": [{ "type": "text", "text": "Saved" }, { "type": "button", "id": "undo", "text": "Undo" }] }
```

What an agent reads of all three is the same tree:

```
column
  text "Saved"
  button#undo "Undo"
```

## Identity

A node an agent or a test must find has an explicit id, and the id never depends on the text shown: `undo` stays `undo` when the label is translated.
