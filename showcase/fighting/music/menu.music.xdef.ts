import { defineMusic } from "omui/define";

export default defineMusic({
  doc: `拳意's main theme, under the title and the select screen: the game's three
homes in one room. A dizi states the theme in E minor over a pad and a
guzheng's arpeggio (墨's study); a sub bass and a boom-bap kit come in under
it (坤's rooftop); a wide saw doubles the answer at the top (霓's neon).

Intensity (0–1) adds parts at the next bar: below 0.4 (the title) the dizi,
the pad, the arpeggio, a bell and a soft taiko; from 0.4 (the select screen)
the bass, the strings and the beat; from 0.8 (a player ready) the full kit,
the taiko's answers, the crash and the saw.

96 beats a minute; a two-bar intro, then a loop of 16 bars (A, B) — A states
the theme over Em C G D, B answers it higher on the guzheng over C D Bm Em and
lands on B for the loop's return.`,
  name: "menu",
  tempo: 96,
  meter: [4, 4],
  gain: 0.5,

  patches: {
    // ── patches: the built-ins, shaded ───────────────────────────────────────
    flute: {
      base: "dizi",
      vibrato: { depth: 14, delay: 0.35 },
    },

    zheng: {
      base: "guzheng",
      pluck: { decay: 3.2 },
      vibrato: { depth: 10, delay: 0.4 },
    },

    zheng_low: {
      base: "koto",
      pluck: { brightness: 0.5, decay: 1.2, pick: 0.2 },
      filter: { cutoff: 3200 },
    },

    warm: {
      base: "pad",
      filter: { cutoff: 650, envelope: 1 },
    },

    wide: {
      base: "supersaw",
      filter: { cutoff: 2400 },
    },
  },

  instruments: {
    // ── instruments ──────────────────────────────────────────────────────────
    lead: {
      patch: "flute",
      gain: 0.78,
      pan: 0.05,
      legato: 1,
      sends: { hall: 0.3, delay: 0.14 },
    },

    answer: {
      patch: "zheng",
      gain: 0.95,
      pan: -0.08,
      legato: 1,
      voices: 8,
      sends: { hall: 0.25, delay: 0.12 },
    },

    arp: {
      patch: "zheng_low",
      gain: 0.42,
      pan: -0.3,
      legato: 1,
      humanize: 0.3,
      voices: 10,
      sends: { hall: 0.15, delay: 0.18 },
    },

    pad: {
      patch: "warm",
      bus: "pads",
      gain: 0.36,
      legato: 1,
      voices: 8,
      sends: { hall: 0.35 },
    },

    strings: {
      patch: "strings",
      gain: 0.42,
      pan: 0.25,
      legato: 1,
      sends: { hall: 0.35 },
    },

    saw: {
      patch: "wide",
      bus: "pads",
      gain: 0.2,
      pan: -0.1,
      legato: 1,
      voices: 6,
      sends: { hall: 0.3, delay: 0.1 },
    },

    bell: {
      patch: "bell",
      gain: 0.42,
      pan: 0.2,
      legato: 1,
      sends: { hall: 0.5 },
    },

    bass: {
      patch: "sub_bass",
      bus: "low",
      gain: 0.85,
      legato: 0.6,
    },

    taiko: {
      patch: "taiko",
      bus: "drums",
      gain: 0.66,
      humanize: 0.15,
      sends: { hall: 0.15 },
    },

    taiko_hi: {
      patch: "tom",
      bus: "drums",
      gain: 0.55,
      pan: 0.2,
      transpose: 7,
      humanize: 0.2,
      sends: { hall: 0.15 },
    },

    kick: {
      patch: "kick",
      bus: "drums",
      gain: 0.62,
    },

    snare: {
      patch: "snare",
      bus: "drums",
      gain: 0.55,
      sends: { hall: 0.25 },
    },

    hat: {
      patch: "hat",
      bus: "drums",
      gain: 0.45,
      pan: 0.25,
      humanize: 0.4,
    },

    shaker: {
      patch: "shaker",
      bus: "drums",
      gain: 0.45,
      pan: 0.3,
      humanize: 0.4,
    },

    crash: {
      patch: "crash",
      bus: "drums",
      gain: 0.32,
      sends: { hall: 0.2 },
    },
  },

  buses: {
    // ── buses ────────────────────────────────────────────────────────────────
    hall: {
      effects: [{ kind: "reverb", size: 0.86, damping: 0.45, predelay: 0.03, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 220 }],
    },

    delay: {
      effects: [{ kind: "echo", beats: 0.75, feedback: 0.3, pingpong: true, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 300 }, { kind: "lowpass", cutoff: 5000 }],
    },

    pads: {
      effects: [{ kind: "chorus", delay: 0.014, depth: 0.004, rate: 0.25, wet: 0.5 }],
    },

    drums: {},

    low: {
      duck: { key: "drums", depth: 0.35, threshold: 0.3, attack: 0.002, release: 0.1 },
    },
  },

  patterns: {
    // ── the intro ────────────────────────────────────────────────────────────
    intro_bell: {
      instrument: "bell",
      step: "1/1",
      notes: "E5 | B4",
    },

    intro_lead: {
      instrument: "lead",
      step: "1/16",
      notes: "E5:12! . . D5? E5? | G5:4! E5:4 B4:8 |",
    },

    intro_pad: {
      instrument: "pad",
      step: "1/1",
      notes: "E3+B3+D4+G4 | B2+F#3+B3+D#4",
    },

    intro_drums: {
      step: "1/16",

      lanes: {
        taiko: "X............... | X.......x.x.xxXX",
        shaker: "................ | ........o.o.x.xX",
      },
    },

    // ── the theme (the dizi) and its answer (the guzheng) ────────────────────
    // Em | C | G | D | Em | C | Am | B
    theme_a: {
      instrument: "lead",
      step: "1/16",
      notes: `E5:6! G5:2 A5:4 B5:4 |
G5:3! E5? D5:4 E5:8 |
D5:4! E5:2 G5:2 B5:6 A5:2 |
A5:8! G5:4 F#5:4 |
E5:6! G5:2 A5:4 B5:2 D6:2 |
E6:6! D6:2 B5:4 G5:4 |
A5:4! B5:2 A5:2 G5:4 E5:4 |
F#5>B5:12! . . . . |
`,
    },

    // C | D | Bm | Em | C | D | B7sus4 | B
    theme_b: {
      instrument: "answer",
      step: "1/16",
      notes: `G5:3! A5 B5:4 E6:6 D6:2 |
D6:4! B5:2 A5:2 F#5:8 |
F#5:3! A5 B5:4 D6:4 B5:4 |
E6:12! . . D6? B5? |
E6:3! G6 E6:2 D6:2 B5:4 G5:4 |
A5:3! B5 D6:4 F#6:4 E6:4 |
E6:4! D6:4 B5:4 A5:4 |
B5:16! |
`,
    },

    bell_e: {
      instrument: "bell",
      step: "1/1",
      notes: "E5",
    },

    bell_b: {
      instrument: "bell",
      step: "1/1",
      notes: "B4",
    },

    // ── harmony ──────────────────────────────────────────────────────────────
    // Em7 | Cmaj7 | Gadd9 | D | Em7 | Cmaj7 | Am7 | B
    pad_a: {
      instrument: "pad",
      step: "1/1",
      notes: `E3+B3+D4+G4 | C3+G3+B3+E4 | G3+B3+D4+A4 | D3+A3+D4+F#4 |
E3+B3+D4+G4 | C3+G3+B3+E4 | A2+E3+G3+C4 | B2+F#3+B3+D#4 |
`,
    },

    // Cmaj7 | D | Bm7 | Em7 | Cmaj7 | D | B7sus4 | B7
    pad_b: {
      instrument: "pad",
      step: "1/1",
      notes: `C3+G3+B3+E4 | D3+A3+D4+F#4 | B2+F#3+A3+D4 | E3+B3+D4+G4 |
C3+G3+B3+E4 | D3+A3+D4+F#4 | B2+E3+F#3+A3 | B2+F#3+A3+D#4 |
`,
    },

    strings_a: {
      instrument: "strings",
      step: "1/2",
      notes: "B4:2 | C5 B4 | B4 A4 | A4 F#4 | G4:2 | G4 E4 | E4 C5 | D#5:2",
    },

    strings_b: {
      instrument: "strings",
      step: "1/2",
      notes: "E5:2 | F#5 D5 | D5 F#5 | G5:2 | G5 E5 | F#5 A5 | A5 F#5 | D#5:2",
    },

    // ── the arpeggio under the tune (sixteenths) ─────────────────────────────
    arp_a: {
      instrument: "arp",
      step: "1/16",
      notes: `E4! B4? E5 B4? G4! B4? E5 B4? E4! B4? E5 B4? G4! B4? D5 B4? | C4! G4? C5 G4? E4! G4? C5 G4? C4! G4? C5 G4? E4! G4? B4 G4? |
G3! D4? G4 D4? B3! D4? G4 D4? G3! D4? G4 D4? B3! D4? A4 D4? | D4! A4? D5 A4? F#4! A4? D5 A4? D4! A4? D5 A4? F#4! A4? E5 A4? |
E4! B4? E5 B4? G4! B4? E5 B4? E4! B4? E5 B4? G4! B4? D5 B4? | C4! G4? C5 G4? E4! G4? C5 G4? C4! G4? C5 G4? E4! G4? B4 G4? |
A3! E4? A4 E4? C4! E4? A4 E4? A3! E4? A4 E4? C4! E4? G4 E4? | B3! F#4? B4 F#4? D#4! F#4? B4 F#4? B3! F#4? B4 F#4? D#4! F#4? A4 F#4? |
`,
    },

    arp_b: {
      instrument: "arp",
      step: "1/16",
      notes: `C4! G4? C5 G4? E4! G4? C5 G4? C4! G4? C5 G4? E4! G4? B4 G4? | D4! A4? D5 A4? F#4! A4? D5 A4? D4! A4? D5 A4? F#4! A4? E5 A4? |
B3! F#4? B4 F#4? D4! F#4? B4 F#4? B3! F#4? B4 F#4? D4! F#4? A4 F#4? | E4! B4? E5 B4? G4! B4? E5 B4? E4! B4? E5 B4? G4! B4? D5 B4? |
C4! G4? C5 G4? E4! G4? C5 G4? C4! G4? C5 G4? E4! G4? B4 G4? | D4! A4? D5 A4? F#4! A4? D5 A4? D4! A4? D5 A4? F#4! A4? E5 A4? |
B3! F#4? B4 F#4? E4! F#4? B4 F#4? B3! F#4? B4 F#4? E4! F#4? A4 F#4? | B3! F#4? B4 F#4? D#4! F#4? B4 F#4? B3! F#4? B4 F#4? D#4! F#4? A4 F#4? |
`,
    },

    // ── the bass (eighths) ───────────────────────────────────────────────────
    bass_a: {
      instrument: "bass",
      notes: `E1! . E2? E1 . E1! E2? . | C2! . C3? C2 . C2! C3? . |
G1! . G2? G1 . G1! G2? . | D2! . D3? D2 . D2! D3? . |
E1! . E2? E1 . E1! E2? . | C2! . C3? C2 . C2! C3? . |
A1! . A2? A1 . A1! A2? . | B1! . B2? B1 . B1! B2? . |
`,
    },

    bass_b: {
      instrument: "bass",
      notes: `C2! . C3? C2 . C2! C3? . | D2! . D3? D2 . D2! D3? . |
B1! . B2? B1 . B1! B2? . | E1! . E2? E1 . E1! E2? . |
C2! . C3? C2 . C2! C3? . | D2! . D3? D2 . D2! D3? . |
B1! . B2? B1 . B1! B2? . | B1! . B2? B1 . B1! B1! B1! |
`,
    },

    // ── drums (sixteenths) ───────────────────────────────────────────────────
    drums_soft: {
      step: "1/16",

      lanes: {
        taiko: "X.........o.....",
        shaker: "....x.......x..o",
      },
    },

    drums_beat: {
      step: "1/16",

      lanes: {
        kick: "x.....x...x..... | x.....x.x.......",
        snare: "....X.......X... | ....X.......X..o",
        hat: "x.x.x.x.x.x.xox. | x.x.x.x.x.x.xoxx",
        taiko: "X............... | ................",
      },
    },

    drums_full: {
      step: "1/16",

      lanes: {
        kick: "x.....x.x.x...x. | x.....x.x.....x.",
        snare: "....X.......X... | ....X.......X.oX",
        hat: "x.x.x.x.x.x.xox. | x.x.x.x.x.x.xoxx",
        taiko: "X.......X....... | X.......X...x.x.",
        taiko_hi: "......x....x..x. | ...x..x....x.xx.",
      },
    },

    fill_beat: {
      step: "1/16",

      lanes: {
        kick: "x.....x.........",
        snare: "....X.....o.xxXX",
        taiko: "........x.x.xxXX",
      },
    },

    crash: {
      step: "1/16",

      lanes: {
        crash: "X...............",
      },
    },

    // ── the outro ────────────────────────────────────────────────────────────
    end_lead: {
      instrument: "lead",
      step: "1/16",
      notes: "B5? A5? G5? E5? D5? B4? A4? G4? E4:8! | .:16",
    },

    end_pad: {
      instrument: "pad",
      step: "1/1",
      notes: "E3+B3+D4+G4:2",
    },

    end_drums: {
      step: "1/16",

      lanes: {
        taiko: "X............... | ................",
      },
    },
  },

  sections: {
    // ── sections ─────────────────────────────────────────────────────────────
    intro: {
      bars: 2,
      layers: [{ pattern: "intro_bell" }, { pattern: "intro_lead" }, { pattern: "intro_pad" }, { pattern: "intro_drums" }],
    },

    a: {
      bars: 8,
      layers: [{ pattern: "theme_a" }, { pattern: "pad_a" }, { pattern: "arp_a" }, { pattern: "bell_e", times: 1, intensity: [0, 0.8] }, { pattern: "drums_soft", intensity: [0, 0.4] }, { pattern: "drums_beat", times: 3, intensity: [0.4, 0.8] }, { pattern: "fill_beat", at: 7, intensity: [0.4, 0.8] }, { pattern: "drums_full", times: 3, intensity: [0.8, 1] }, { pattern: "fill_beat", at: 7, intensity: [0.8, 1] }, { pattern: "bass_a", intensity: [0.4, 1] }, { pattern: "strings_a", intensity: [0.4, 1] }, { pattern: "crash", times: 1, intensity: [0.8, 1] }],
    },

    b: {
      bars: 8,
      layers: [{ pattern: "theme_b" }, { pattern: "theme_b", instrument: "saw", transpose: -12, intensity: [0.8, 1] }, { pattern: "pad_b" }, { pattern: "arp_b" }, { pattern: "bell_b", times: 1, intensity: [0, 0.8] }, { pattern: "drums_soft", intensity: [0, 0.4] }, { pattern: "drums_beat", times: 3, intensity: [0.4, 0.8] }, { pattern: "fill_beat", at: 7, intensity: [0.4, 0.8] }, { pattern: "drums_full", times: 3, intensity: [0.8, 1] }, { pattern: "fill_beat", at: 7, intensity: [0.8, 1] }, { pattern: "bass_b", intensity: [0.4, 1] }, { pattern: "strings_b", intensity: [0.4, 1] }, { pattern: "crash", times: 1, intensity: [0.8, 1] }],
    },

    end: {
      bars: 2,
      layers: [{ pattern: "end_lead" }, { pattern: "end_pad" }, { pattern: "bell_e", times: 1 }, { pattern: "end_drums" }],
    },
  },

  arrangement: {
    intro: ["intro"],
    loop: ["a", "b"],
    outro: ["end"],
  },
});
