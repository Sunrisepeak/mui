# One tree, many renderings

openmui is a UI framework and an open specification built around one idea: **the interface is a semantic tree**, and everything else is a rendering of it.

- The **pixels** you see are one rendering, drawn the same on every platform, bit for bit.
- **Assistive technology** reads another, the tree's roles, names and states.
- An **agent** reads the tree as data and acts through the same actions a person's input reaches, with the same checks and the same undo.
- A **test** asserts on the tree and pins the pixels.

This site is built with openmui. Switch the **lens** above to see the page you are reading as each of those renderings: its pixels, its tree's wireframe, the JSON an agent receives, the assertions a test would write.

## What is here

- **Features** and **Practices**: the framework's documents, in English and 中文.
- **Timeline**: a working timeline editor. Drag a clip, trim its edge, press S to split it at the playhead, then Ctrl+Z. Every edit is one exact change with its exact inverse.

The footer shows the semantic tree's hash as your browser computed it. Determinism is part of the design: the same tree on every machine.
