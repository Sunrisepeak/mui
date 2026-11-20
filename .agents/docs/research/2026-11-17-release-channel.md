# The release channel (RC3.1): facts, 2026-11-17

Facts checked against mcpp 2026.11.16.1 (its `docs/` and `src/`), the mcpp-index checkout and its local copy
(`~/.mcpp/indices/mcpplibs`), xim-pkgindex, and this repository at `9105f5bb`. Goal of RC3.1: on a clean machine one
command installs omui and creates a project; the templates resolve their dependencies from published releases.

## 1. How a library enters mcpp-index

- **Repository**: `github.com/mcpplibs/mcpp-index` (the URL in mcpp's default config). Descriptors are Lua files,
  `pkgs/<initial of the full name>/<name>.lua`; the file name plays no part in resolution, the declared identity does.
- **Required fields** (Form A, the source carries its own `mcpp.toml`): `spec = "1"`, `namespace` (dotted path),
  `name` (one segment: `namespace = "omui", name = "core"`; `name = "omui.core"` is the older shape, still accepted,
  the index is migrated to the short one), `description`, `licenses`, `repo`, `type = "package"`, `xpm` with a
  `linux`, `macosx` and `windows` block, each version (bare, no `v`) with `url = { GLOBAL = …, CN = … }` and `sha256`.
  A version missing from one block reads as "not found" on that platform. `mcpp xpkg parse FILE` is the checker CI uses.
- **Flow** (mcpp `docs/11-publishing-a-library.md`): tag → archive → byte-identical CN mirror on GitCode `mcpp-res`
  (`gtc release publish`; without write access a plain-string GLOBAL URL is accepted and maintainers add the mirror) →
  PR adding the descriptor **and a consumer project** under `tests/examples/<member>/` (workspace member, a failing
  assertion possible) → CI green → merge → `publish-artifact.yml` republishes the index artifact → cold resolve.
- **Precedents for several packages of one repository**:
  - *Archive route* (`mtest`): every descriptor points at the repository's tag tarball and names its member's
    manifest, `mcpp = "*/manifest/mcpp.toml"`. Each package downloads and extracts the whole tarball
    (`~/.mcpp/xpkgs/mcpplibs.mtest-x-manifest/0.6.3/mtest-0.6.3/` holds the full repository).
  - *Member route* (`mcpp publish` in a member, mcpp 2026.9.25.1+): the archive holds the member's directory alone;
    inherited workspace values are written out; a sibling `path` edge must state `version` beside it and is published
    as that version; `path` dev-dependencies are dropped. Release-asset precedent: `pkgs/t/tensorvia-cpu.lua`.
- **Archive route, for this repository**: a GitHub tag tarball is 33 MB (`git archive HEAD | gzip`) and carries no
  submodule, so `spec/` (`openlifa.openmui`) is absent; path edges between members stay `path` edges into each
  package's own extraction, so two packages a project names reach the same member at two directories (a warning, the
  first wins); `mcpp new --template` takes the archive's top-level `mcpp.toml` as the package root and ignores the
  descriptor's `mcpp = "*/sub/mcpp.toml"` (`src/scaffold/create.cppm`, `fetch_template_package`), so it would look
  for `templates/` at the workspace root.
- **Member route, measured** (`mcpp publish --dry-run --allow-dirty`): `modules/base/math` packs to a 26 KB archive;
  `modules/projects/omui/native` is refused: "`mlibs.unicode` reaches the workspace member … by path … State the
  version … `{ path = "../../../base/unicode", version = "0.1.0" }`". The archive and its URL use the short name and
  the tag `v<version>`: `…/releases/download/v0.1.0/math-0.1.0.tar.gz` (`src/pm/publisher.cppm`,
  `release_tarball_url`); the emitted descriptor writes `name = "mlibs.math"` (the older shape) into `math.lua`.
- **A project's own index**: `[indices] <namespace> = { path = … }` or a git URL redirects one namespace to another
  index (mcpp-index `docs/repository-and-schema.md`); a member that consumes another namespace replaces the inherited
  table. The build sandbox fetches no `file://` or loopback URL; before a release, archives are seeded into
  `~/.mcpp/xpkgs/<ns>-x-<name>/<version>/` by hand (docs/11 "Testing against an unreleased version").
