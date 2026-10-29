# One tree, many renderings

openmui is a UI framework and an open specification built around one idea: **the interface is a semantic tree**, and everything else is a rendering of it.

- The **pixels** you see are one rendering, drawn the same on every platform, bit for bit.
- **Assistive technology** reads another, the tree's roles, names and states.
- An **agent** reads the tree as data and acts through the same actions a person's input reaches, with the same checks and the same undo.
- A **test** asserts on the tree and pins the pixels.

This site is built with openmui. Switch the **lens** above to see the page you are reading as each of those renderings: its pixels, its tree's wireframe, the JSON an agent receives, the assertions a test would write.

## What is here

- **Features** and **Practices**: the framework's documents, in English and 中文. Read [what sets openmui apart](features.md) first.
- **Timeline**: a working timeline editor. Drag a clip, trim its edge, press S to split it at the playhead, then Ctrl+Z. Every edit is one exact change with its exact inverse.

- **An agent on this page**: ask it to read the page, edit the timeline or open a link. Each step is a real request on the bus, decided by [openas](https://github.com/openlifa/openas). What is external asks you first, and the buttons that answer are yours alone.

The specification is an open standard, [openlifa/openmui](https://github.com/openlifa/openmui) (CC BY-SA 4.0).

The footer is the page's time. Drag it back and the page is drawn as it was: the camera's flights, the letters of a language switch and the lenses' fades, played backwards. Animations are functions of time, so any moment can be shown again. Only the drawing goes back; the tree stays as it is now. Beside the time is the semantic tree's hash as your browser computed it. Determinism is part of the design: the same tree on every machine.
