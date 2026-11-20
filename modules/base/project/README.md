# mlibs.project

A project as the tools see it, and its health: the tool kernel's project model and doctor (stage 0 of D-205; D-207). It reads a project's manifests — the root's and, in a workspace, every member's — into packages (name, profile, dependencies, own files), and runs rules over them. Every finding names its rule, its place and its fix.

```cpp
auto project = mlibs::project::read(".");
auto findings = mlibs::project::check(*project, std::array{mlibs::project::general(), mlibs::project::workspace()});
```

## Profiles

A **profile** is a set of rules over the same model. The **general** rules hold for every project; the **workspace** rules for a workspace; a domain's owner declares its own as data, a `<owner>.profile.xdef.ts` (`defineProfile` from `mlibs/define`) whose rules are conditions over a package — `metadata` keys said, a `file` matching a glob, `dependsOn` a package, or `anyOf` them — each with its doc and its fix: omui's `app` and `extension` (omui.cli, carried by `omui doctor`), hanim's `film` (hanim.studio), meng's `game` (meng.runtime). `profiles_in(root)` reads every profile a tree declares; `omui doctor` runs omui's and the project's own. A package says its profile in `[package.metadata.openmui] profile = "app"`; `kind = "app"`, as omui's templates write it, says it too. A profile's rules look only at the packages that say it.

| Rule | What it holds |
|---|---|
| `general.manifest` | a package's one project file is `mcpp.toml` (no CMakeLists.txt, package.json, meson.build …) |
| `general.path-name` | a member's directory is its name's last part (an ordering prefix such as `02-` aside; a submodule is its own repository's) |
| `general.module-name` | a library's primary module is its package's name, or the manifest says which it is (`primary-module = "omui"`) or that it is a collection (`"*"`) |
| `general.tests` | tests are in `tests/` and written with mtest |
| `general.data-names` | a declared data file is `<name>.<kind>.xdef.<ts|json|jsonl>`; the earlier suffix is `xdef migrate`'s |
| `workspace.members` | every member listed has a manifest; every manifest under the root is listed |

The repository's examples and showcases are the corpus: its tests hold them to the general rules, and omui.cli's tests to omui's profiles (`omui doctor`).
