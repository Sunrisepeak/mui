# xengine.video.tool

The `video` command: films on exact frame and sample counts without a GUI, for people, agents and CI.

```sh
video render intro.video.xdef.ts --out intro.webm            # VP9 and Opus; x.y4m (a WAV beside it), x.wav, shots/{:05}.png or shots/
video still  intro.video.xdef.ts --at 2.5 --out f.png        # one frame
video check  intro.video.xdef.ts                             # its problems at their lines, or its size, rate, frames and samples
video probe  intro.webm                                      # what a WebM, Y4M or WAV holds (a WebM decoded: frames and samples counted)
video timing --fps 30000/1001 --frames 5                     # each frame's first sample, length and start
xtools video render hello.view.xdef.json --out shots/        # an omui view's frames, composed in by xtools
```

The command is the subject `video` behind the tool kernel's entries (mlibs.agent:host): records on standard output, a line each, the data first and the result last; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request. `video mcp` offers the same operations to an MCP client. A description is a `*.video.xdef.ts` data module (evaluated with its workspace's kinds, mlibs.xdef) or its JSON: the sync card or PNG files, with the sound track [xengine.video](..) renders offline.

Films are xengine.video's; the frames of an omui view are omui's (a view drawn on the headless host's clock). This package does not know omui (the layers go one way): a view's frames come through `Sources::view`, which a composition fills — [xtools](../../../../xtools) gives the subject omui's `view_frames`, and omui's `design film` renders through `run` with it. Without one, a view is refused with the way out.

Its operations are a catalog ([`video.ops.xdef.ts`](video.ops.xdef.ts), listed in [`src/catalog`](src/catalog)); how an Agent uses them is [`skill.omui.md`](skill.omui.md). Its own package: the WebM codec and the data evaluator are a tool's dependencies, not the library's (RC3.3: moved here from omui's command line).

Tests (`tests/tool_test.cpp`): a TypeScript description rendered to Y4M and its WAV and to WebM, probed back with exact frame and sample counts; a still at a time; the timing of 29.97 fps; a misspelt key at its line; a request without its output refused before it runs; a view refused with the way out. A view's film through the composition is xtools' test.
