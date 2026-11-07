# The default fonts

Part of rendering determinism (D-48): the default text of openmui renders with these files on every platform, never with whatever the system has.

| File | Font | Coverage | Licence |
|---|---|---|---|
| `Inter.ttf` | Inter (variable: opsz, wght) | Latin, Greek, Cyrillic — the UI face | SIL OFL 1.1 (`OFL-inter.txt`) |
| `JetBrainsMono.ttf` | JetBrains Mono (variable: wght) | code | SIL OFL 1.1 (`OFL-jetbrainsmono.txt`) |
| `NotoSansArabic.ttf` | Noto Sans Arabic (variable: wdth, wght) | Arabic | SIL OFL 1.1 (`OFL-notosansarabic.txt`) |
| `NotoSansSC.ttf` | Noto Sans SC (variable: wght), subset to GB 2312 (6763 hanzi) and CJK punctuation | Simplified Chinese | SIL OFL 1.1 (`OFL-notosanssc.txt`) |
| `TwemojiMozilla.ttf` | Twemoji Mozilla 0.7.0 (COLR colour glyphs) | emoji | code Apache 2.0, graphics CC BY 4.0 (`LICENSE-TwemojiMozilla.md`) |

Sources: google/fonts (`ofl/inter`, `ofl/jetbrainsmono`, `ofl/notosansarabic`, `ofl/notosanssc`), mozilla/twemoji-colr release v0.7.0. SHA-256:

```
29160a80ff49ddcab2c97711247e08b1fab27a484a329ce8b813d820dc559031  Inter.ttf
48715a42ec242c21e9f02692891e147d022299a52e48d5e413e1a942193ffeda  JetBrainsMono.ttf
63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e  NotoSansArabic.ttf
f4a4f44e6d3c9a557ad4d467d7fb647a560a462e5919212e125329a33fdd4b38  NotoSansSC.ttf
6d90152ee0d29e82fe2a87793af5aa4b7ad13e6538360889e141e81ed299ee8e  TwemojiMozilla.ttf
```
