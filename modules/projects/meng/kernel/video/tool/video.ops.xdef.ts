// The video toolkit's operations (convergence plan RC3.3, D-204): what an Agent does with films without a GUI,
// through the `video` command — the subject `video` behind the tool kernel's entries (`video render card.video.xdef.ts
// --out card.webm`, `video mcp`; records out, exit 0 done, 1 failed with why, 2 a request that is not one). Frames
// come from the built-in sync card or PNG files; a view's frames are omui's, given by a composition (xtools) that
// adds omui's view source — the same operations there take a view document. ops-gen makes the catalog
// (src/catalog/catalog.cppm), the listing (video.ops.json) and the reference (video.ops.md).
import { defineOps } from "mlibs/define";

export default defineOps({
  product: "video",
  module: "meng.video.tool",
  namespace: "meng::video::tool",
  doc: "Films on exact frame and sample counts: render a description to WebM, Y4M, PNG or WAV, take a still, check a description, probe a file, show the timing. A view's frames are omui's, through a composition that adds them.",
  generated: "src/catalog",
  operations: [
    {
      name: "video.render",
      effect: "reversible",
      summary: "A video rendered to files on exact frame and sample counts: WebM (VP9 and Opus), Y4M (and a WAV beside it), a PNG sequence, or a WAV",
      params: [
        { name: "file", type: "path", doc: "a video description (*.video.xdef.ts, or its .json) (or, where a composition adds omui's view source, a view document *.view.xdef.json)" },
        { name: "out", type: "path", doc: "x.webm, x.y4m, x.wav, shots/{:05}.png or shots/" },
        { name: "size", type: "string", doc: "WxH (a view's default: 1280x720)", optional: true },
        { name: "fps", type: "string", doc: "frames a second: 30, 30000/1001, 29.97 (a view's default: 30)", optional: true },
        { name: "seconds", type: "number", doc: "how long (a view's default: 3)", optional: true },
        { name: "frames", type: "integer", doc: "how many frames", optional: true },
        { name: "format", type: "string", doc: "webm, y4m, png or wav (default: from out)", optional: true },
        { name: "quality", type: "integer", doc: "WebM: VP9 at constant quality, 0 (best) … 63 (default 24)", optional: true },
        { name: "bitrate", type: "integer", doc: "WebM: a target bit rate (kbps) instead of a quality", optional: true },
        { name: "speed", type: "integer", doc: "WebM: libvpx's speed, 0 (smallest) … 9 (fastest) (default 5)", optional: true },
        { name: "realtime", type: "bool", doc: "WebM: libvpx's real-time mode", optional: true },
        { name: "lossless", type: "bool", doc: "WebM: every Y'CbCr sample kept", optional: true },
        { name: "threads", type: "integer", doc: "WebM: encoder threads (1, the default, gives the same bytes everywhere)", optional: true },
        { name: "audio-bitrate", type: "integer", doc: "WebM: Opus kbps (default 128)", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "delete the files written",
    },
    {
      name: "video.still",
      effect: "reversible",
      summary: "One frame of a video as a PNG",
      params: [
        { name: "file", type: "path", doc: "a video description (*.video.xdef.ts, or its .json) (or, where a composition adds omui's view source, a view document *.view.xdef.json)" },
        { name: "out", type: "path", doc: "the PNG" },
        { name: "frame", type: "integer", doc: "the frame", optional: true },
        { name: "at", type: "number", doc: "the frame shown at a time, seconds", optional: true },
        { name: "size", type: "string", doc: "WxH (a view's default: 1280x720)", optional: true },
        { name: "fps", type: "string", doc: "frames a second: 30, 30000/1001, 29.97 (a view's default: 30)", optional: true },
        { name: "seconds", type: "number", doc: "how long (a view's default: 3)", optional: true },
        { name: "frames", type: "integer", doc: "how many frames", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the PNG",
    },
    { name: "video.check", effect: "read", summary: "Whether a video description reads: its problems, or its size, rate, frames and samples", params: [
        { name: "file", type: "path", doc: "a video description (*.video.xdef.ts, or its .json) (or, where a composition adds omui's view source, a view document *.view.xdef.json)" },
        { name: "size", type: "string", doc: "WxH (a view's default: 1280x720)", optional: true },
        { name: "fps", type: "string", doc: "frames a second: 30, 30000/1001, 29.97 (a view's default: 30)", optional: true },
        { name: "seconds", type: "number", doc: "how long (a view's default: 3)", optional: true },
        { name: "frames", type: "integer", doc: "how many frames", optional: true },
      ], result: "json", cost: "bounded" },
    { name: "video.probe", effect: "read", summary: "What a WebM, Y4M or WAV file holds (a WebM decoded: its frames and samples counted)", params: [{ name: "file", type: "path", doc: "a .webm, .y4m or .wav file" }], result: "json", cost: "bounded" },
    {
      name: "video.timing",
      effect: "read",
      summary: "Each frame's first sample and length, and its start in nanoseconds",
      params: [
        { name: "fps", type: "string", doc: "30, 30000/1001, 29.97", optional: true },
        { name: "rate", type: "integer", doc: "samples a second (48000)", optional: true },
        { name: "frames", type: "integer", doc: "how many (default: a second's)", optional: true },
      ],
      result: "json",
      cost: "instant",
    },
  ],
});
