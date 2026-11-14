# mlibs.testing

What openmui's test programs share beyond their test framework, which is mcpplibs.mtest (D-147: `UTest`, `"name"_test`, `expect`, `eq`, `near` …):
- `scratch_dir(name)`: a directory whose files stay after the run for a person to look at. README media are read from it.
- `threads`: the target's constants, chosen by the manifest per target.
- `Hash`: FNV-1a for the pinned determinism hashes.

It has no checks, cases or suites; those were moved to mtest by `tools/dev/to_mtest.py`. It is a directory of `mlibs.utils`. The lint rule `testing` keeps `import mlibs.testing` in test programs (`tests/`), so no artifact carries it.

The package and public import are `mlibs.testing`.