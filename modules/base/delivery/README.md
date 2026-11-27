# mlibs.delivery

A built directory as a static host sends it: every file's bytes and its bytes as gzip, what a visitor downloads (compressed where the host compresses the kind, as stored where not), budgets over patterns of paths and their verdicts — a budget that states no most is a share: counted and held to nothing — and the manifest of it all as data (`delivery@1`).

Label: **preview**. A tool's shared library (0.1.0 plan RL7 §3.2: T1 of the toolchain): `oweb` writes a site's manifest with it; the repository's tool serves and measures with it (its Pages-like server's gzip, `check takeover`, `check dist-toy-loading`, the site's split); `check size` and `omui build` adopt it with RL8. Its own package because zlib is a dependency a library below it should not carry (D-99: dependency isolation).

```cpp
const auto manifest = mlibs::delivery::measure("dist", std::array{
    mlibs::delivery::Budget{"a page readable", "**/index.html", mlibs::delivery::Measure::sent, mlibs::delivery::Over::each, 100'000}});
if (manifest && !manifest->within()) …   // each verdict: what was found, in which file, against what
std::string data = mlibs::delivery::to_json(*manifest);
```

- **Sent, not stored**: GitHub Pages serves gzip only, so a budget is over gzip bytes for HTML, Markdown, JSON, scripts, styles, SVG, XML, text, WebAssembly and TrueType fonts, and over stored bytes for what is packed already (pictures, WOFF2).
- **Budgets**: a pattern (`*` within a directory, `**` across them, `?` one character), a measure (`bytes`, `gzip`, `sent`), over `each` file (the verdict names the largest) or their `sum`.
- `gzip` and `gunzip` are zlib's through its wrap (`mcpplibs.mwrap.zlib`), in a `native/` unit.

## Agent surface

A library: no subject of its own. Its manifest is the data the tools that use it answer with (`oweb site.build`'s `delivery`), so an agent reads the same numbers a check holds.

## Tests

`tests/delivery_test.cpp`: gzip there and back and by level; the patterns; a directory measured — both sizes, what is sent, budgets each and summed, a verdict out of budget, the manifest's JSON.