- **A git dependency** names a member of a repository's workspace by identity at one revision
  (`omui.sdk = { git = URL, tag = … }`, mcpp 2026.9.16.1+), and a member's `path` edge inside the clone is the same
  source — a channel with no index at all, but `mcpp new --template` reads the index only.

## 2. How a program enters xlings' index

- **Repository**: `github.com/openxlings/xim-pkgindex` (more index repositories: `xim-pkgindex-awesome`, `-d2x`,
  `-scode`). Flow (`docs/10-adding-a-package.md`): an "Add XPackage" issue → `pkgs/<x>/<name>.lua` →
  `tests/<x>/test_<name>.py` → an install in an isolated `XLINGS_HOME` on every platform claimed → PR, merged when
  Linux, macOS, Windows and the index checks pass. Commit subject `feat(pkg): add omui 0.1.0`.
- **Format**: XPackage spec 2 — `name`, `description`, `licenses`, `repo`, `type = "package"`, `archs`, `status`,
  `programs = {"omui"}`, `xvm_enable = true`, `xpm.<linux|macosx|windows>.<version>` with `url` (GLOBAL/CN) and a
  `sha256` per architecture, `["latest"] = { ref = "0.1.0" }`; `deps = { runtime = { "xim:mcpp" } }` for what must be
  on PATH. Official binaries mirror to `xlings-res/<name>` (GitHub and GitCode).
- **Precedents**: `pkgs/m/mcpp.lua` (spec 1 with `install()`/`config()` hooks, per-arch archives on
  `xlings-res/mcpp`); `pkgs/m/mtest.lua` (spec 2, one archive per version, a sha256 per architecture).
- **Generator**: `mcpp emit xpkg --package <archive> [--package …] --url <release prefix>` writes the spec 2 entry from
  the packages `mcpp pack` wrote (their `mcpp-package.json`); a `polyglot` single file is refused, one system's tree
  or single file is accepted. `.github/workflows/omui.yml` already packs `omui.cli` for Linux x64/arm64, macOS arm64
  and Windows x64 (`--mode self-contained --pkg-format single-file`), writes SHA256SUMS and releases on `omui-v*`.

## 3. Prebuilt module artifacts

`mcpp pack <lib target>` writes an ordinary package: `mcpp.toml`, `interface/` (module interface units the consumer
compiles), `include/`, `lib/<triple>/`, tagged `x86_64-linux-gnu-gcc16-libstdcxx16-c++23`; an index entry for it has
the same shape as a source package's. BMIs are not shipped ("compiler-build-exact"); `path` and `git` dependencies are
dropped, so every dependency must be published too (mcpp `docs/12-binary-distribution.md`). A consumer of omui
therefore compiles every interface unit of the closure in either form; prebuilt libraries save the implementation
units only.

## 4. The templates' closure (`omui-devtools release index --list`)

Roots (7, from `modules/projects/omui/templates/*/mcpp.toml.in` rendered as mcpp renders them): `omui.sdk`,
`omui.build`, `omui.native`, `omui.entry.player`, `omui.player`, `omui.lang.js`, `omui.theme`. Closure over every
non-development dependency table, every platform and every feature (what `mcpp publish` must find published):
**67 packages**, 62 of them built with the features a template's project uses, 5 reached only through another feature
(`mlibs.cts`, `xengine.matter`, `xengine.particles`, `xengine.field`, `omui.designer`).

| Project | Packages | Stable | Preview | Experimental | Unlabelled |
|---|---|---|---|---|---|
| omui | 26 | 23 | 3 (`generate`, `host.android`, `designer`) | — | — |
| mlibs | 23 | 23 | — | — | — |
| xrender | 9 | 9 | — | — | — |
| xengine (kernel) | 4 | — | 2 (`dynamics`, `matter`) | 2 (`particles`, `field`) | — |
| hanim | 3 | 3 | — | — | — |
| mpal | 1 | — | 1 | — | — |
| openlifa (`spec`) | 1 | — | — | — | 1 |

