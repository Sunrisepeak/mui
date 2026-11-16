// xengine's profile (D-207): a game made with xengine, beyond the general rules. The tool kernel's doctor
// (mlibs.project) reads the snapshot.
import { defineProfile } from "mlibs/define";

export default defineProfile({
  game: {
    doc: "a game: playable on its own (no window needed) and played by an agent in a test",
    rules: [
      {
        id: "game.headless",
        doc: "it runs on xengine's runtime, a game on its own that an agent can play",
        anyOf: [{ dependsOn: "xengine.runtime" }],
        fix: "make the game a Playable of xengine.runtime: its steps, what a player is shown, its controls",
      },
      {
        id: "game.playtest",
        doc: "a test in tests/ that plays it headless",
        file: "tests/**.cpp",
        fix: "play it as an agent does (xengine.playtest), the same play twice the same",
      },
    ],
  },
});
