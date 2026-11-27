# A live example

A document can show an example running. A block of the language `view` is a view document — an interface written as data: its components, its state, what an event sets — and the page shows it twice: the example itself, and under it the source that makes it.

## A counter

```view
{
  "omui": "0.1",
  "imports": ["omui.ui.base"],
  "view": {
    "name": "Counter",
    "signals": { "count": { "type": "int", "value": 0 } },
    "root": {
      "type": "row",
      "id": "counter",
      "props": { "gap": 12, "align": "center" },
      "children": [
        {
          "type": "button",
          "id": "add",
          "props": { "text": "Add one", "variant": "primary" },
          "on": { "activated": [{ "set": "count", "expr": "count + 1" }] }
        },
        {
          "type": "text",
          "id": "said",
          "props": { "text": { "expr": "count == 0 ? 'Not pressed yet' : format('Presses: {}', count)" } }
        },
        {
          "type": "button",
          "id": "reset",
          "props": { "text": "Reset", "variant": "ghost", "disabled": { "expr": "count == 0" } },
          "on": { "activated": [{ "set": "count", "value": 0 }] }
        }
      ]
    }
  }
}
```

Press **Add one**: the text beside it counts, and **Reset** becomes available.

## What each reader gets

- Before the page's program is there, the example is shown as it starts — its buttons, its text — with its source under it. Nothing is lost if the program never comes.
- When the program is there (this page takes it once the page is idle), the example runs: the same nodes, now answering.
- An agent that fetches the page's Markdown gets the block as it is written: a view document it can check and run.

The source is data, so the page's program carries no script engine to run it.
