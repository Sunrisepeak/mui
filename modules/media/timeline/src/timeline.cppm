// openmui.timeline — the timeline every editor shares (D-86): a video
// editor's, a sequencer's, an animation editor's, gongfu.studio's.
//
// Time is exact: flicks, 1/705,600,000 of a second, which every common frame
// rate (24, 25, 30, 48, 50, 60, 90, 100, 120 and the NTSC 1000/1001 ones)
// and sample rate (44.1, 48, 96 kHz) divides — a frame or a sample is a whole
// number of them, so an edit never drifts and two machines agree.
//
//   :model  time, tracks, clips, keyed properties, markers; what plays when
//   :edit   changes made of three steps, each change giving back its inverse
//   :file   *.timeline.omui.toml read (every problem at its line and column)
//           and written back as itself
export module openmui.timeline;

export import :model;
export import :edit;
export import :file;
