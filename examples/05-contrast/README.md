# Example 05 — a native module: a contrast checker

`contrast` checks a text colour against a background as WCAG 2.2 measures it. It shows:

- the ratio, and the levels it meets (AA, AA large, AAA, user-interface components);
- a sample at both text sizes;
- the pair as each colour-vision deficiency shows it;
- the nearest colour, in perceived lightness, that passes AA. A worker finds it, so typing never waits.

The interface is TypeScript ([`logic/main.ts`](logic/main.ts)), run by the player. The colour science is the application's own native module (component-binding §8):

- [`native/contrast.native.omui.toml`](native/contrast.native.omui.toml) is the contract: three functions and two records.
- [`src/contrast.cpp`](src/contrast.cpp) is the C++ that implements it: WCAG relative luminance, Machado, Oliveira and Fernandes (2009) for the deficiencies, OKLab for the suggestion.
- [`build.mcpp`](build.mcpp) runs `openmui.build.contracts`' `natives()`, which generates the partition `colors:contrast_native` and the TypeScript, Python and Lua stubs when the package builds.
- [`src/main.cpp`](src/main.cpp) registers the module, then hands over to the player.

Nothing between the two languages is written by hand. The script imports `openmui:native/contrast`. `check` and `simulate` answer at once. `suggest` is `async`: it returns a promise and runs on a worker.

```sh
mcpp run -p openmui.example.contrast
mcpp run -p openmui.example.contrast -- --headless --png contrast.png
```

The test (`tests/contrast_test.cpp`) checks:
- the module's answers against WCAG's values (21:1, #777 on white at 4.47:1);
- that a suggestion meets the target it was asked for;
- the interface as a person uses it: typing a colour, a half-typed one that keeps the last answer and says why, the worker's suggestion;
- its pinned pixels.

Ratios are shown to two decimals and never rounded up past a threshold: 4.499 reads 4.49:1, as it fails 4.5.
