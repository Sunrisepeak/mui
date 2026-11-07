# omui.bench.web

motion/drive-1000's work (tests/bench) as a program of its own, so the same loop runs natively and as WebAssembly beside the competitors' scripts in the same JavaScript engine ([competitors](../competitors/README.md)).

A package apart from `omui.bench` for the platform alone: `omui.bench`'s dependencies (the SDK, text, audio) are not all built for the Web; this one needs only hanim.
