# omui.lang.ts

omui's TypeScript over the tool kernel's ([mlibs.ts](../../../../base/ts): types blanked, data modules evaluated): omui's data formats, the declarations of components and native modules, and the edits of a view's builder calls (D-138, D-139, D-157).

TypeScript is the one upper-level language of omui: code, config and data at once. It is a package of its own (D-99) because both the runtime (omui.lang.js) and the build-time generator (omui-gen) read omui's TypeScript, and the generator must not depend on the runtime.

## Data formats (`:define`)

`evaluate(file)` is mlibs.ts's with omui's two define modules, by layer (D-157): `"openmui/define"` has the spec's formats (`defineContract`, `defineNative`, `defineView`), `"omui/define"` the implementation's (`defineConfig`, `defineOps`, `defineTheme`, `defineStyle`, `defineMotion`, `defineMatter`, `defineProject` …). `omui_define_types()` gives the TypeScript declarations of `"omui/define"`; `"openmui/define"`'s are the spec's (`spec/stubs/define.d.ts`).


## Declarations (`:declare`)

`declare_components(shapes, options)` and `declare_natives(shapes, options)` write the TypeScript declarations of components' builders and native modules, the same text for omui-gen, `design.api` and an application's `.omui/types`.

## Edits of builder calls (`:edit`)

`builder_call_at`, `set_property`, `remove_property`, `insert_child` and `remove_call` find and edit the builder call at a line and column of a view's source, the rest of the text as it was (Live Design). A failure is an `EditError` on the error contract (`omui.ts.no-call`, `omui.ts.no-place` …).
