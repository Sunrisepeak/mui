# voice — the announcer

The announcer's calls, from Kenney's [Voiceover Pack: Fighter](https://opengameart.org/content/voiceover-pack-fighter-40-taunts) by Kenney Vleugels ([kenney.nl](https://kenney.nl)), released under [CC0 1.0](http://creativecommons.org/publicdomain/zero/1.0/).

Each call was converted from the pack's Ogg Vorbis to 22.05 kHz mono 16-bit WAV with its silences trimmed and its rumble under 70 Hz taken off (`ffmpeg … silenceremove … highpass=f=70`). `tie.wav` is the pack's `it's_a_tie`. They are processed into the announcer by `sounds/voice.sound.omui.toml` (`openmui.audio`'s recording layers).
