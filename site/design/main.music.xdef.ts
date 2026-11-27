import { defineMusic } from "omui/define";

export default defineMusic({
  doc: `The site's show, scored: the page drawing itself. Ten bars at 100 beats a
minute — a bar is 2.4 s, the show's usual beat — in C major: a soft pad
opens; a pluck's eighths come in with the first parts (Cmaj7 Am7 Fmaj7
G6); a bell, a bass and a light kit as the page fills (Cmaj7 Em7 Fmaj7
G7); the chord it ends on. Each beat of the show that changes what is
seen is a cue on its own first frame (the direction's \`cue\`,
main.sound.xdef.ts's \`step\`), so the hits land on the picture's beats;
the score is the bed under them. Played once (the arrangement's intro; its
loop is a bar of rest, after the film).`,
  name: "site-show",
  tempo: 100,
  meter: [4, 4],
  gain: 0.42,

  patches: {
    soft_pad: {
      base: "pad",
      filter: { cutoff: 900, envelope: 0.8 },
    },

    pluck: {
      base: "fm_pluck",
      fm: { ratio: 2, index: 1.2, decay: 0.18 },
      amp: { decay: 0.4, release: 0.2 },
    },

    chime: {
      base: "bell",
      fm: { ratio: 3.5, index: 1.1, decay: 0.9 },
      amp: { attack: 0.001, decay: 1.4, sustain: 0, release: 0.4 },
    },
  },

  instruments: {
    pad: {
      patch: "soft_pad",
      bus: "pads",
      gain: 0.34,
      legato: 1,
      voices: 8,
      sends: { hall: 0.4 },
    },

    pluck: {
      patch: "pluck",
      gain: 0.42,
      pan: -0.15,
      voices: 8,
      humanize: 0.1,
      sends: { hall: 0.2, delay: 0.18 },
    },

    bell: {
      patch: "chime",
      gain: 0.38,
      pan: 0.2,
      voices: 6,
      sends: { hall: 0.45 },
    },

    bass: {
      patch: "pluck_bass",
      bus: "low",
      gain: 0.62,
      legato: 0.6,
    },

    kick: {
      patch: "kick",
      bus: "drums",
      gain: 0.42,
    },

    snare: {
      patch: "snare",
      bus: "drums",
      gain: 0.26,
      sends: { hall: 0.2 },
    },

    hat: {
      patch: "hat",
      bus: "drums",
      gain: 0.28,
      pan: 0.25,
      humanize: 0.3,
    },
  },

  buses: {
    hall: {
      effects: [{ kind: "reverb", size: 0.8, damping: 0.5, predelay: 0.02, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 200 }],
    },

    delay: {
      effects: [{ kind: "echo", beats: 0.75, feedback: 0.25, pingpong: true, wet: 1, dry: 0 }, { kind: "highpass", cutoff: 300 }, { kind: "lowpass", cutoff: 5000 }],
    },

    pads: {
      effects: [{ kind: "chorus", delay: 0.014, depth: 0.004, rate: 0.2, wet: 0.5 }],
    },

    drums: {},

    low: {
      duck: { key: "drums", depth: 0.3, threshold: 0.3, attack: 0.002, release: 0.1 },
    },
  },

  patterns: {
    // ── the page opens ───────────────────────────────────────────────────────
    open_pad: {
      instrument: "pad",
      step: "1/1",
      notes: "C3+G3+B3+E4",
    },

    // ── its parts come: the mark, the sentence, the key, the install ────────
    pad_a: {
      instrument: "pad",
      step: "1/1",
      notes: "C3+E3+G3+B3 | A2+C3+E3+G3 | F2+A2+C3+E3 | G2+B2+D3+E3",
    },

    arp_a: {
      instrument: "pluck",
      step: "1/8",
      notes: `C5 G5 E5 G5 B5 G5 E5 G5 |
A4 E5 C5 E5 G5 E5 C5 E5 |
F4 C5 A4 C5 E5 C5 A4 C5 |
G4 D5 B4 D5 E5 D5 B4 D5 |
`,
    },

    // ── the page fills: Mui, the person's words, its documents ──────────────
    pad_b: {
      instrument: "pad",
      step: "1/1",
      notes: "C3+E3+G3+B3 | E3+G3+B3+D4 | F2+A2+C3+E3 | G2+B2+D3+F3",
    },

    arp_b: {
      instrument: "pluck",
      step: "1/8",
      notes: `C5 G5 E5 G5 B5 G5 E5 G5 |
E5 B5 G5 B5 D6 B5 G5 B5 |
F4 C5 A4 C5 E5 C5 A4 C5 |
G4 D5 B4 D5 F5 D5 B4 D5 |
`,
    },

    bell_b: {
      instrument: "bell",
      step: "1/4",
      notes: "E6:2 G6 E6 | D6:4 | C6:2 A5 C6 | B5:3 D6 |",
    },

    bass_b: {
      instrument: "bass",
      step: "1/8",
      notes: `C2! . G2 . C2 . G2 . | E2! . B2 . E2 . B2 . | F2! . C3 . F2 . C3 . | G2! . D3 . G2 . D3 . |
`,
    },

    kit: {
      step: "1/16",

      lanes: {
        kick: "x.......x.......",
        snare: "....x.......x...",
        hat: "..x...x...x...x.",
      },
    },

    // ── the page as it ships ─────────────────────────────────────────────────
    end_pad: {
      instrument: "pad",
      step: "1/1",
      notes: "C3+G3+D4+E4",
    },

    end_bell: {
      instrument: "bell",
      step: "1/2",
      notes: "C6 G6",
    },
  },

  sections: {
    open: {
      bars: 1,
      layers: [{ pattern: "open_pad" }],
    },

    a: {
      bars: 4,
      layers: [{ pattern: "pad_a" }, { pattern: "arp_a" }],
    },

    b: {
      bars: 4,
      layers: [{ pattern: "pad_b" }, { pattern: "arp_b" }, { pattern: "bell_b" }, { pattern: "bass_b" }, { pattern: "kit", times: 4 }],
    },

    end: {
      bars: 1,
      layers: [{ pattern: "end_pad" }, { pattern: "end_bell" }],
    },

    // After the show: nothing (an arrangement loops something; a film is over by then).
    rest: {
      bars: 1,
      layers: [],
    },
  },

  arrangement: {
    intro: ["open", "a", "b", "end"],
    loop: ["rest"],
  },
});
