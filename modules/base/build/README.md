# mlibs.build

The foundation's build plugins (D-73, D-231): what any package's build program shares, and the embed plugin for data files a program carries. omui's build plugins ([mcpp/build](../../../mcpp/build)) build on it; a package below omui uses it directly, so nothing below omui needs omui to build.

```cpp
// build.mcpp — [build-dependencies] mlibs.build = { path = "…/modules/base/build", host-module = true }
import mcpp;
import mlibs.build.embed;
int main() {
    return mlibs::build::embed({.module = "mlibs.i18n.cldr.api", .ns = "mlibs::i18n::cldr", .dir = "data",
                                .suffix = ".locale.xdef.json", .tag = "locale"}) ? 0 : 1;
}
```

| Module | What |
|---|---|
| `mlibs.build.plan` | `edge` (a command, its inputs and outputs, and for a generated module unit what it provides and imports: a file that does not exist yet cannot be scanned); `submit()` hands edges to mcpp as source actions; `tool(package, program)`, a program a dependency builds for the build; `utf8()`, `generic_utf8()`, `namespace_of()`, `files_ending()`, `files_under()`, `is_packed()` |
| `mlibs.build.embed` | `embed()`: each `<stem><suffix>` in `dir` becomes the partition `<module>:<stem>_<tag>` holding its text (`xdef embed`); with `.index = true` also `<module>:<tag>_files`, a table of them all by name; with `.left_out = true` none, and an empty table |

A TypeScript data module (`*.xdef.ts`) is carried as the JSON it evaluates to with its owner's kinds, so its owner's plugin carries it (omui's `omui.build.embed`, which plans these edges and points those files at omui-gen); `embed()` here refuses them.

Beside mcpp's own `mcpp.tools.embed` (mcpp-plugins): that one writes a header carrying an array (and, with `group()`, a generated module over the headers). This one writes a module partition holding a `std::string_view` and a table of them, which the repository's readers use and its no-headers rule asks for; moving them to mcpp's is open.

## Lower parts

- `mlibs.xdef.tool` (preview): `xdef`, the program the embed edges run.

## Agent surface

A library of build plugins: what it does is a build's edges, which the build tool reports.
