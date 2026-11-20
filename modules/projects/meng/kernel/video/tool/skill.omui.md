# video — skill

How an Agent (or a person at a terminal) makes and checks a film without a GUI. The operations are the catalog's: [`video.ops.xdef.ts`](video.ops.xdef.ts), listed in [`src/catalog/video.ops.json`](src/catalog/video.ops.json) and [`src/catalog/video.ops.md`](src/catalog/video.ops.md). Each is the `video` command (`video.render` is `video render FILE --out OUT`): records out, exit 0 done, 1 failed with why, 2 a wrong command. A film of an omui view goes through xtools (`xtools video render x.view.xdef.json …`), which adds omui's view source.

## Before

1. `video timing --fps R`: how frames and samples line up at the rate you want (29.97 is 30000/1001; five frames are exactly 8008 samples at 48 kHz).
2. Write the description (`<name>.video.xdef.ts`, `defineVideo`): size, rate, length, where the frames come from (the sync card, PNG files by pattern), the sound.

## Doing

1. `video check FILE`: every problem at its line, or what it will make (size, rate, frames, samples a frame).
2. `video still FILE --at SECONDS --out f.png` at the moments that matter, and look at them.
3. `video render FILE --out x.webm` (or y4m, wav, shots/): the answer names each file with its size and hash, and how much faster than real time it ran.

## Verifying

- `video probe` the result: the frame count and sample count are the description's exactly; a WebM says VP9, Opus and BT.709.
- With one encoder thread (the default) the same description gives the same video bytes everywhere: pin the hash a test depends on.
- Record the commands, the files and their hashes.
