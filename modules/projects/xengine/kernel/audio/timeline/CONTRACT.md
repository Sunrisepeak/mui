# Timeline audio control contract (0.1)

- Input: an owned prepared snapshot of one named `hanim::Track<double>` plus clip placement, level and fades; exact sample rate, explicit outside gain.
- Sampling: pure, total at all signed sample indices, fill the entire caller span; use the declared exact sample clock. No transport, event, GUI or device side effects; no allocation, locking or I/O during sample.
- Ownership: prepare owns all required curve state. No borrowed clip/property/string survives preparation. Multiple observers can sample one immutable owner concurrently with their own buffers.
- Failures: typed preparation errors; unsupported sample rates fail before publication. Invalid mixer bus/rate bindings retain the old owner. Out-of-domain absolute sample indices yield outside gain.
- Values: clip-local keys × level × fades, bounded to linear gain [0,8]. Before begin and at/after end use outside. Declared holds/cuts and easing overshoot are intentional, not hidden smoothing.
- Installation: `Mixer::automate_gain` requires a quiescent owner; its shared pointer retains the plan. Cross-thread live replacement, transport events and stateful DSP seek are separate capabilities, not implicit promises.
- Optionality: adapter → timeline + audio + hanim.core; audio itself consumes only its block control contract and shared math. No UI/renderer dependency in the adapter.
- Observability and CTS: package README names trace events, tests, benchmark and headless SOP. The ordinary CLI's real WAV/images are separate from the pure library dependency closure.
