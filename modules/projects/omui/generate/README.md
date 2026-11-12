# omui.generate

Runtime generation as an application ships (D-144, convergence plan W5): interface an Agent makes while the application runs, put into the surfaces the application declares.

A draft goes through stages, each asked for and each readable — draft → validated → prepared → previewed → committed. A commit is a version of its surface that undo takes back; versions are kept on disk and checked again when the application starts. How open it is (off, data, limited, all) is the developer's in `omui.config.ts`, narrowed by the user's settings and the session (openas). `:a2ui` reads A2UI's server messages as one input format: a stream's surfaces drafted and committed as they come.

Its operations are the `generate.*` catalog, [omui.agent's generate.ops.omui.ts](../agent/generate.ops.omui.ts) (the catalogs an application's endpoint describes live there, beside the protocol's and Live Design's); `Generation` implements them on an application's runtime.

A package of its own, not part of [omui.agent](../agent): generation takes view documents ([omui.viewdoc](../viewdoc)); the endpoint takes none. Held together they made the endpoint depend on documents and documents' tests on the endpoint (red list 44).

```sh
mcpp test -p omui.generate
```
