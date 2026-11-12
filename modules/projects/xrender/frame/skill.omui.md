# frame — skill

How an Agent reviews the picture a program draws (`xrender.frame`), whoever made its content. The operations, their parameters and effects are in [frame.ops.omui.ts](frame.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `frame` says what it can do (observation, the source of what it puts out) and what it cannot (control, time: those are its producers' — `ui`, `world`).
2. `frame.layers` gives the size, the pixels' hash and the layers from the back. A layer that reports missing images or glyphs, or effects skipped, did not draw all it was asked to: say so before judging the picture.

## Doing

1. Compare by hash first: two frames with the same hash are the same picture; no pixels need be read.
2. Read the part in question: `frame.capture` with a `region`. The whole frame only when the whole is in question (at most 4 megapixels a request).
3. For "what is this?", `frame.at` a pixel: its colour and the chain back — the layer, and that layer's object. Take the object to its producer: an interface's object is a node (`ui.drew` at the same place says which, and where it was built), a world's an entity.
4. An empty chain means no object drew there (a background, or a layer that keeps no ids): `frame.layers` says which layers keep them.

## Verifying

- What an agent reviews is what a person sees: the capture is the target's own pixels, not a second rendering.
- After a change made through a producer's subject, the frame's hash changes only if the picture did.

## Not here

Changing what is drawn, and time, are the producers' subjects'. Vectors (a layer as SVG) are the display list's (`xrender.vector`'s SVG writer; omui's `ui.capture` with `format: "svg"`).
