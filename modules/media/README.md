# modules/media

Media that is not drawn: sound now, video and timelines later (D-86).

Depends on: [base](../base) (hanim's deterministic math, trace). Used by: [ui](../ui), mgame and the works.

| Package | |
|---|---|
| [audio](audio) | Sounds and music as data (`*.sound.omui.toml`, `*.music.omui.toml`: synthesised instruments, patterns as text, layers by intensity), voices mixed into buses through their effects, a block at a time with nothing allocated; offline renders the same bits everywhere; the `omui-music` tool |

Start with [audio](audio/README.md).
