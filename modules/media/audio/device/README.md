# openmui.audio.device

A [Mixer](..) heard (D-86). It opens the system's default output at the mixer's rate (32-bit float stereo) and pulls blocks from the mixer on miniaudio's audio thread. Meanwhile a program plays into the mixer from any one thread.

```cpp
import openmui.audio;
import openmui.audio.device;

omui::audio::Mixer mixer;
auto device = omui::audio::Device::open(mixer);              // or {.null = true}: the clock runs, nothing sounds
mixer.play(mixer.find("punch"), mixer.clock() + 512);        // a little ahead of the device
```

- **Backends:** miniaudio's own order on each system (PulseAudio or ALSA, WASAPI, Core Audio). The null backend is for headless programs and tests: its clock runs at real time.
- **A plugin:** the mixer does not know of it. miniaudio comes through `mcpplibs.mwrap.miniaudio`, in this package's native unit only (D-83).
- **The Web:** output there (Web Audio, unlocked by the first click) comes with the Web host.

`tests/device_test.cpp`: on the null backend, the device pulls 0.1 s and more while the test plays into the mixer, and every play begins.
