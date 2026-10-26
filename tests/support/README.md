# openmui-testing

The checks openmui's test programs share: `Suite` (named cases, the exit code is the verdict), `check` / `check_eq` / `check_bits` / `check_near` with `std::source_location`, and `Hash` for pinned determinism hashes. No macros. Members list it under `[dev-dependencies]`, so it is part of no artifact.
