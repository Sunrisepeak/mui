import { defineMusic } from "omui/define";

export default defineMusic({
  doc: `The film's score: 120 beats a minute, a bar every two seconds, so the
picture's cuts and hits land on its beats (the timeline's markers are
checked against them). A minor, blueprint-clean electronics: a sine pad
and a cold arpeggio while the camera finds the pixel, the beat when the
wordmark forms, a typing groove under the code, the floor dropping away
for the wall and coming back as the camera dives, a supersaw stab on every
style, a half-time pulse of doubt under the agent, a drive into the
rewind, and an open C major to end.

Sections follow the film's chapters; the arrangement plays them once.
The second half of \`time\` and the \`end\` are what the film plays after its
rewind (from 46 s of the score).`,
  name: "promo",
  tempo: 120,
  meter: [4, 4],
  gain: 0.55,

  patches: {
    // ── patches ──────────────────────────────────────────────────────────────
    glass_pad: {
      base: "pad",
      osc: { wave: "saw", unison: 5, detune: 18, spread: 0.9 },
      amp: { attack: 1.2, decay: 2, sustain: 0.85, release: 1.6 },
      filter: { cutoff: 1100, envelope: 1.4, attack: 1.6, decay: 3, sustain: 0.6, release: 1.6 },
    },

    sine_pad: {
      engine: "analog",
      gain: 0.2,
      osc: { wave: "triangle", unison: 3, detune: 6, spread: 0.7 },
      amp: { attack: 1.8, decay: 2, sustain: 0.9, release: 2.2 },
      filter: { kind: "lowpass", cutoff: 1800, q: 0.6 },
    },

    cold_arp: {
      base: "fm_pluck",
      fm: { ratio: 3, index: 2.2, decay: 0.09 },
      amp: { decay: 0.35, release: 0.15 },
    },

    blip: {
      base: "arp",
      filter: { cutoff: 1500, envelope: 2.4, decay: 0.1 },
    },

    lead: {
      base: "saw_lead",
      osc: { unison: 3, detune: 12, spread: 0.5 },
      filter: { cutoff: 3200, envelope: 1.3 },
      vibrato: { depth: 14, delay: 0.3 },
      glide: 0.05,
    },

    stab: {
      base: "supersaw",
      amp: { attack: 0.003, decay: 0.35, sustain: 0.25, release: 0.2 },
      filter: { cutoff: 3400, envelope: 0.8 },
    },

    bass: {
      base: "saw_bass",
      filter: { cutoff: 520, envelope: 1.8 },
      drive: 0.35,
    },

    keys: {
      base: "epiano",
      fm: { index: 1.6 },
      amp: { decay: 2.8 },
    },

    riser: {
      engine: "analog",
      osc: { wave: "noise" },
      amp: { attack: 3.6, decay: 1, sustain: 1, release: 0.05 },
      filter: { kind: "bandpass", cutoff: 400, q: 1.4, envelope: 4.5, attack: 3.8, decay: 1, sustain: 1, release: 0.05 },
      gain: 0.5,
    },

    punch: {
      base: "kick",
      drum: { pitch: 50, from: 4.8, sweep: 0.028, decay: 0.36, click: 0.4 },
      drive: 0.3,
    },

    boom: {
      base: "kick",
      drum: { pitch: 38, from: 3.2, sweep: 0.08, decay: 1.6, click: 0.2 },
      gain: 0.55,
      drive: 0.2,
    },
  },

  instruments: {
    // ── instruments ──────────────────────────────────────────────────────────
    pads: {
      patch: "glass_pad",
      bus: "pads",
      gain: 0.42,
      legato: 1,
      voices: 10,
      sends: { space: 0.35 },
    },

    air: {
      patch: "sine_pad",
      bus: "pads",
      gain: 0.5,
      legato: 1,
      voices: 6,
      sends: { space: 0.5 },
    },

    arp: {
      patch: "cold_arp",
      gain: 0.42,
      pan: -0.15,
      humanize: 0.15,
      voices: 8,
      sends: { echo: 0.3, space: 0.2 },
    },

    blip: {
      patch: "blip",
      gain: 0.3,
      pan: 0.25,
      voices: 6,
      sends: { echo: 0.35 },
    },

    keys: {
      patch: "keys",
      gain: 0.5,
      pan: 0.1,
      legato: 1,
      voices: 8,
      sends: { space: 0.4, echo: 0.2 },
    },

    lead: {
      patch: "lead",
      bus: "leads",
      gain: 0.9,
      legato: 0.95,
      voices: 6,
      sends: { space: 0.25, echo: 0.18 },
    },

    stab: {
      patch: "stab",
      bus: "pads",
      gain: 0.32,
      legato: 1,
      voices: 12,
      sends: { space: 0.25 },
    },

    bass: {
      patch: "bass",
      bus: "low",
      gain: 0.75,
      legato: 0.85,
      voices: 2,
    },

    sub: {
      patch: "sub_bass",
      bus: "low",
      gain: 0.75,
      legato: 1,
      voices: 2,
    },

    riser: {
      patch: "riser",
      gain: 0.32,
      legato: 1,
      voices: 1,
      sends: { space: 0.4 },
    },

    kick: {
      patch: "punch",
      bus: "kick",
      gain: 1.0,
    },

    boom: {
      patch: "boom",
      bus: "kick",
      gain: 0.9,
    },

    snare: {
      patch: "snare",
      bus: "drums",
      gain: 0.42,
      sends: { space: 0.25 },
    },

    clap: {
      patch: "clap",
      bus: "drums",
      gain: 0.32,
      pan: 0.05,
      sends: { space: 0.3 },
    },

    hat: {
      patch: "hat",
      bus: "drums",
      gain: 0.42,
      pan: 0.2,
      humanize: 0.3,
      choke: "hats",
    },

    open: {
      patch: "open_hat",
      bus: "drums",
      gain: 0.3,
      pan: 0.2,
      choke: "hats",
    },

    tick: {
      patch: "woodblock",
      bus: "drums",
      gain: 0.16,
      pan: -0.3,
    },

    crash: {
      patch: "crash",
      bus: "drums",
      gain: 0.34,
      pan: -0.15,
      sends: { space: 0.2 },
    },
  },

  buses: {
    // ── buses ────────────────────────────────────────────────────────────────
    space: {
      effects: [{ kind: "reverb", size: 0.88, damping: 0.35, predelay: 0.02, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 220 }],
    },

    echo: {
      effects: [{ kind: "echo", beats: 0.75, feedback: 0.38, pingpong: true, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 400 }, { kind: "lowpass", cutoff: 6500 }],
    },

    leads: {
      effects: [{ kind: "chorus", delay: 0.01, depth: 0.002, rate: 0.7, wet: 0.3 }],
    },

    pads: {
      effects: [{ kind: "chorus", delay: 0.014, depth: 0.004, rate: 0.25, wet: 0.4 }],
      duck: { key: "kick", depth: 0.55, threshold: 0.3, attack: 0.003, release: 0.2 },
    },

    low: {
      duck: { key: "kick", depth: 0.5, threshold: 0.3, attack: 0.002, release: 0.12 },
    },

    kick: {},

    drums: {},
  },

  patterns: {
    // ── harmony: Am9 | Fmaj7 | Cadd9 | G6 ────────────────────────────────────
    pads_main: {
      instrument: "pads",
      step: "1/1",
      notes: "A3+C4+E4+B4 | F3+A3+C4+E4 | C3+G3+D4+E4 | G3+B3+D4+E4",
    },

    air_main: {
      instrument: "air",
      step: "1/1",
      notes: "E5+A5 | C5+E5 | D5+G5 | D5+B5",
    },

    sub_main: {
      instrument: "sub",
      step: "1/1",
      notes: "A1 | F1 | C2 | G1",
    },

    bass_main: {
      instrument: "bass",
      step: "1/16",
      notes: `A1:2! A2? A1 . A2:2 A1 A2? A1:2! A2? . E2 G2 A2? |
F1:2! F2? F1 . F2:2 F1 F2? F1:2! F2? . C2 E2 F2? |
C2:2! C3? C2 . C3:2 C2 C3? C2:2! C3? . G2 B2 C3? |
G1:2! G2? G1 . G2:2 G1 G2? G1:2! G2? . D2 F#2 G2? |
`,
    },

    arp_main: {
      instrument: "arp",
      step: "1/16",
      notes: `A4! C5 E5 B5 A5 E5 C5 E5 A4! C5 E5 B5 A5 E5 C5 E5 |
F4! A4 C5 E5 F5 E5 C5 A4 F4! A4 C5 E5 F5 E5 C5 A4 |
C5! E5 G5 D6 E6 D6 G5 E5 C5! E5 G5 D6 E6 D6 G5 E5 |
G4! B4 D5 E5 G5 E5 D5 B4 G4! B4 D5 E5 G5 F#5 E5 D5 |
`,
    },

    arp_slow: {
      instrument: "arp",
      step: "1/8",
      notes: `A4! E5 B5 E5 C5 E5 B5 E5 |
F4! C5 E5 C5 A4 C5 E5 C5 |
C5! G5 D6 G5 E5 G5 D6 G5 |
G4! D5 E5 D5 B4 D5 F#5 D5 |
`,
    },

    keys_motif: {
      instrument: "keys",
      step: "1/8",
      notes: `E5:3! B4 C5:2 E5:2 |
A5:4! G5:2 E5:2 |
G5:3! D5 E5:2 G5:2 |
B5:4! A5:2 G5? F#5? |
`,
    },

    lead_hook: {
      instrument: "lead",
      step: "1/16",
      notes: `E5:3! D5:3 C5:2 B4:2 C5:2 E5:4 |
A5:6! G5:2 E5:4 C5:4 |
D5:3! E5:3 G5:2 E5:2 D5:2 C5:4 |
B4:4! D5:4 G5:8 |
`,
    },

    lead_lift: {
      instrument: "lead",
      step: "1/16",
      notes: `A5:3! G5:3 E5:2 D5:2 E5:2 A5:4 |
C6:6! B5:2 A5:4 E5:4 |
D6:3! C6:3 B5:2 G5:2 A5:2 B5:4 |
G5:8! . . . . B5:2 D6:2 |
`,
    },

    // The styles: a chord struck on each switch (beats 3 and 1 of each bar).
    stabs_styles: {
      instrument: "stab",
      step: "1/4",
      notes: `A3+C4+E4+B4:2 A3+C4+E4+B4! . |
F3+A3+C4+E4! . F3+A3+C4+E4! . |
C4+E4+G4+D5! . C4+E4+G4+D5! . |
G3+B3+D4+E4! . . . |
`,
    },

    // ── the agent's doubt: Dm9 | Fmaj7 | Am9 | E7 (half time) ───────────────
    pads_doubt: {
      instrument: "pads",
      step: "1/1",
      notes: "D3+F3+A3+E4 | F3+A3+C4+E4 | A3+C4+E4+B4 | E3+G#3+B3+D4 | D3+F3+A3+E4",
    },

    sub_doubt: {
      instrument: "sub",
      step: "1/1",
      notes: "D2 | F1 | A1 | E1 | D2",
    },

    blip_doubt: {
      instrument: "blip",
      step: "1/8",
      notes: `D5! A4 F5 A4 E5! A4 F5 A4 |
F5! C5 A5 C5 G5! C5 A5 C5 |
E5! C5 A5 C5 B5! C5 A5 C5 |
E5! B4 G#5 B4 D6! B4 G#5 B4 |
D5! A4 F5 A4 E5! A4 F5 A4 |
`,
    },

    keys_doubt: {
      instrument: "keys",
      step: "1/4",
      notes: `F5:2! E5? D5? |
A5:2! G5? E5? |
C6:2! B5? A5? |
G#5:3! . |
F5:4! |
`,
    },

    // ── drums (sixteenths) ───────────────────────────────────────────────────
    clock: {
      step: "1/16",

      lanes: {
        tick: "x...o...x...o... | x...o...x...o.oo",
      },
    },

    first_beat: {
      step: "1/16",

      lanes: {
        kick: "x............... | x.......x.......",
        hat: "....o.......o... | ..x...x...x...xx",
        crash: "................ | X...............",
      },
    },

    groove: {
      step: "1/16",

      lanes: {
        kick: "x...x...x...x...",
        clap: "....x.......x...",
        hat: "x.x.x.x.x.x.x.x.",
        open: "..x...x...x...x.",
      },
    },

    groove_fill: {
      step: "1/16",

      lanes: {
        kick: "x...x...x...x.x.",
        clap: "....x.......x...",
        snare: "..........x.xxXX",
        hat: "x.x.x.x.x.x.....",
      },
    },

    morphs: {
      step: "1/16",

      lanes: {
        crash: "X.......X....... | X...............",
        boom: "x.......x....... | x...............",
      },
    },

    build: {
      step: "1/16",

      lanes: {
        kick: "x.......x.......x...x...x.x.x.x. | x...x...x...x...xxxxxxxxXXXXXXXX",
        snare: "x.......x.......x...x...x.x.x.x. | x.x.x.x.xxxxxxxxXXXXXXXXXXXXXXXX",
      },
    },

    drop: {
      step: "1/16",

      lanes: {
        crash: "X...............",
        boom: "x...............",
      },
    },

    half: {
      step: "1/16",

      lanes: {
        kick: "x.........x.....",
        snare: "........X.......",
        hat: "x.xxx.xxx.xxx.xx",
      },
    },

    alarm: {
      step: "1/16",

      lanes: {
        boom: "....x...........",
      },
    },

    drive: {
      step: "1/16",

      lanes: {
        kick: "x...x...x...x... | x...x...x...x.x.",
        clap: "....x.......x... | ....x.......x...",
        hat: "xxx.xxx.xxx.xxx. | xxx.xxx.xxx.xxxx",
        open: "..............x. | ......x.......x.",
        snare: "................ | ..........x.xxXX",
      },
    },

    final_hit: {
      step: "1/16",

      lanes: {
        crash: "X............... | ................ | X...............",
        boom: "x............... | ................ | x...............",
      },
    },

    typing_ticks: {
      step: "1/16",

      lanes: {
        tick: "................ | ................ | x.x.x.x.x.x.x.x.",
      },
    },

    rise1: {
      instrument: "riser",
      step: "1/16",
      notes: "A4:16",
    },

    rise2: {
      instrument: "riser",
      step: "1/16",
      notes: "A4:32",
    },

    // ── the end: Fmaj7 | G6 | Cadd9 (held) ───────────────────────────────────
    pads_end: {
      instrument: "pads",
      step: "1/1",
      notes: "F3+A3+C4+E4 | G3+B3+D4+E4 | C3+G3+D4+E4:2",
    },

    air_end: {
      instrument: "air",
      step: "1/1",
      notes: "C5+A5 | D5+B5 | E5+G5+D6:2",
    },

    sub_end: {
      instrument: "sub",
      step: "1/1",
      notes: "F1 | G1 | C2:2",
    },

    arp_end: {
      instrument: "arp",
      step: "1/8",
      notes: `F4! C5 E5 A5 E5 C5 A4 C5 |
G4! D5 E5 B5 E5 D5 B4 D5 |
C5! G5 D6 E6 G6:4! |
`,
    },

    lead_end: {
      instrument: "lead",
      step: "1/16",
      notes: ". . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | G5:4! E5:4 D6:8 |",
    },
  },

  sections: {
    // ── sections, a chapter each ─────────────────────────────────────────────
    hook: {
      bars: 2,
      layers: [{ pattern: "air_main", times: 1 }, { pattern: "sub_main", times: 1 }, { pattern: "clock" }, { pattern: "rise1", at: 1 }],
    },

    title: {
      bars: 2,
      layers: [{ pattern: "pads_main", at: 0, times: 1 }, { pattern: "sub_main", at: 0, times: 1 }, { pattern: "arp_slow", at: 0, times: 1 }, { pattern: "first_beat" }],
    },

    code: {
      bars: 4,
      layers: [{ pattern: "pads_main" }, { pattern: "bass_main" }, { pattern: "arp_main" }, { pattern: "groove", times: 3 }, { pattern: "groove_fill", at: 3 }, { pattern: "morphs", at: 2 }],
    },

    infinite_a: {
      bars: 3,
      layers: [{ pattern: "pads_main", times: 1 }, { pattern: "air_main", times: 1 }, { pattern: "sub_main", times: 1 }, { pattern: "keys_motif", times: 1 }, { pattern: "arp_slow", times: 1 }],
    },

    infinite_b: {
      bars: 3,
      layers: [{ pattern: "pads_main", times: 1 }, { pattern: "rise2" }, { pattern: "build" }, { pattern: "drop", at: 2 }, { pattern: "groove", at: 2 }, { pattern: "bass_main", at: 2, times: 1 }, { pattern: "lead_hook", at: 2, times: 1 }],
    },

    styles: {
      bars: 4,
      layers: [{ pattern: "pads_main" }, { pattern: "bass_main" }, { pattern: "arp_main" }, { pattern: "lead_lift" }, { pattern: "stabs_styles" }, { pattern: "groove", times: 3 }, { pattern: "groove_fill", at: 3 }, { pattern: "drop" }],
    },

    agent: {
      bars: 5,
      layers: [{ pattern: "pads_doubt" }, { pattern: "sub_doubt" }, { pattern: "blip_doubt" }, { pattern: "keys_doubt" }, { pattern: "half", times: 5 }, { pattern: "alarm", at: 2, times: 1 }, { pattern: "alarm", at: 3, times: 1 }],
    },

    time: {
      bars: 2,
      layers: [{ pattern: "pads_main", times: 1 }, { pattern: "bass_main", times: 1 }, { pattern: "arp_main", times: 1 }, { pattern: "drive" }, { pattern: "drop" }],
    },

    end: {
      bars: 3,
      layers: [{ pattern: "pads_end" }, { pattern: "air_end" }, { pattern: "sub_end" }, { pattern: "arp_end" }, { pattern: "lead_end" }, { pattern: "final_hit" }, { pattern: "typing_ticks" }],
    },

    rest: {
      bars: 1,
      layers: [],
    },
  },

  arrangement: {
    intro: ["hook", "title", "code", "infinite_a", "infinite_b", "styles", "agent", "time", "end"],
    loop: ["rest"],
  },
});
