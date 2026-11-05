# xengine.render.cts

Desktop device conformance tests for the existing render graph and vector
renderer. This is a tests-only workspace member: no empty library, exported
module or second rendering implementation is added. Run:

```sh
OMUI_REQUIRE_GPU=1 mcpp test -p xengine.render.cts --locked
```

The programs retain the original graph-device, CPU/GPU image comparison,
paint-mixture and effect-output-bound assertions. GPU tests use SDL's actual
device path. Lavapipe is software Vulkan and must be reported as such; it is
not evidence of physical-GPU performance. The existing `OMUI_VULKAN_LIBRARY`
and `OMUI_GPU_VIDEO_DRIVER` overrides still apply. `OMUI_REQUIRE_GPU=1` makes
missing devices fail, as in CI. No driver leak suppression is introduced.

Portable CPU tests and image hashes remain in `xengine.render` and
`xengine.render.vector`. Their manifests declare Emscripten support; this
package declares Linux/macOS/Windows only. Native `mcpp test --workspace`
includes this member, while `tools/ci/web.sh` selects the portable packages
by their declared platforms. Thread-only CPU phases use the existing
`omui::testing::threads` constant; all serial Web cases and pins remain.

This separation follows mcpp 2026.11.2.2's actual manifest contract. It supports
`target.<selector>.dev-dependencies`, but has no conditional `test.discover`:
`modules/manifest/src/toml.cppm`'s accepted target tables omit `test`, and
`src/build/test_targets.cppm` discovers one manifest test set without a target
argument. Its e2e tests 745 and 836 exercise members consisting only of a
package manifest and `tests/`. Consequently platform-specific test programs
live in a platform-specific member rather than being hidden by unsupported
keys or a weaker Web lint rule.

The independent manifest isolates actual desktop/SDL device tests from portable
runtime consumption and Web test discovery. It is a platform-specific validation
project, not a runtime library or another renderer. There is no new forwarding
package for the old test selector: use `-p xengine.render.cts`.
