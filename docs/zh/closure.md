# 收口清单

每个项目的收口清单（发布方案 §5.1，发布门第 3 条）：稳定与预览级过全部 10 项，实验级过带 ★ 的 4 项。每一格从仓库里读出，由 `omui-devtools closure` 生成，不要手改。✓ 通过，○ 欠着，例外 写明的例外，— 不要求。

| 项目 | 标注 | 1★ 定义与特性 | 2 API 复查 | 3 错误 | 4★ Agent 面 | 5 ops 与 skill | 6★ 测试 | 7 基准 | 8 并排对照 | 9 独立消费 | 10★ 已知局限 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| mlibs | stable | ✓ | ✓ | ✓ | ✓ | ○ | ○ | ✓ | ○ | ○ | ✓ |
| xrender | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ✓ | ○ | ✓ |
| hanim | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ✓ | ○ | ✓ |
| omui | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ○ | ○ | ✓ |
| xengine | preview | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ | ○ | ○ | ✓ |
| gongfu | experimental | ✓ | — | — | ✓ | — | ✓ | — | — | — | ✓ |
| xtools | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ○ | ○ | ✓ |

通过 47 项，欠 17 项。

## 欠着的

- **mlibs**, 5 ops 与 skill: no operation catalog (*.ops.xdef.ts)
- **mlibs**, 6 测试: 1 package(s) without tests, the first mlibs.shader.rules
- **mlibs**, 8 并排对照: no claim in the ledger dated from 2026-11-15
- **mlibs**, 9 独立消费: probe hanim-core-consumer builds from paths; from published packages once RC3.1 publishes them
- **xrender**, 6 测试: 3 package(s) without tests, the first xrender.particle.gpu
- **xrender**, 9 独立消费: probe hanim-play-consumer builds from paths; from published packages once RC3.1 publishes them
- **hanim**, 6 测试: 2 package(s) without tests, the first hanim.timeline.tool
- **hanim**, 9 独立消费: probe hanim-api-probe builds from paths; from published packages once RC3.1 publishes them
- **omui**, 6 测试: 2 package(s) without tests, the first omui.host.android
- **omui**, 8 并排对照: no claim in the ledger dated from 2026-11-15
- **omui**, 9 独立消费: probe features-consumer builds from paths; from published packages once RC3.1 publishes them
- **xengine**, 6 测试: 2 package(s) without tests, the first xengine.cellular.rhi
- **xengine**, 8 并排对照: no claim in the ledger dated from 2026-11-15
- **xengine**, 9 独立消费: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
- **xtools**, 7 基准: no budget in tests/bench
- **xtools**, 8 并排对照: no claim in the ledger dated from 2026-11-15
- **xtools**, 9 独立消费: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
