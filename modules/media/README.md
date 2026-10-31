# modules/media

Media that is not drawn: sound, video, and the timeline every editor shares (D-86).

Depends on: [base](../base) (hanim's deterministic math, image, trace). Used by: [ui](../ui), mgame and the works.

| Package | |
|---|---|
| [audio](audio) | Sounds and music as data (`*.sound.omui.toml`, `*.music.omui.toml`: synthesised instruments, patterns as text, layers by intensity), voices mixed into buses through their effects, a block at a time with nothing allocated; offline renders the same bits everywhere; the `omui-music` tool |
| [video](video) | Frames and a sound track encoded on exact frame and sample counts: sources (the sync card, PNG files, a host's view), encoders as plugins (a PNG sequence, Y4M with integer BT.709 4:2:0, WAV built in), `*.video.omui.toml` descriptions; the same bytes everywhere |
| [timeline](timeline) | Tracks of clips in exact time (flicks), properties keyed with hanim's curves, what plays when; edits that each give back their exact inverse; the readable `*.timeline.omui.toml` |

Start with [audio](audio/README.md).
