# 收口清单

每个项目的收口清单（发布方案 §5.1，发布门第 3 条）：稳定与预览级过全部 10 项，实验级过带 ★ 的 4 项。每一格从仓库里读出，由 `omui-devtools closure` 生成，不要手改。✓ 通过，○ 欠着，例外 写明的例外，— 不要求。

| 项目 | 标注 | 1★ 定义与特性 | 2 API 复查 | 3 错误 | 4★ Agent 面 | 5 ops 与 skill | 6★ 测试 | 7 基准 | 8 并排对照 | 9 独立消费 | 10★ 已知局限 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| mlibs | stable | ✓ | ✓ | ✓ | ✓ | 例外 | ✓ | ✓ | ✓ | ○ | ✓ |
| xrender | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ |
| hanim | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ✓ |
| omui | stable | ✓ | ✓ | ✓ | ✓ | ✓ | 例外 | ✓ | ○ | ○ | ✓ |
| xengine | preview | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ○ | ○ | ✓ |
| gongfu | experimental | ✓ | — | — | ✓ | — | ✓ | — | — | — | ✓ |
| xtools | stable | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 例外 | ○ | ○ | ✓ |

通过 55 项，欠 9 项。

## 欠着的

- **mlibs**, 9 独立消费: probe hanim-core-consumer builds from paths; from published packages once RC3.1 publishes them
- **xrender**, 9 独立消费: probe hanim-play-consumer builds from paths; from published packages once RC3.1 publishes them
- **hanim**, 9 独立消费: probe hanim-api-probe builds from paths; from published packages once RC3.1 publishes them
- **omui**, 8 并排对照: no claim in the ledger dated from 2026-11-15
- **omui**, 9 独立消费: probe features-consumer builds from paths; from published packages once RC3.1 publishes them
- **xengine**, 8 并排对照: no claim in the ledger dated from 2026-11-15
- **xengine**, 9 独立消费: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
- **xtools**, 8 并排对照: no claim in the ledger dated from 2026-11-15
- **xtools**, 9 独立消费: no out-of-tree probe (tools/ci/*/mcpp.toml.in)
