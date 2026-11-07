# 01 · Hook — 0:00

A cyan pixel lights up in the dark.

The pixel belongs to the Save button of a settings panel. The renderer
records which node drew every pixel, so the label beside it is read, not
written: the node's id, its role and what its action does.

```text
button#save · role=button · effect=reversible
```

- **Shows:** pixel provenance, the semantic tree.
- **Proves:** a pixel can be asked what it is.
