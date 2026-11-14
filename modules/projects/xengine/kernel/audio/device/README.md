# xengine.audio.device

A [Mixer](..) heard (D-86). It opens the system's default output at the mixer's rate (32-bit float stereo) and pulls blocks from the mixer on miniaudio's audio thread. Meanwhile a program plays into the mixer from any one thread.

```cpp
import xengine.audio;
import xengine.audio.device;

xengine::audio::Mixer mixer;
auto device = xengine::audio::Device::open(mixer);              // or {.null = true}: the clock runs, nothing sounds
mixer.play(mixer.find("punch"), mixer.clock() + 512);        // a little ahead of the device
```

**A feed** is for sources only their owner may touch while it renders — a place's sounds (`xengine.audio.space`), moved by a game's step on the game's thread. The owner renders its mixer on its own thread and keeps a `Feed` (a single-producer, single-consumer ring of stereo frames) a little ahead; the device drains it. Nothing is locked or allocated after the feed is made. The device plays exactly the samples an offline render makes. Whatever the device asks for that the feed does not have yet plays as silence and is counted (`starved`).

```cpp
xengine::audio::Feed feed(mixer.rate());
feed.fill(mixer, 2048);                                       // ahead before the device starts
auto device = xengine::audio::Device::open(feed);
// each frame, after the game's steps (space->emit / move / listen):
feed.fill(mixer, 2048);
```

- **Backends:** miniaudio's own order on each system (PulseAudio or ALSA, WASAPI, Core Audio). The null backend is for headless programs and tests: its clock runs at real time.
- **A plugin:** the mixer does not know of it. miniaudio comes through `mcpplibs.mwrap.miniaudio`, in this package's native unit only (D-83).
- **The Web:** output there (Web Audio, unlocked by the first click) comes with the Web host.

`tests/device_test.cpp`: on the null backend, the device pulls 0.1 s and more while the test plays into the mixer, and every play begins. A feed: drained in odd pieces across its ring's end, it gives the offline render's samples bit for bit; an empty one gives silence and counts it. The null device drains a feed kept ahead, none of it short.
