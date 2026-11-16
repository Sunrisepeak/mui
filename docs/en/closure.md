# Closure

Each project's closure checklist (release plan §5.1, gate 3): stable and preview projects pass all ten items, experimental ones the four starred. Every cell is read from the repository by `omui-devtools closure`; do not edit. ✓ passed, ○ owed, exempt an exception said, — not required.

| Project | Label | 1★ definition and features | 2 API review | 3 errors | 4★ agent surface | 5 ops and skill | 6★ tests | 7 benchmarks | 8 side by side | 9 independent use | 10★ known limitations |
|---|---|---|---|---|---|---|---|---|---|---|---|
| mlibs | stable | ✓ | ✓ | ✓ | ✓ | ○ | ○ | ✓ | ○ | ○ | ✓ |
| xrender | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ✓ | ○ | ✓ |
| hanim | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ✓ | ○ | ✓ |
| omui | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ○ | ○ | ✓ |
| xengine | preview | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ○ | ○ | ✓ |
| gongfu | experimental | ✓ | — | — | ✓ | — | ✓ | — | — | — | ✓ |
| xtools | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ○ | ○ | ✓ |

47 passed, 17 owed.

## Owed

- **mlibs**, 5 ops and skill: no operation catalog (*.ops.xdef.ts)
- **mlibs**, 6 tests: 1 package(s) without tests, the first mlibs.shader.rules
- **mlibs**, 8 side by side: no claim in the ledger dated from 2026-11-15
- **mlibs**, 9 independent use: probe hanim-core-consumer builds from paths; from published packages once RC3.1 publishes them
- **xrender**, 6 tests: 3 package(s) without tests, the first xrender.particle.gpu
- **xrender**, 9 independent use: probe hanim-play-consumer builds from paths; from published packages once RC3.1 publishes them
- **hanim**, 6 tests: 2 package(s) without tests, the first hanim.timeline.tool
- **hanim**, 9 independent use: probe hanim-api-probe builds from paths; from published packages once RC3.1 publishes them
- **omui**, 6 tests: 2 package(s) without tests, the first omui.host.android
- **omui**, 8 side by side: no claim in the ledger dated from 2026-11-15
- **omui**, 9 independent use: probe features-consumer builds from paths; from published packages once RC3.1 publishes them
- **xengine**, 6 tests: 2 package(s) without tests, the first xengine.cellular.rhi
- **xengine**, 8 side by side: no claim in the ledger dated from 2026-11-15
- **xengine**, 9 independent use: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
- **xtools**, 7 benchmarks: no budget in tests/bench
- **xtools**, 8 side by side: no claim in the ledger dated from 2026-11-15
- **xtools**, 9 independent use: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
