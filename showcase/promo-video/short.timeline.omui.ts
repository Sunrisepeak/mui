import { defineTimeline } from "omui/define";

export default defineTimeline({
  doc: `The fifteen-second cut: the hook, the unbroken shot through the wall,
the styles on every beat, the command. The same scenes as the film's,
with their own camera and choreography keyed here; rendered 16:9 as
written, or 9:16 with --size 1080x1920 (every scene lays itself out for
either).`,
  name: "short",
  rate: 60,
  sample_rate: 48000,
  size: [1920, 1080],

  markers: [
    { at: "0s", name: "chapter:hook" },
    { at: "0s", name: "hit:pixel" },
    { at: "3s", name: "chapter:infinite" },
    { at: "5s", name: "hit:dive" },
    { at: "9s", name: "chapter:styles" },
    { at: "12s", name: "chapter:end" },
    { at: "14s", name: "hit:sting" },
  ],

  tracks: [
    {
      name: "picture",
      kind: "video",

      clips: [
        {
          name: "hook",
          source: "shot:origin",
          at: "0f",
          length: "3s",

          properties: [
            { name: "magnify", keys: [["0s", 9.4, "linear"], ["1s", 9.2, "in_out_cubic"], ["3s", -0.35]] },
          ],
        },
        {
          name: "infinite",
          source: "shot:infinite",
          at: "3s",
          length: "6s",

          properties: [
            {
              name: "out",
              keys: [["0s", 0, "in_out_cubic"], ["2s", 1, "linear"], ["2.4s", 0.98, "in_out_cubic"], ["5.2s", 0]],
            },
            {
              name: "pan",
              keys: [["0s", 0, "in_cubic"], ["2s", 1, "linear"], ["2.4s", 1.04, "out_cubic"], ["5s", 2]],
            },
          ],
        },
        {
          name: "styles",
          source: "shot:styles",
          at: "9s",
          length: "3s",

          properties: [
            {
              name: "style",
              keys: [["0s", 1, "hold"], ["0.5s", 2, "hold"], ["1s", 3, "hold"], ["1.5s", 4, "hold"], ["2s", 5, "hold"], ["2.5s", 0, "hold"]],
            },
          ],
        },
        {
          name: "end",
          source: "shot:end",
          at: "12s",
          length: "3s",
          fade: { in: "0f", out: "0.4s", ease: "in_quad" },

          properties: [
            { name: "dive", keys: [["0s", 9.6, "hold"]] },
            { name: "typed", keys: [["0.1s", 0, "linear"], ["1.1s", 1]] },
            { name: "close", keys: [["1.4s", 0, "out_cubic"], ["2.1s", 1]] },
          ],
        },
      ],
    },
    {
      name: "subtitles",
      kind: "events",

      clips: [
        { name: "hook", source: "line:short", at: "0.5s", length: "2.4s" },
        { name: "infinite", source: "line:infinite", at: "3.4s", length: "5.4s" },
        { name: "styles", source: "line:styles", at: "9.1s", length: "2.8s" },
      ],
    },
    {
      name: "music",
      kind: "audio",

      clips: [
        {
          name: "score",
          source: "music/promo.music.omui.ts",
          at: "0f",
          length: "15s",
          in: "19s",
          fade: { in: "0.3s", out: "0.5s", ease: "in_quad" },
        },
      ],
    },
    {
      name: "sfx",
      kind: "events",

      clips: [
        { name: "ping", source: "sound:ping", at: "0f", length: "1s" },
        { name: "dive", source: "sound:whoosh", at: "4.5s", length: "1s" },
        { name: "closing", source: "sound:sting", at: "14s", length: "1s" },
      ],
    },
  ],
});
