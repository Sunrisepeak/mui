# omui.gen (`omui-gen`)

The generator: `spec/registry` and `*.contract.omui.toml` to every derived artifact — `openmui.h`, the `openmui.spec` partitions, reference tables, TypeScript builders and the view-document schema (implementation plan §4.2).

```bash
omui-gen registry spec/registry --out spec [--check] [--json]
omui-gen stubs <contract>... --registry spec/registry --module <name> --out <dir> [--check]
omui-gen check <contract>... --registry spec/registry [--json]
```

Every problem is reported with file, line and column, a message and a hint; `--json` gives the same as JSON. `--check` writes nothing and fails when an output is out of date.

The `dist-toy` packer accepts `--font-languages NotoSansSC=zh` and `--font-initial NotoSansSC=中文`. Initial coverage is literal UTF-8 text, without the common-character expansion of `--subset`. It needs a selected font; unknown or repeated declarations fail the build. With a language rule, matching visits load the regular face directly and other visits start with the initial face. Without a language rule, every visit starts with the initial face. Generated initial and regular font files have independent content hashes and share the same runtime font basename on their respective loading paths.

TrueType subsets retain original glyph ids, metrics and layout tables; their Unicode cmap contains only retained characters. This keeps a small initial subset from claiming glyphs whose outlines were removed and lets the runtime select a later fallback face.

Before hashing each delivered TrueType face, the packer removes its stored
PostScript glyph names using `compact_font_post` from
`openmui.gen.font_subset`. It keeps the other `post` header fields, every
unrelated table payload and physical table order; it recalculates the SFNT
checksums. Source fonts and licences are unchanged. Already-post-3 and
missing-post inputs are byte-identical; invalid directory/checksum/post data,
unsupported versions and changes to signed faces produce an error. This is a
delivery optimization for consumers addressing glyphs by ID, not a general
font-validity check or a glyph-name-preserving export. The real packer CTS
compares metadata, variable-weight shaping and glyph pixels against the
original Inter and Mono faces.

The loader keeps `/fonts/.partial` while language fonts are pending. A full font replaces its canonical `/fonts/` source and is exposed to the Web host through a `/later/fonts/` symlink, so independent font collections also see the full source without duplicating the MEMFS data. The site's reference panel waits for the canonical sources before comparing its replay with CI.

`closure <script or replay>… --out <unit.cpp> --report <closure.json> [--graph FILE] [--config omui.config.ts]` derives what an application carries from what it wrote (B7b; `omui.gen.closure`). It reads the names its scripts, and every step of its replay, take from `openmui` (`mlibs::ts::imports`). It writes them as the registry the player's `closure` entry calls (`omui_application_components()`: `omui::register_base_named<…>()`) and as `closure.json`. Every component stays in three cases, and the report says why: a script takes everything of `openmui`, the build links the designer, or `omui.config.ts` opens generation to components or surfaces. A file is written only when its text changes.

`dist-toy-split <package> --primary <wasm> --deferred <wasm> [--lazy <route>,…]` takes a packed page's program cut in two by Binaryen's wasm-split (`omui-devtools site split` profiles the first screen in headless Chromium and cuts by it): the first half replaces the package's `.wasm`, the second lands beside it as `<target>-<hash>.deferred.wasm`, and the page's `var omuiDeferred` says where it is, its size and the routes whose first screen the first half covers. The loader adds the placeholder imports itself (no `-sSPLIT_MODULE` build): on those routes the second half is compiled after the first frame, elsewhere main waits for it; a placeholder called before it is ready fetches it synchronously, the fallback. `window.omuiDeferredState` says where it is (`none`, `waiting`, `fetching`, `compiled`, `instantiated`, `error`).

`dist-toy --load eager|idle|interaction` says when the page takes its program (D-260): at once (the default); once the page has loaded and is idle; or when the reader first operates something only the program answers (a button, a field) or the page calls `omuiLoad.start()`. Until then the page is its document — reading (scrolling, the browser's find, a selection, links) never takes the program — the load state's phase is `waiting`, and the press that took it is kept for the program to answer once it has presented (`omuiLoad.takePress`). `--background-dark #rrggbb` is the page's colour where the person prefers a dark appearance.

`dist-toy-document <package> --static-html <file> [--lang L] [--description T] [--canonical URL] [--image URL] [--alternate HREFLANG=URL …]` makes a packed page a document before its program runs (D-236): the application's first screen, its semantic tree projected (`<program> --static-html FILE`), goes into the body before the canvas and over its place — readable text, scrolling and selection, in a colour that reads on the page's background — and steps aside (`hidden`) on the load event that says the program presented its first frame, the Web host's mirror reading the page from then on; the head gains the description, the canonical address, the page in other languages (`hreflang`) and a share card (Open Graph, `twitter:card`). Once a page; a projection holding a script is refused; a Toy page keeps Toy's 1 MiB limit.


## Lower parts

- `mlibs.xdef.tool` (preview): the embed emitters `omui-gen embed` writes through (`mlibs::xdef::emit_embed`, `emit_embed_index`; one owner of both, with `xdef embed`), its header naming omui-gen (`omui::gen::embedded_by`).
