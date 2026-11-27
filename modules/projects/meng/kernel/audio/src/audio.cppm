// meng.audio — sound for openmui and its games (D-86): sounds as data,
// voices mixed into buses, each bus through its effects into the master,
// rendered a block at a time with nothing allocated, every sample a function
// of what was played and when — so an offline render is the same bits on
// every platform (a match's audio pinned like its pixels). Devices and
// codecs are plugins (`meng.audio.device`).
//
//   :mix    sounds (`*.sound.xdef.ts`), effects, the mixer and its buses,
//           offline rendering, levels
//   :music  instruments (patches), pieces (`*.music.xdef.ts`) and the
//           sequencer that plays them, adaptive by intensity
export module meng.audio;

export import :mix;
export import :music;
