// openmui.video — frames and a sound track encoded on exact frame and sample
// counts (D-86): the frame pipeline with "present" swapped for "encode".
//
// A video is frames from a source (`FrameSource`: the sync card, PNG files,
// or a host's — the omui command line renders an openmui view headless) and
// a sound track (openmui.audio's offline render), handed to encoders frame by
// frame: frame i with exactly the samples that play under it. Encoders are
// plugins (`Encoder`); built in, with no third-party code but openmui.image's
// PNG: a PNG sequence, Y4M (raw YUV 4:2:0, BT.709, integer conversion) and
// WAV. A compressed format is a plugin package (openmui.video.webm).
//
//   :timing       frame rates as fractions, a frame's first sample, exact
//   :picture      RGB ↔ Y'CbCr 4:2:0 (BT.709, limited range) in integers;
//                 Y4M read back; PSNR
//   :encode       sources, outputs, encoders, the built-ins and the driver
//   :card         the A/V sync and colour card: bars, a ramp, a sweep, the
//                 frame number and timecode, a flash and a beep each second
//   :description  `*.video.omui.toml`: a video as data, read with every
//                 problem at its line and column, written back as itself;
//                 its sound track rendered
export module openmui.video;

export import :timing;
export import :picture;
export import :encode;
export import :card;
export import :description;
