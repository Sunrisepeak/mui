# omui's programs

The composition roots of omui (component-binding §10.2): programs, not libraries — each depends on the SDK and what
it hosts, and nothing depends on it.

| Package | |
|---|---|
| [player](player) | `omui.entry.player`: runs a TypeScript application from its manifest (`[package.metadata.openmui]`), registers the official components and the script languages, and runs it with omui's options; saved files reload in place |
| [devhost](devhost) | `omui.entry.devhost`: the development host (`omui dev`, D-154): it holds the window, the application is its render target, and the designer is linked (preview: `omui.designer`) |
