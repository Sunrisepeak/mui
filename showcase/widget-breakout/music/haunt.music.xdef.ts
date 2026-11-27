import { defineMusic } from "omui/define";

export default defineMusic({
  doc: `The haunted panel's theme: a music box left on in an empty office, a
little out of tune with the dark, and something that hums along. In F sharp
minor — the chain's notes (an A major pentatonic, its pops rising a step a
brick) are the same seven notes, so a long chain sings over it, never
against it.

Intensity (0–1) adds parts at the next bar: below 0.35 the music box, the
pad and a knock somewhere in the walls; from 0.35 the bass and a soft kit;
from 0.6 the full kit and the hum (a theremin an octave under the tune); from
0.85 a celesta over the tune and the crash. The game raises it with the
chain, with the last ball and with several in the air.

112 beats a minute; a two-bar intro, then a loop of 16 bars (A, B) — A over
F#m D Bm C#m, B over A E F#m D, home again on C#m.`,
  name: "haunt",
  tempo: 112,
  meter: [4, 4],
  gain: 0.38,

  patches: {
    // ── patches ──────────────────────────────────────────────────────────────
    music_box: {
      base: "bell",
      fm: { ratio: 3.5, index: 1.4, decay: 0.8 },
      amp: { attack: 0.001, decay: 1.2, sustain: 0, release: 0.4 },
    },

    hum: {
      base: "pulse_lead",
      osc: { wave: "triangle" },
      filter: { cutoff: 1400 },
      vibrato: { depth: 18, delay: 0.25 },
      amp: { attack: 0.08, decay: 0.4, sustain: 0.7, release: 0.35 },
    },

    dark_pad: {
      base: "pad",
      filter: { cutoff: 700, envelope: 1 },
    },

    celesta: {
      base: "fm_pluck",
      fm: { ratio: 4, index: 1.1, decay: 0.25 },
    },
  },

  instruments: {
    // ── instruments ──────────────────────────────────────────────────────────
    box: {
      patch: "music_box",
      gain: 0.8,
      pan: -0.1,
      voices: 10,
      humanize: 0.15,
      sends: { hall: 0.35, delay: 0.18 },
    },

    hum: {
      patch: "hum",
      gain: 0.42,
      pan: 0.15,
      legato: 1,
      voices: 4,
      sends: { hall: 0.45 },
    },

    celesta: {
      patch: "celesta",
      gain: 0.45,
      pan: 0.2,
      voices: 8,
      sends: { hall: 0.3, delay: 0.2 },
    },

    pad: {
      patch: "dark_pad",
      bus: "pads",
      gain: 0.3,
      legato: 1,
      voices: 8,
      sends: { hall: 0.4 },
    },

    bass: {
      patch: "pluck_bass",
      bus: "low",
      gain: 0.8,
      legato: 0.6,
    },

    knock: {
      patch: "woodblock",
      bus: "drums",
      gain: 0.45,
      pan: -0.4,
      transpose: -7,
      sends: { hall: 0.5 },
    },

    kick: {
      patch: "kick",
      bus: "drums",
      gain: 0.5,
    },

    snare: {
      patch: "snare",
      bus: "drums",
      gain: 0.38,
      sends: { hall: 0.25 },
    },

    hat: {
      patch: "hat",
      bus: "drums",
      gain: 0.3,
      pan: 0.25,
      humanize: 0.3,
    },

    crash: {
      patch: "crash",
      bus: "drums",
      gain: 0.26,
      sends: { hall: 0.2 },
    },
  },

  buses: {
    // ── buses ────────────────────────────────────────────────────────────────
    hall: {
      effects: [{ kind: "reverb", size: 0.9, damping: 0.5, predelay: 0.04, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 240 }],
    },

    delay: {
      effects: [{ kind: "echo", beats: 0.75, feedback: 0.35, pingpong: true, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 400 }, { kind: "lowpass", cutoff: 4500 }],
    },

    pads: {
      effects: [{ kind: "chorus", delay: 0.016, depth: 0.004, rate: 0.2, wet: 0.5 }],
    },

    drums: {},

    low: {
      duck: { key: "drums", depth: 0.3, threshold: 0.3, attack: 0.002, release: 0.1 },
    },
  },

  patterns: {
    // ── the intro ────────────────────────────────────────────────────────────
    intro_box: {
      instrument: "box",
      step: "1/4",
      notes: "F#5 A5 C#6 F#6 | E6:4",
    },

    intro_pad: {
      instrument: "pad",
      step: "1/1",
      notes: "F#2+A2+C#3:2",
    },

    // ── the tune ─────────────────────────────────────────────────────────────
    tune_a: {
      instrument: "box",
      step: "1/8",
      notes: `C#6:2! F#6:2 E6 C#6 A5:2 |
B5! A5 F#5:2 A5:2 D6:2 |
C#6:2! B5 A5 G#5:2 F#5:2 |
E5:4! G#5:2 C#6:2 |
C#6:2! F#6:2 E6 C#6 A5:2 |
C#6! E6 A6:2 G#6:2 E6:2 |
F#6:2! D6:2 C#6:2! E6:2 |
F#6:6! .:2 |
`,
    },

    tune_b: {
      instrument: "box",
      step: "1/8",
      notes: `E6:2! C#6:2 A5:2 C#6:2 |
B5:2! G#5:2 E5:2 G#5 B5 |
A5:2! C#6:2 F#6:3 E6 |
D6:4! C#6:2 B5:2 |
B5:2! D6:2 F#6:2 E6 D6 |
C#6:4! A5:2 F#5:2 |
G#5:2! B5:2 E6:2 D6:2 |
E6:4! C#6:4 |
`,
    },

    // ── harmony ──────────────────────────────────────────────────────────────
    pad_a: {
      instrument: "pad",
      step: "1/1",
      notes: "F#2+A2+C#3 | D3+F#3+A3 | B2+D3+F#3 | C#3+E3+G#3 | F#2+A2+C#3 | A2+C#3+E3 | B2+D3+F#3 | F#2+A2+C#3",
    },

    pad_b: {
      instrument: "pad",
      step: "1/1",
      notes: "A2+C#3+E3 | E3+G#3+B3 | F#2+A2+C#3 | D3+F#3+A3 | B2+D3+F#3 | F#2+A2+C#3 | E3+G#3+B3 | C#3+E3+G#3",
    },

    bass_a: {
      instrument: "bass",
      step: "1/8",
      notes: `F#1! . C#2 . F#1 . C#2 . | D2! . A2 . D2 . A2 . | B1! . F#2 . B1 . F#2 . | C#2! . G#2 . C#2 . G#2 . |
F#1! . C#2 . F#1 . C#2 . | A1! . E2 . A1 . E2 . | B1! . F#2 . C#2! . G#2 . | F#1! . C#2 . F#1:2 . . |
`,
    },

    bass_b: {
      instrument: "bass",
      step: "1/8",
      notes: `A1! . E2 . A1 . E2 . | E2! . B2 . E2 . B2 . | F#1! . C#2 . F#1 . C#2 . | D2! . A2 . D2 . A2 . |
B1! . F#2 . B1 . F#2 . | F#1! . C#2 . F#1 . C#2 . | E2! . B2 . E2 . G#2 . | C#2! . G#2 . C#2 . E2 . |
`,
    },

    // ── drums (sixteenths) ───────────────────────────────────────────────────
    walls: {
      step: "1/16",

      lanes: {
        knock: "x..........x.... | ......x.........",
      },
    },

    drums_soft: {
      step: "1/16",

      lanes: {
        kick: "x.......x....... | x.......x.......",
        snare: "....x.......x... | ....x.......x...",
        hat: "..x...x...x...x. | ..x...x...x...x.",
      },
    },

    drums_full: {
      step: "1/16",

      lanes: {
        kick: "x.....x...x..... | x.....x.x...x...",
        snare: "....X.......X... | ....X.......X.oX",
        hat: "x.xox.xox.xox.xo | x.xox.xox.xoxxxx",
      },
    },

    crash: {
      step: "1/16",

      lanes: {
        crash: "X...............",
      },
    },

    // ── the outro ────────────────────────────────────────────────────────────
    end_box: {
      instrument: "box",
      step: "1/8",
      notes: "F#6:4! C#6:4 | F#5:8",
    },

    end_pad: {
      instrument: "pad",
      step: "1/1",
      notes: "F#2+A2+C#3:2",
    },
  },

  sections: {
    // ── sections ─────────────────────────────────────────────────────────────
    intro: {
      bars: 2,
      layers: [{ pattern: "intro_box" }, { pattern: "intro_pad" }],
    },

    a: {
      bars: 8,
      layers: [{ pattern: "tune_a" }, { pattern: "pad_a" }, { pattern: "walls", times: 4, intensity: [0, 0.6] }, { pattern: "drums_soft", times: 4, intensity: [0.35, 0.6] }, { pattern: "drums_full", times: 4, intensity: [0.6, 1] }, { pattern: "bass_a", intensity: [0.35, 1] }, { pattern: "tune_a", instrument: "hum", transpose: -12, intensity: [0.6, 1] }, { pattern: "tune_a", instrument: "celesta", transpose: 12, intensity: [0.85, 1] }, { pattern: "crash", times: 1, intensity: [0.85, 1] }],
    },

    b: {
      bars: 8,
      layers: [{ pattern: "tune_b" }, { pattern: "pad_b" }, { pattern: "walls", times: 4, intensity: [0, 0.6] }, { pattern: "drums_soft", times: 4, intensity: [0.35, 0.6] }, { pattern: "drums_full", times: 4, intensity: [0.6, 1] }, { pattern: "bass_b", intensity: [0.35, 1] }, { pattern: "tune_b", instrument: "hum", transpose: -12, intensity: [0.6, 1] }, { pattern: "tune_b", instrument: "celesta", transpose: 12, intensity: [0.85, 1] }, { pattern: "crash", times: 1, intensity: [0.85, 1] }],
    },

    end: {
      bars: 2,
      layers: [{ pattern: "end_box" }, { pattern: "end_pad" }],
    },
  },

  arrangement: {
    intro: ["intro"],
    loop: ["a", "b"],
    outro: ["end"],
  },
});
