# voice — the announcer and the fighters' voices

Recordings played by `openmui.audio`'s recording layers, all CC0 ([CC0 1.0](http://creativecommons.org/publicdomain/zero/1.0/)):

| Files | From |
|---|---|
| `fight`, `round_1`–`round_3`, `final_round`, `time`, `player_1`, `player_2`, `winner`, `flawless_victory`, `tie`, `choose_your_character` | Kenney's [Voiceover Pack: Fighter](https://opengameart.org/content/voiceover-pack-fighter-40-taunts) ([kenney.nl](https://kenney.nl)); `tie` is its `it's_a_tie` |
| `female_1`–`female_7` | [Female Hurt Grunts & Groans](https://opengameart.org/content/female-hurt-grunts-groans), its takes |
| `strain_1`, `strain_3`, `strain_5`, `strain_6`, `strain_8`, `strain_9`, `strain_11`, `strain_12`, `strain_13`, `strain_14` | [15 vocal male strain/hurt/pain/jump sounds](https://opengameart.org/content/15-vocal-male-strainhurtpainjump-sounds) (`slightscream-NN`) |
| `pain_1`, `pain_12` | [grunts of male death and pain](https://opengameart.org/content/grunts-male-death-and-pain), its first and twelfth takes |

How they were made: the packs' Ogg Vorbis and FLAC converted to WAV; a file of many takes split with `omui-music takes` (which also measured each take's length, pitch, how voiced and how bright, to choose by); each then converted to 22.05 kHz mono 16-bit with its rumble under 70 Hz taken off (`ffmpeg … highpass=f=70`; the announcer's silences trimmed by `silenceremove`). `sounds/voice.sound.omui.toml` makes the announcer of them, `sounds/fighters.sound.omui.toml` each fighter's voice.

The takes were chosen by measurement, not by ear: low and dark ones for 坤's restraint and, slowed further, 岳's weight; short bright ones for a cry. 墨, 霓, 璃 and 刻 share one actress, told apart only by processing; more voices are wanted.
