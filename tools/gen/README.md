# openmui.gen (`omui-gen`)

The generator: `spec/registry` and `*.contract.omui.toml` to every derived artifact — `openmui.h`, the `openmui.spec` partitions, reference tables, TS / Python / Lua builders and the view-document schema (implementation plan §4.2).

```bash
omui-gen registry spec/registry --out spec [--check] [--json]
omui-gen stubs <contract>... --registry spec/registry --module <name> --out <dir> [--check]
omui-gen check <contract>... --registry spec/registry [--json]
```

Every problem is reported with file, line and column, a message and a hint; `--json` gives the same as JSON. `--check` writes nothing and fails when an output is out of date.

The `dist-toy` packer accepts `--font-languages NotoSansSC=zh` and `--font-initial NotoSansSC=中文`. Initial coverage is literal UTF-8 text, without the common-character expansion of `--subset`. It needs a selected font with language-based deferred loading; unknown or repeated declarations fail the build. Generated initial and regular font files have independent content hashes and share the same runtime font basename on their respective loading paths.

TrueType subsets retain original glyph ids, metrics and layout tables; their Unicode cmap contains only retained characters. This keeps a small initial subset from claiming glyphs whose outlines were removed and lets the runtime select a later fallback face.

The loader keeps `/fonts/.partial` while language fonts are pending. A full font replaces its canonical `/fonts/` source and is exposed to the Web host through a `/later/fonts/` symlink, so independent font collections also see the full source without duplicating the MEMFS data. The site's reference panel waits for the canonical sources before comparing its replay with CI.
