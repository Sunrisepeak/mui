// openmui.bench:audio — what sound costs (D-86): a block of the mix with
// many voices sounding — punches and bells from a sound file, through an
// echo on the effects' bus and the master's limiter — as an audio thread
// renders it, nothing allocated.
export module openmui.bench:audio;

import std;

export namespace omui::bench {

// Registers the audio benchmarks with the harness (add_framework_benchmarks calls it).
void add_audio_benchmarks();

}  // namespace omui::bench
