// The fighters' voices (xengine.audio's recording layers over voice/*.wav,
// CC0 takes chosen with `omui-music takes` by length, pitch and brightness):
// `<fighter>_hurt_<n>` when a heavy blow or a counter lands on them,
// `<fighter>_ko` when they fall, `<fighter>_effort_<n>` with their own heavy
// blows. Each fighter's takes are processed into their character.

import { defineSound } from "omui/define";

export default defineSound({
  // ── 墨: composed — the takes a touch slower and softened (lowpass 7 kHz) ────────
  mo_hurt_1: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_3",
        speed: 0.97,
        filter: { kind: "lowpass", cutoff: 7000 },
      },
    ],
  },

  mo_hurt_2: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_1",
        speed: 0.97,
        filter: { kind: "lowpass", cutoff: 7000 },
      },
    ],
  },

  mo_ko: {
    gain: 0.6,

    layers: [
      {
        wave: "recording",
        recording: "female_5",
        speed: 0.97,
        filter: { kind: "lowpass", cutoff: 7000 },
      },
    ],
  },

  // ── 霓: sharp — quicker and higher, driven a little, the low end thinned (highpass 160 Hz) ────────
  ni_hurt_1: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_2",
        speed: 1.07,
        filter: { kind: "highpass", cutoff: 160 },
        drive: 0.15,
      },
    ],
  },

  ni_hurt_2: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_6",
        speed: 1.07,
        filter: { kind: "highpass", cutoff: 160 },
        drive: 0.15,
      },
    ],
  },

  ni_ko: {
    gain: 0.6,

    layers: [
      {
        wave: "recording",
        recording: "female_7",
        speed: 1.07,
        filter: { kind: "highpass", cutoff: 160 },
        drive: 0.15,
      },
    ],
  },

  // ── 坤: cool — lower and darker (lowpass 5.5 kHz), efforts held back ────────
  kun_hurt_1: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "strain_8",
        speed: 0.92,
        filter: { kind: "lowpass", cutoff: 5500 },
      },
    ],
  },

  kun_hurt_2: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "strain_1",
        speed: 0.92,
        filter: { kind: "lowpass", cutoff: 5500 },
      },
    ],
  },

  kun_hurt_3: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "strain_6",
        speed: 0.92,
        filter: { kind: "lowpass", cutoff: 5500 },
      },
    ],
  },

  kun_ko: {
    gain: 0.6,

    layers: [
      {
        wave: "recording",
        recording: "pain_12",
        speed: 0.92,
        filter: { kind: "lowpass", cutoff: 5500 },
      },
    ],
  },

  kun_effort_1: {
    gain: 0.4,

    layers: [
      {
        wave: "recording",
        recording: "strain_14",
        speed: 0.92,
        filter: { kind: "lowpass", cutoff: 5500 },
      },
    ],
  },

  kun_effort_2: {
    gain: 0.4,

    layers: [
      {
        wave: "recording",
        recording: "strain_11",
        speed: 0.92,
        filter: { kind: "lowpass", cutoff: 5500 },
      },
    ],
  },

  kun_effort_3: {
    gain: 0.4,

    layers: [
      {
        wave: "recording",
        recording: "strain_3",
        speed: 0.92,
        filter: { kind: "lowpass", cutoff: 5500 },
      },
    ],
  },

  // ── 璃: clear — lighter and quicker, the low end gone (highpass 300 Hz), a touch of air ────────
  li_hurt_1: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_3",
        speed: 1.12,
        filter: { kind: "highpass", cutoff: 300 },
      },
    ],
  },

  li_hurt_2: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_1",
        speed: 1.12,
        filter: { kind: "highpass", cutoff: 300 },
      },
    ],
  },

  li_ko: {
    gain: 0.6,

    layers: [
      {
        wave: "recording",
        recording: "female_4",
        speed: 1.12,
        filter: { kind: "highpass", cutoff: 300 },
      },
    ],
  },

  // ── 岳: a mountain — the lowest takes slowed to three quarters, darker (lowpass 3.2 kHz), driven ────────
  yue_hurt_1: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "strain_9",
        speed: 0.76,
        filter: { kind: "lowpass", cutoff: 3200 },
        drive: 0.2,
      },
    ],
  },

  yue_hurt_2: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "strain_13",
        speed: 0.76,
        filter: { kind: "lowpass", cutoff: 3200 },
        drive: 0.2,
      },
    ],
  },

  yue_ko: {
    gain: 0.6,

    layers: [
      {
        wave: "recording",
        recording: "pain_1",
        speed: 0.76,
        filter: { kind: "lowpass", cutoff: 3200 },
        drive: 0.2,
      },
    ],
  },

  yue_effort_1: {
    gain: 0.45,

    layers: [
      {
        wave: "recording",
        recording: "strain_5",
        speed: 0.76,
        filter: { kind: "lowpass", cutoff: 3200 },
        drive: 0.2,
      },
    ],
  },

  yue_effort_2: {
    gain: 0.45,

    layers: [
      {
        wave: "recording",
        recording: "strain_12",
        speed: 0.76,
        filter: { kind: "lowpass", cutoff: 3200 },
        drive: 0.2,
      },
    ],
  },

  // ── 刻: measured — lower and slower, softened (lowpass 4.5 kHz) ────────
  ke_hurt_1: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_2",
        speed: 0.9,
        filter: { kind: "lowpass", cutoff: 4500 },
      },
    ],
  },

  ke_hurt_2: {
    gain: 0.5,

    layers: [
      {
        wave: "recording",
        recording: "female_6",
        speed: 0.9,
        filter: { kind: "lowpass", cutoff: 4500 },
      },
    ],
  },

  ke_ko: {
    gain: 0.6,

    layers: [
      {
        wave: "recording",
        recording: "female_5",
        speed: 0.9,
        filter: { kind: "lowpass", cutoff: 4500 },
      },
    ],
  },
});
