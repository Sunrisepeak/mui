# mlibs.i18n.cldr

Optional embedded CLDR 49 locale data for `mlibs.i18n`: English, Simplified
Chinese, Traditional Chinese and their likely subtags. The i18n library does
not implicitly include this pack; applications select it explicitly.

```cpp
import mlibs.i18n;
import mlibs.i18n.cldr;
mlibs::i18n::Library library;
mlibs::i18n::cldr::add_to(library);
```

The existing `data/*.locale.omui.json` files are embedded as module partitions
in `mlibs.i18n.cldr.api`; their content, format and CLDR version are unchanged.
The generic build embed plugin remains `openmui.build.embed` until its own
migration. Its host tool is a build-time dependency, not a UI runtime dependency.

The original `openmui.i18n.cldr` import and `omui::i18n::cldr` access share the
same cached packs after rebuilding. Use `mlibs.i18n.cldr` for package selection.
Data uses the existing Unicode-3.0 license; the package also contains Apache-2.0 code.
