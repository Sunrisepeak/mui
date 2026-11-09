// What the breakout sounds like (xengine.audio): each brick as the component
// it is — a button's click, a switch's snap, a capacitor's charge (pitched up
// a step a hit), its fuse and its beam — a pop when one breaks (the program
// pitches it up a pentatonic scale along the chain), a banked chain's chord and
// a lost one's slide, the core's knock and the panel powering down; the
// paddle's knock, the walls', a kicker's thump, a breakthrough's rush, the
// magnet's clunk, a capsule's chime and its catch, a ball lost, a room
// cleared, a launch, a gate opening, a rewind, a scan, a refusal.

import { defineSound } from "omui/define";

export default defineSound({
  paddle: {
    layers: [
      {
        wave: "sine",
        length: 0.07,
        pitch: [330, 220],
        envelope: { attack: 0.001, decay: 0.06, sustain: 0, release: 0.008 },
        drive: 0.15,
        gain: 0.7,
      },
      {
        wave: "noise",
        length: 0.02,
        envelope: { attack: 0.0005, decay: 0.018, sustain: 0, release: 0.002 },
        filter: { kind: "bandpass", cutoff: [2200, 1400], q: 1.2 },
        gain: 0.3,
        seed: 11,
      },
    ],
  },

  wall: {
    layers: [
      {
        wave: "sine",
        length: 0.04,
        pitch: [520, 420],
        envelope: { attack: 0.0005, decay: 0.035, sustain: 0, release: 0.004 },
        gain: 0.35,
      },
    ],
  },

  click: {
    layers: [
      {
        wave: "noise",
        length: 0.018,
        envelope: { attack: 0.0002, decay: 0.016, sustain: 0, release: 0.002 },
        filter: { kind: "highpass", cutoff: [4000, 2500], q: 0.8 },
        gain: 0.55,
        seed: 13,
      },
      {
        wave: "square",
        length: 0.03,
        pitch: [1400, 1100],
        envelope: { attack: 0.0005, decay: 0.025, sustain: 0, release: 0.004 },
        filter: { kind: "lowpass", cutoff: [5000, 3000], q: 0.7 },
        gain: 0.18,
      },
    ],
  },

  flip: {
    layers: [
      {
        wave: "sine",
        length: 0.05,
        pitch: [660, 660],
        envelope: { attack: 0.001, decay: 0.04, sustain: 0, release: 0.008 },
        gain: 0.4,
      },
      {
        wave: "sine",
        at: 0.045,
        length: 0.06,
        pitch: [990, 990],
        envelope: { attack: 0.001, decay: 0.05, sustain: 0, release: 0.01 },
        gain: 0.4,
      },
    ],
  },

  tick: {
    layers: [
      {
        wave: "saw",
        length: 0.035,
        pitch: [880, 600],
        envelope: { attack: 0.0005, decay: 0.03, sustain: 0, release: 0.004 },
        filter: { kind: "lowpass", cutoff: [4000, 1500], q: 1 },
        gain: 0.3,
      },
    ],
  },

  pop: {
    layers: [
      {
        wave: "sine",
        length: 0.11,
        pitch: [440, 880],
        envelope: { attack: 0.001, decay: 0.1, sustain: 0, release: 0.01 },
        gain: 0.6,
      },
      {
        wave: "noise",
        length: 0.05,
        envelope: { attack: 0.0005, decay: 0.045, sustain: 0, release: 0.005 },
        filter: { kind: "bandpass", cutoff: [3000, 6000], q: 1.5 },
        gain: 0.3,
        seed: 17,
      },
    ],
  },

  chime: {
    layers: [
      {
        wave: "sine",
        length: 0.25,
        pitch: [1320, 1320],
        envelope: { attack: 0.002, decay: 0.22, sustain: 0, release: 0.03 },
        gain: 0.3,
      },
      {
        wave: "sine",
        at: 0.06,
        length: 0.3,
        pitch: [1760, 1760],
        envelope: { attack: 0.002, decay: 0.27, sustain: 0, release: 0.03 },
        gain: 0.25,
      },
    ],
  },

  catch: {
    layers: [
      {
        wave: "square",
        length: 0.18,
        pitch: [520, 1560],
        envelope: { attack: 0.002, decay: 0.16, sustain: 0, release: 0.02 },
        filter: { kind: "lowpass", cutoff: [3000, 6000], q: 0.8 },
        gain: 0.3,
      },
    ],
  },

  lost: {
    layers: [
      {
        wave: "saw",
        length: 0.6,
        pitch: [300, 70],
        envelope: { attack: 0.005, decay: 0.5, sustain: 0, release: 0.1 },
        filter: { kind: "lowpass", cutoff: [2000, 300], q: 0.9 },
        drive: 0.25,
        gain: 0.55,
      },
    ],
  },

  cleared: {
    layers: [
      {
        wave: "square",
        length: 0.14,
        pitch: [523.25, 523.25],
        envelope: { attack: 0.002, decay: 0.1, sustain: 0.3, release: 0.04 },
        filter: { kind: "lowpass", cutoff: [4000, 4000], q: 0.7 },
        gain: 0.3,
      },
      {
        wave: "square",
        at: 0.12,
        length: 0.14,
        pitch: [659.25, 659.25],
        envelope: { attack: 0.002, decay: 0.1, sustain: 0.3, release: 0.04 },
        filter: { kind: "lowpass", cutoff: [4000, 4000], q: 0.7 },
        gain: 0.3,
      },
      {
        wave: "square",
        at: 0.24,
        length: 0.14,
        pitch: [783.99, 783.99],
        envelope: { attack: 0.002, decay: 0.1, sustain: 0.3, release: 0.04 },
        filter: { kind: "lowpass", cutoff: [4000, 4000], q: 0.7 },
        gain: 0.3,
      },
      {
        wave: "sine",
        at: 0.36,
        length: 0.6,
        pitch: [1046.5, 1046.5],
        envelope: { attack: 0.004, decay: 0.5, sustain: 0, release: 0.1 },
        gain: 0.4,
      },
    ],
  },

  launch: {
    layers: [
      {
        wave: "noise",
        length: 0.16,
        envelope: { attack: 0.02, decay: 0.13, sustain: 0, release: 0.01 },
        filter: { kind: "bandpass", cutoff: [600, 3000], q: 1.4 },
        gain: 0.35,
        seed: 19,
      },
    ],
  },

  gate: {
    layers: [
      {
        wave: "saw",
        length: 0.5,
        pitch: [110, 220],
        envelope: { attack: 0.02, decay: 0.4, sustain: 0, release: 0.08 },
        filter: { kind: "lowpass", cutoff: [400, 2400], q: 1.1 },
        gain: 0.45,
      },
    ],
  },

  rewind: {
    layers: [
      {
        wave: "saw",
        length: 0.3,
        pitch: [1200, 300],
        envelope: { attack: 0.01, decay: 0.25, sustain: 0, release: 0.04 },
        filter: { kind: "lowpass", cutoff: [5000, 800], q: 1.2 },
        gain: 0.35,
      },
    ],
  },

  denied: {
    layers: [
      {
        wave: "square",
        length: 0.16,
        pitch: [140, 120],
        envelope: { attack: 0.002, decay: 0.14, sustain: 0, release: 0.02 },
        filter: { kind: "lowpass", cutoff: [1200, 800], q: 0.8 },
        gain: 0.3,
      },
    ],
  },

  scan: {
    layers: [
      {
        wave: "sine",
        length: 0.5,
        pitch: [400, 2400],
        envelope: { attack: 0.02, decay: 0.45, sustain: 0, release: 0.04 },
        gain: 0.25,
      },
      {
        wave: "noise",
        length: 0.4,
        envelope: { attack: 0.05, decay: 0.33, sustain: 0, release: 0.03 },
        filter: { kind: "bandpass", cutoff: [800, 6000], q: 2 },
        gain: 0.15,
        seed: 23,
      },
    ],
  },

  order: {
    layers: [
      {
        wave: "sine",
        length: 0.08,
        pitch: [880, 1100],
        envelope: { attack: 0.002, decay: 0.07, sustain: 0, release: 0.01 },
        gain: 0.3,
      },
    ],
  },

  snap: {
    layers: [
      {
        wave: "noise",
        length: 0.012,
        envelope: { attack: 0.0002, decay: 0.011, sustain: 0, release: 0.001 },
        filter: { kind: "highpass", cutoff: [5000, 3500], q: 0.9 },
        gain: 0.6,
        seed: 29,
      },
      {
        wave: "square",
        at: 0.008,
        length: 0.05,
        pitch: [740, 980],
        envelope: { attack: 0.0005, decay: 0.045, sustain: 0, release: 0.005 },
        filter: { kind: "lowpass", cutoff: [4000, 3000], q: 0.7 },
        gain: 0.25,
      },
    ],
  },

  wire: {
    layers: [
      {
        wave: "saw",
        length: 0.12,
        pitch: [1800, 600],
        envelope: { attack: 0.002, decay: 0.11, sustain: 0, release: 0.01 },
        filter: { kind: "bandpass", cutoff: [3000, 1200], q: 2.5 },
        gain: 0.2,
      },
    ],
  },

  charge: {
    layers: [
      {
        wave: "sine",
        length: 0.12,
        pitch: [520, 780],
        envelope: { attack: 0.002, decay: 0.11, sustain: 0, release: 0.01 },
        gain: 0.45,
      },
      {
        wave: "square",
        length: 0.05,
        pitch: [1560, 1560],
        envelope: { attack: 0.001, decay: 0.045, sustain: 0, release: 0.004 },
        filter: { kind: "lowpass", cutoff: [4000, 3000], q: 0.7 },
        gain: 0.08,
      },
    ],
  },

  armed: {
    layers: [
      {
        wave: "saw",
        length: 0.09,
        pitch: [600, 1800],
        envelope: { attack: 0.004, decay: 0.08, sustain: 0, release: 0.01 },
        filter: { kind: "lowpass", cutoff: [2000, 6000], q: 1.5 },
        gain: 0.3,
      },
    ],
  },

  beam: {
    layers: [
      {
        wave: "saw",
        length: 0.45,
        pitch: [220, 110],
        envelope: { attack: 0.003, decay: 0.4, sustain: 0, release: 0.05 },
        filter: { kind: "lowpass", cutoff: [6000, 600], q: 1.1 },
        drive: 0.35,
        gain: 0.45,
      },
      {
        wave: "noise",
        length: 0.35,
        envelope: { attack: 0.002, decay: 0.32, sustain: 0, release: 0.03 },
        filter: { kind: "bandpass", cutoff: [7000, 900], q: 1.2 },
        gain: 0.35,
        seed: 31,
      },
    ],
  },

  kick: {
    layers: [
      {
        wave: "sine",
        length: 0.09,
        pitch: [180, 70],
        envelope: { attack: 0.001, decay: 0.08, sustain: 0, release: 0.01 },
        drive: 0.3,
        gain: 0.7,
      },
      {
        wave: "noise",
        length: 0.12,
        envelope: { attack: 0.01, decay: 0.1, sustain: 0, release: 0.01 },
        filter: { kind: "bandpass", cutoff: [900, 3500], q: 1.2 },
        gain: 0.25,
        seed: 37,
      },
    ],
  },

  core: {
    layers: [
      {
        wave: "sine",
        length: 0.3,
        pitch: [110, 92],
        envelope: { attack: 0.001, decay: 0.28, sustain: 0, release: 0.02 },
        drive: 0.4,
        gain: 0.7,
      },
      {
        wave: "square",
        length: 0.06,
        pitch: [660, 440],
        envelope: { attack: 0.001, decay: 0.05, sustain: 0, release: 0.01 },
        filter: { kind: "lowpass", cutoff: [3000, 1500], q: 0.8 },
        gain: 0.2,
      },
    ],
  },

  shutdown: {
    layers: [
      {
        wave: "saw",
        length: 1.2,
        pitch: [440, 40],
        envelope: { attack: 0.005, decay: 1.1, sustain: 0, release: 0.1 },
        filter: { kind: "lowpass", cutoff: [5000, 200], q: 1.3 },
        drive: 0.3,
        gain: 0.5,
      },
      {
        wave: "noise",
        length: 0.6,
        envelope: { attack: 0.001, decay: 0.55, sustain: 0, release: 0.05 },
        filter: { kind: "lowpass", cutoff: [8000, 300], q: 0.8 },
        gain: 0.4,
        seed: 41,
      },
    ],
  },

  breach: {
    layers: [
      {
        wave: "noise",
        length: 0.6,
        envelope: { attack: 0.05, decay: 0.5, sustain: 0, release: 0.05 },
        filter: { kind: "bandpass", cutoff: [400, 5000], q: 1.6 },
        gain: 0.45,
        seed: 43,
      },
      {
        wave: "sine",
        at: 0.05,
        length: 0.5,
        pitch: [330, 990],
        envelope: { attack: 0.01, decay: 0.45, sustain: 0, release: 0.04 },
        gain: 0.3,
      },
    ],
  },

  whoosh: {
    layers: [
      {
        wave: "noise",
        length: 0.25,
        envelope: { attack: 0.03, decay: 0.2, sustain: 0, release: 0.02 },
        filter: { kind: "bandpass", cutoff: [500, 3000], q: 1.4 },
        gain: 0.3,
        seed: 47,
      },
    ],
  },

  bank: {
    layers: [
      {
        wave: "sine",
        length: 0.45,
        pitch: [523.25, 523.25],
        envelope: { attack: 0.004, decay: 0.4, sustain: 0, release: 0.05 },
        gain: 0.3,
      },
      {
        wave: "sine",
        at: 0.03,
        length: 0.45,
        pitch: [659.25, 659.25],
        envelope: { attack: 0.004, decay: 0.4, sustain: 0, release: 0.05 },
        gain: 0.28,
      },
      {
        wave: "sine",
        at: 0.06,
        length: 0.5,
        pitch: [783.99, 783.99],
        envelope: { attack: 0.004, decay: 0.45, sustain: 0, release: 0.05 },
        gain: 0.28,
      },
    ],
  },

  chain_lost: {
    layers: [
      {
        wave: "square",
        length: 0.4,
        pitch: [660, 220],
        envelope: { attack: 0.004, decay: 0.36, sustain: 0, release: 0.04 },
        filter: { kind: "lowpass", cutoff: [3000, 600], q: 0.9 },
        gain: 0.3,
      },
    ],
  },

  magnet: {
    layers: [
      {
        wave: "square",
        length: 0.08,
        pitch: [300, 260],
        envelope: { attack: 0.001, decay: 0.07, sustain: 0, release: 0.01 },
        filter: { kind: "lowpass", cutoff: [1500, 900], q: 1.5 },
        gain: 0.45,
      },
    ],
  },

  duet: {
    layers: [
      {
        wave: "sine",
        length: 0.12,
        pitch: [990, 990],
        envelope: { attack: 0.002, decay: 0.11, sustain: 0, release: 0.01 },
        gain: 0.25,
      },
    ],
  },
});
