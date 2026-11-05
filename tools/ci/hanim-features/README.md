# Hanim feature boundaries

`run.py` creates seven external consumers of the same real `hanim` package:
core only, effect, object, library, program, default player, and vector rendering
without the player. Each imports the public `hanim` umbrella, executes the chosen
API, repeats with `--locked`, then inspects actual resolution, compile commands
and generated exports. Disabled product sources must be absent, default/effect
users must not acquire renderer/RHI, and core-only keeps exactly std plus math.
It also packs the default product, imports its seven installed interfaces and
links the actual archive, then runs all 104 public authoring checks. The archive
must contain no implementation `.cpp`, and the consumer must not compile the
original product sources. Existing path-dependency metadata limitations require
explicit foundation dependencies; selected binary features are frozen at pack
time, not reselected after installation.

```sh
python3 tools/ci/hanim-features/run.py --output target/hanim-features/report.omui.json
```

This is a native release dependency/selection gate, not a GPU performance or
visual-quality test. The API probe, Hanim unit tests and external playback pixel
consumer cover richer execution and whole-process visuals. No fixture owns a
new library manifest inside this repository; manifests are generated only for
the actual independent-consumer builds.
