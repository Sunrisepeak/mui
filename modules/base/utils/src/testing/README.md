# mlibs.testing

The checks openmui's test programs share: `Suite` (named cases, the exit code is the verdict), `check` / `check_eq` / `check_bits` / `check_near` with `std::source_location`, and `Hash` for pinned determinism hashes. No macros. It is a directory of `mlibs.utils`; lint `testing` keeps `import mlibs.testing` in test programs (`tests/`), so no artifact carries it.

The package and public import are `mlibs.testing`.