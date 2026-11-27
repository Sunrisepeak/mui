// The site's show, heard (meng.audio): a beat that changes what is seen is a
// soft chime on its first frame (the show's `sfx` track, the direction's
// `cue`) — in the score's C major, an E6 over its octave, under the score.

import { defineSound } from "omui/define";

export default defineSound({
  step: {
    layers: [
      {
        wave: "sine",
        length: 0.9,
        pitch: 1318.5,
        envelope: { attack: 0.002, decay: 0.85, sustain: 0, release: 0.05 },
        gain: 0.16,
      },
      {
        wave: "sine",
        length: 0.5,
        pitch: 2637,
        envelope: { attack: 0.001, decay: 0.45, sustain: 0, release: 0.05 },
        gain: 0.05,
        pan: 0.15,
      },
    ],
  },
});
