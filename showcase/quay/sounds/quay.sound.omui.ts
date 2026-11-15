// What the harbour at night sounds like (xengine.audio), each sound a place in the world (xengine.audio.space puts
// it there and the ears hear it through the containers' steel): the watchkeeper's rifle — a crack, the thump under
// it, the quay throwing it back — a warden's shot, an electric snap; steps on wet concrete; a warden's servos as it
// walks; a warden falling, its plates clanking; a hit taken; the beacon catching and roaring up; on the second night
// a lookout's horn — two low reeds a little apart, long — and a fuse box thrown, a clunk and the line's hum.

import { defineSound } from "omui/define";

export default defineSound({
  rifle: {
    layers: [
      {
        wave: "noise",
        length: 0.16,
        envelope: { attack: 0.0003, decay: 0.15, sustain: 0, release: 0.01 },
        filter: { kind: "lowpass", cutoff: [9000, 1400], q: 0.8 },
        drive: 0.45,
        gain: 0.9,
        seed: 21,
      },
      {
        wave: "sine",
        length: 0.14,
        pitch: [130, 48],
        envelope: { attack: 0.0005, decay: 0.12, sustain: 0, release: 0.015 },
        drive: 0.3,
        gain: 0.8,
      },
      {
        wave: "noise",
        at: 0.05,
        length: 0.55,
        envelope: { attack: 0.03, decay: 0.5, sustain: 0, release: 0.02 },
        filter: { kind: "bandpass", cutoff: [900, 380], q: 0.9 },
        gain: 0.16,
        seed: 22,
      },
    ],
  },

  horn: {
    layers: [
      { wave: "saw", length: 1.3, pitch: 98, envelope: { attack: 0.08, decay: 0.2, sustain: 0.8, release: 0.3 }, filter: { kind: "lowpass", cutoff: [900, 600], q: 0.9 }, gain: 0.32 },
      { wave: "saw", length: 1.3, pitch: 98.8, envelope: { attack: 0.1, decay: 0.2, sustain: 0.8, release: 0.3 }, filter: { kind: "lowpass", cutoff: [800, 550], q: 0.9 }, gain: 0.28 },
      { wave: "noise", length: 1.2, envelope: { attack: 0.05, decay: 0.3, sustain: 0.4, release: 0.3 }, filter: { kind: "bandpass", cutoff: [300, 200], q: 1.2 }, gain: 0.06, seed: 61 },
    ],
  },

  fuse: {
    layers: [
      { wave: "noise", length: 0.06, envelope: { attack: 0.0005, decay: 0.05, sustain: 0, release: 0.005 }, filter: { kind: "bandpass", cutoff: [1800, 900], q: 1.4 }, gain: 0.5, seed: 71 },
      { wave: "sine", at: 0.04, length: 0.8, pitch: 100, envelope: { attack: 0.05, decay: 0.3, sustain: 0.3, release: 0.3 }, gain: 0.12 },
    ],
  },

  "warden-shot": {
    layers: [
      {
        wave: "square",
        length: 0.09,
        pitch: [1100, 180],
        envelope: { attack: 0.0004, decay: 0.08, sustain: 0, release: 0.008 },
        filter: { kind: "lowpass", cutoff: [6000, 1200], q: 1.4 },
        gain: 0.35,
      },
      {
        wave: "noise",
        length: 0.07,
        envelope: { attack: 0.0002, decay: 0.065, sustain: 0, release: 0.004 },
        filter: { kind: "highpass", cutoff: [2500, 1200], q: 0.7 },
        drive: 0.3,
        gain: 0.6,
        seed: 31,
      },
      {
        wave: "noise",
        at: 0.04,
        length: 0.45,
        envelope: { attack: 0.03, decay: 0.4, sustain: 0, release: 0.02 },
        filter: { kind: "bandpass", cutoff: [700, 320], q: 0.9 },
        gain: 0.12,
        seed: 32,
      },
    ],
  },

  step: {
    layers: [
      {
        wave: "noise",
        length: 0.07,
        envelope: { attack: 0.002, decay: 0.06, sustain: 0, release: 0.006 },
        filter: { kind: "bandpass", cutoff: [1300, 700], q: 1.1 },
        gain: 0.32,
        seed: 41,
      },
      {
        wave: "sine",
        length: 0.05,
        pitch: [95, 60],
        envelope: { attack: 0.001, decay: 0.045, sustain: 0, release: 0.004 },
        gain: 0.3,
      },
      {
        wave: "noise",
        at: 0.015,
        length: 0.09,
        envelope: { attack: 0.01, decay: 0.07, sustain: 0, release: 0.01 },
        filter: { kind: "highpass", cutoff: [3500, 2500], q: 0.7 },
        gain: 0.08,
        seed: 42,
      },
    ],
  },

  servo: {
    layers: [
      {
        wave: "saw",
        length: 0.32,
        pitch: [150, 185],
        envelope: { attack: 0.03, decay: 0.05, sustain: 0.7, release: 0.08 },
        filter: { kind: "lowpass", cutoff: [700, 900], q: 2.2 },
        gain: 0.12,
      },
      {
        wave: "noise",
        at: 0.28,
        length: 0.05,
        envelope: { attack: 0.001, decay: 0.045, sustain: 0, release: 0.004 },
        filter: { kind: "bandpass", cutoff: [500, 400], q: 1.5 },
        gain: 0.22,
        seed: 51,
      },
    ],
  },

  "warden-down": {
    layers: [
      {
        wave: "triangle",
        length: 0.5,
        pitch: [420, 95],
        envelope: { attack: 0.001, decay: 0.45, sustain: 0, release: 0.04 },
        filter: { kind: "lowpass", cutoff: [5000, 900], q: 1.8 },
        gain: 0.4,
      },
      {
        wave: "noise",
        at: 0.32,
        length: 0.25,
        envelope: { attack: 0.001, decay: 0.22, sustain: 0, release: 0.02 },
        filter: { kind: "bandpass", cutoff: [1800, 700], q: 2.5 },
        gain: 0.45,
        seed: 61,
      },
      {
        wave: "sine",
        at: 0.32,
        length: 0.2,
        pitch: [80, 45],
        envelope: { attack: 0.001, decay: 0.18, sustain: 0, release: 0.015 },
        gain: 0.5,
      },
    ],
  },

  hit: {
    layers: [
      {
        wave: "sine",
        length: 0.18,
        pitch: [110, 55],
        envelope: { attack: 0.001, decay: 0.16, sustain: 0, release: 0.015 },
        drive: 0.4,
        gain: 0.7,
      },
      {
        wave: "noise",
        length: 0.06,
        envelope: { attack: 0.0005, decay: 0.055, sustain: 0, release: 0.004 },
        filter: { kind: "lowpass", cutoff: [1600, 600], q: 0.8 },
        gain: 0.4,
        seed: 71,
      },
    ],
  },

  beacon: {
    layers: [
      {
        wave: "noise",
        length: 1.6,
        envelope: { attack: 0.5, hold: 0.4, decay: 0.6, sustain: 0.2, release: 0.1 },
        filter: { kind: "lowpass", cutoff: [300, 2600], q: 0.9 },
        gain: 0.55,
        seed: 81,
      },
      {
        wave: "sine",
        length: 1.6,
        pitch: [52, 60],
        envelope: { attack: 0.4, hold: 0.5, decay: 0.6, sustain: 0.2, release: 0.1 },
        gain: 0.45,
      },
      {
        wave: "noise",
        at: 0.35,
        length: 1.1,
        envelope: { attack: 0.2, decay: 0.85, sustain: 0, release: 0.05 },
        filter: { kind: "bandpass", cutoff: [3000, 5000], q: 1.2 },
        gain: 0.1,
        seed: 82,
      },
    ],
  },
});
