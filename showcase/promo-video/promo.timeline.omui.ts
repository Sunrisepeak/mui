import { defineTimeline } from "omui/define";

export default defineTimeline({
  doc: `openmui's film: sixty seconds at 60 frames a second, 120 beats a minute.
Each picture clip is a live openmui scene (\`shot:<name>\`, drawn by the
scene code in src/shots) or the film's own frames (\`film\`: played back at
its speed — here, backwards five times over: the rewind). Its keyed
properties are the scene's camera and choreography, in the clip's own
time. Every cut and every marked hit lies on a beat of the music.`,
  name: "promo",
  rate: 60,
  sample_rate: 48000,
  size: [1920, 1080],

  markers: [
    // ── chapters and the hits the picture lands on (each on a beat) ──────────
    { at: "0s", name: "chapter:hook" },
    { at: "0s", name: "hit:pixel" },
    { at: "4s", name: "chapter:title" },
    { at: "6s", name: "hit:wordmark" },
    { at: "8s", name: "chapter:code" },
    { at: "12s", name: "hit:cpp" },
    { at: "14s", name: "hit:both" },
    { at: "16s", name: "chapter:infinite" },
    { at: "24s", name: "hit:dive" },
    { at: "28s", name: "chapter:styles" },
    { at: "29s", name: "hit:glass" },
    { at: "30s", name: "hit:neon" },
    { at: "31s", name: "hit:retro" },
    { at: "32s", name: "hit:ink" },
    { at: "33s", name: "hit:liquid" },
    { at: "34s", name: "hit:blueprint" },
    { at: "36s", name: "chapter:agent" },
    { at: "40.5s", name: "hit:needs-answer" },
    { at: "42.5s", name: "hit:answered" },
    { at: "46s", name: "chapter:time" },
    { at: "50s", name: "hit:rewind" },
    { at: "51s", name: "hit:replay" },
    { at: "55s", name: "chapter:end" },
    { at: "58.5s", name: "hit:sting" },
  ],

  tracks: [
    // ── the picture ──────────────────────────────────────────────────────────
    {
      name: "picture",
      kind: "video",

      clips: [
        {
          name: "origin",
          source: "shot:origin",
          at: "0f",
          length: "8s",

          properties: [
            // log2 of the screen pixels a pixel of the panel covers: one pixel the size
            // of the screen's height, then the camera pulls back to the whole panel.
            {
              name: "magnify",
              keys: [["0s", 9.4, "linear"], ["1.5s", 9.1, "in_out_cubic"], ["4.6s", -0.35, "out_cubic"], ["6s", -0.75]],
            },
            // The panel's semantic tree growing beside it, then its lines gathering
            // into the wordmark (on the bar), then the band of light.
            { name: "tree", keys: [["4s", 0, "out_cubic"], ["5.2s", 1]] },
            { name: "gather", keys: [["5.25s", 0, "in_out_cubic"], ["6s", 1]] },
            { name: "sweep", keys: [["6.1s", 0, "in_out_sine"], ["7.2s", 1]] },
          ],
        },
        {
          name: "code",
          source: "shot:code",
          at: "8s",
          length: "8s",

          properties: [
            // How much of tally.ts is typed (its characters, one after another).
            { name: "typed", keys: [["0.3s", 0, "linear"], ["3.7s", 1]] },
            // Which version is shown: 0 TypeScript, 1 C++, 2 both side by side.
            { name: "language", keys: [["0s", 0, "hold"], ["4s", 1, "hold"], ["6s", 2, "hold"]] },
          ],
        },
        {
          name: "infinite",
          source: "shot:infinite",
          at: "16s",
          length: "12s",

          properties: [
            // How far out the camera is: 0 a page, live, 1 the whole wall (the zoom's
            // logarithm between them).
            {
              name: "out",
              keys: [["0s", 0, "in_out_sine"], ["0.8s", 0.02, "in_out_cubic"], ["5.6s", 1, "linear"], ["8s", 0.98, "in_out_cubic"], ["11.2s", 0]],
            },
            // Where it looks: 0 the first page, 1 the wall's middle, 2 the second page
            // (slow while it is close, fast while it is far).
            {
              name: "pan",
              keys: [["0.8s", 0, "in_cubic"], ["5.6s", 1, "linear"], ["8s", 1.04, "out_cubic"], ["11s", 2]],
            },
          ],
        },
        {
          name: "styles",
          source: "shot:styles",
          at: "28s",
          length: "8s",

          properties: [
            // The style pack shown: 0 blueprint, 1 glass, 2 neon, 3 retro, 4 ink, 5 liquid.
            {
              name: "style",
              keys: [["0s", 0, "hold"], ["1s", 1, "hold"], ["2s", 2, "hold"], ["3s", 3, "hold"], ["4s", 4, "hold"], ["5s", 5, "hold"], ["6s", 0, "hold"]],
            },
          ],
        },
        {
          name: "agent",
          source: "shot:agent",
          at: "36s",
          length: "10s",

          properties: [
            // The agent's steps: 1 looks, 2 stars a note, 3 undoes it, 4 asks to delete
            // every note (needs an answer), 5 the person answers, 6 the journal.
            {
              name: "step",
              keys: [["0s", 0, "hold"], ["0.5s", 1, "hold"], ["1.5s", 2, "hold"], ["3s", 3, "hold"], ["4.5s", 4, "hold"], ["6.5s", 5, "hold"], ["8s", 6, "hold"]],
            },
          ],
        },
        { name: "time", source: "shot:time", at: "46s", length: "4s" },
        { name: "rewind", source: "film", at: "50s", length: "1s", in: "2999f", speed: -5 },
        {
          name: "replay",
          source: "shot:time",
          at: "51s",
          length: "4s",

          properties: [
            // The same game again: its checksum set beside the first play's, from a run of its own.
            { name: "compare", keys: [["0s", 1]] },
          ],
        },
        {
          name: "end",
          source: "shot:end",
          at: "55s",
          length: "5s",
          fade: { in: "0f", out: "0.5s", ease: "in_quad" },

          properties: [
            // Back into the pixel; the command typed; the closing line.
            { name: "dive", keys: [["0s", -0.6, "in_expo"], ["1.2s", 9.6]] },
            { name: "typed", keys: [["1.3s", 0, "linear"], ["2.1s", 1]] },
            // The command held whole a second, then the card, landing with the sting.
            { name: "close", keys: [["3.1s", 0, "out_cubic"], ["3.6s", 1]] },
          ],
        },
      ],
    },
    // ── the lines ────────────────────────────────────────────────────────────
    {
      name: "subtitles",
      kind: "events",

      clips: [
        { name: "hook", source: "line:short", at: "0.6s", length: "2.9s" },
        { name: "title", source: "line:title", at: "4.5s", length: "3.3s" },
        { name: "code", source: "line:code", at: "8.5s", length: "3.3s" },
        { name: "languages", source: "line:languages", at: "12s", length: "3.8s" },
        { name: "infinite", source: "line:infinite", at: "16.5s", length: "5s" },
        { name: "tiers", source: "line:tiers", at: "22s", length: "5.6s" },
        { name: "styles", source: "line:styles", at: "28.5s", length: "7.2s" },
        { name: "agents", source: "line:agents", at: "36.5s", length: "3.8s" },
        { name: "effects", source: "line:effects", at: "40.5s", length: "5.2s" },
        { name: "time", source: "line:time", at: "46.5s", length: "3.3s" },
        { name: "replay", source: "line:replay", at: "51.5s", length: "3.3s" },
      ],
    },
    // ── sound ────────────────────────────────────────────────────────────────
    {
      name: "music",
      kind: "audio",

      clips: [
        { name: "score", source: "music/promo.music.omui.ts", at: "0f", length: "50s" },
        {
          name: "score again",
          source: "music/promo.music.omui.ts",
          at: "51s",
          length: "9s",
          in: "46s",
          fade: { in: "0f", out: "0.6s", ease: "in_quad" },
        },
      ],
    },
    {
      name: "sfx",
      kind: "events",

      clips: [
        { name: "ping", source: "sound:ping", at: "0f", length: "1s" },
        { name: "pull back", source: "sound:whoosh", at: "2s", length: "1s" },
        { name: "wordmark", source: "sound:sting", at: "6s", length: "1s" },
        { name: "dive", source: "sound:whoosh", at: "23.5s", length: "1s" },
        { name: "tape", source: "sound:tape", at: "50s", length: "1s" },
        { name: "back to the pixel", source: "sound:whoosh", at: "55s", length: "1s" },
        { name: "closing", source: "sound:sting", at: "58.5s", length: "1s" },
      ],
    },
  ],
});
