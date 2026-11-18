# Closure

Each project's closure checklist (release plan §5.1, gate 3): stable and preview projects pass all ten items, experimental ones the four starred. Every cell is read from the repository by `omui-devtools closure`; do not edit. ✓ passed, ○ owed, exempt an exception said, — not required.

| Project | Label | 1★ definition and features | 2 API review | 3 errors | 4★ agent surface | 5 ops and skill | 6★ tests | 7 benchmarks | 8 side by side | 9 independent use | 10★ known limitations |
|---|---|---|---|---|---|---|---|---|---|---|---|
| mlibs | stable | ✓ | ✓ | ✓ | ✓ | exempt | ✓ | ✓ | ✓ | ○ | ✓ |
| xrender | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ |
| hanim | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ |
| omui | stable | ✓ | ✓ | ✓ | ✓ | ✓ | exempt | ✓ | ○ | ○ | ✓ |
| meng | preview | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ○ | ✓ |
| gongfu | experimental | ✓ | — | — | ✓ | — | ✓ | — | — | — | ✓ |
| xtools | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | exempt | ○ | ○ | ✓ |

55 passed, 9 owed.

## Owed

- **mlibs**, 9 independent use: probe hanim-core-consumer builds from paths; from published packages once RC3.1 publishes them
- **xrender**, 9 independent use: probe hanim-play-consumer builds from paths; from published packages once RC3.1 publishes them
- **hanim**, 9 independent use: probe hanim-api-probe builds from paths; from published packages once RC3.1 publishes them
- **omui**, 8 side by side: no claim in the ledger dated from 2026-11-15
- **omui**, 9 independent use: probe features-consumer builds from paths; from published packages once RC3.1 publishes them
- **meng**, 8 side by side: no claim in the ledger dated from 2026-11-15
- **meng**, 9 independent use: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
- **xtools**, 8 side by side: no claim in the ledger dated from 2026-11-15
- **xtools**, 9 independent use: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