- 311 path edges inside the closure carry no `version`; `mcpp publish` refuses each.
- Short names shared by two packages of the closure: `agent`, `core`, `markdown`, `player`, `text`, `ts` — their
  `mcpp publish` archives would collide under one release tag (`core-0.1.0.tar.gz`).
- No manifest or `build.mcpp` in the closure names a path outside its directory except through dependencies (strings
  starting `../`; sources and tests not scanned).
- External dependencies: 21, every one with its version present in the local mcpp-index copy (`mcpplibs.os@0.4.4`,
  `mcpplibs.mwrap.*` ×14, `nlohmann.json@3.12.0`, `marzer.tomlplusplus@3.4.0`, `openlifa.openas@0.2.0`,
  `taskflow.taskflow@4.1.0`, `mcpplibs.cmdline@0.0.3`, `mcpplibs.mtest@0.8.4`).

## 5. What blocks each step

| Step | Blocker | Where it is solved |
|---|---|---|
| Libraries, member route | 311 unversioned sibling edges | here (`version` beside each path), or mcpp: take the sibling member's version when absent (the refusal already reads it) |
| | archive names collide (6 short names); tag fixed to `v<version>` | mcpp: archive and URL by full identity, tag configurable (`--tag`) |
| | 67 `mcpp publish` runs, one per member | mcpp: `mcpp publish -p … / --workspace` (or a devtools loop over the closure) |
| | emitted descriptor uses `name = "<ns>.<name>"` in `<name>.lua` | mcpp: emit the short form in `<ns>.<name>.lua` (index convention) |
| | `spec` is a submodule of another repository | publish `openlifa.openmui` from `openlifa/openmui` with its own tag |
| mcpp-index PR | one consumer test per package; 67 descriptors | ask maintainers to accept one consumer per template (`tests/examples/omui-app-ts` …) covering the closure, or an omui index via `[indices]` |
| CN mirror | write access to GitCode `mcpp-res` and `xlings-res` | maintainers, or plain GLOBAL URLs first |
| Templates | `mcpp new --template` looks in `<package root>/templates/`; the templates live outside any package | here: move to `modules/projects/omui/sdk/templates/` |
| | `omui new` runs `mcpp new NAME --template openmui:<t>`, which selects `mcpplibs.openmui` | here (`omui.cli`): `omui.sdk@<omui's version>:<t>` |
| | the native template's `contracts = ["native/*.native.omui.toml"]`; its contract is `words.native.xdef.ts` | here |
| `xlings install omui` | no `pkgs/o/omui.lua`; no `xlings-res/omui` mirror; `omui.yml` has no `mcpp emit xpkg` step | here (workflow step) and xim-pkgindex PR |
| Release gate §8 item 8 | `check templates` builds against the checkout only | here: a `--published` mode once the index carries 0.1.0 |

## 6. The template side (design; the first two points implemented in `tools/devtools`)

1. **A template states what a user's project states**: published versions only. It already does
   (`"{{self.name}}" = "{{self.version}}"`, `omui.native = "{{self.version}}"`, `omui.build = { version = …,
   host-module = true }`); the members of the closure share one version, the SDK's, so `{{self.version}}` is right for
   every omui-side package as long as they stay in lockstep.
2. **One substitution step for the checkout**: `check templates` renders a template as mcpp does, then
   `release::pointed_at` turns each dependency that names a workspace member (read from the members' manifests, not a
   fixed list) into that member's `path` under the checkout. A template that names a new package needs no change to
   the check. `release::templates_directory` and `release::template_package` are the one place both read.
3. **The templates move into the package that carries them**: `modules/projects/omui/sdk/templates/`, so that the
   member archive of `omui.sdk` holds them where `mcpp new --template omui.sdk:<t>` looks. One constant in devtools,
   the two lint paths (`lint_closures.cpp`, `lint_manifests.cpp`), `omui.cli`'s comment and docs follow.
4. **`omui new` names the SDK at its own version**: `mcpp new NAME --template omui.sdk@<omui's version>:<t>`, so the
   CLI and the libraries a project gets are one release.
5. **The gate from published releases** (§8 item 8): `check templates --published` skips step 2 and builds against
   the index; before the index carries a version, the archives are seeded into `~/.mcpp/xpkgs` as mcpp docs/11 shows.
