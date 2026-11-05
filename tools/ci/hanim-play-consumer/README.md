# External CLI playback consumer

`run.py` creates a fresh project outside this repository, uses absolute `path`
dependencies on the public hanim product with `features = ["render-vector"]`, bootstraps only that project's
resolution, then builds/runs dev and release with `--locked`. It checks actual
`resolution.json` and compilation commands for UI, spec, testing, site, host or SDK
dependencies. The source repository's lock must stay unchanged.

The host explicitly selects `mlibs.control` for shared openas authorization;
hanim's default dependency graph does not import it. Both profiles also run
`--agent-review`: 61 real CPU frames each for linear and curved morph paths,
followed by exact reverse replay, with time/revision/pixel provenance. DISPLAY
and WAYLAND_DISPLAY are removed. Read-only policy, dry run, foreign sessions,
revoked worker continuations and a denied capture that publishes no files are
checked against the actual engine/renderer. These checks use the native public
policy/context library, not a new wire protocol or universal provider registry.

The source imports `hanim` and creates its pixel fixture through
`engine.play(anim::morph(source,target).with_effect(...))`. It also retains an
advanced snapshot Program, proving both authoring levels use the same player.
The audit checks the
umbrella's existing mcpp-normalized identity `mcpplibs.hanim`.

Before the fixed pixel fixture, each invocation directly compiles a nested
two-track/three-segment Program through the public API and exercises shared
pause/seek/reverse/advance/restore/release. The graph audit requires the actual
hanim product and its selected source modules. Program composite pixels are covered by the three integration tests under `tests/`, copied to a
separate test consumer to keep `mlibs.testing` out of production resolution; this CLI's exported PNG remains the fixture below.

```sh
python3 tools/ci/hanim-play-consumer/run.py --mcpp /path/to/mcpp \
  --output /tmp/hanim-play-consumer.report.omui.json --keep-project
```

The retained project's executable accepts native typed command-line controls:

```sh
mcpp run --locked --profile release -- --time 0.5 --region 80,40,160,160 --out /tmp/frame.png
mcpp run --locked --profile release -- --time 0.5 --retarget --out /tmp/retarget.png
mcpp run --locked --profile release -- --time 0.5 --bend 0.35 --out /tmp/curved.png
mcpp run --locked --profile release -- --agent-review --bend 0.35 --out /tmp/review.png
```

`--time` selects seconds in the current one-second segment; `--viewport W,H`
defaults to `320,240`, and `--region X,Y,W,H` crops actual viewport pixels.
`--retarget` first samples the triangle→circle segment at 0.7 seconds, retargets
that current pose to a diamond, rebinds its immutable renderer preparation, and
then selects `--time` in the new segment. It checks rendered C0 continuity with the existing vector consumer’s
18/255 coverage limit only at the one-pixel contour boundary: bare white coverage
is independently rendered, uniform interiors/exteriors stay exact, and blur/tone
output differences must remain on their actual image edge. The report records
measured maxima, off-boundary counts and total error; this is no whole-image
tolerance. Every invocation exercises pause, seek, reverse, resume and batch
advance through Engine. The CPU reference renderer is the existing vector
renderer; effects use the public blur/tone chain. It writes real PNG pixels,
stdout JSON and a matching `.png.provenance.omui.json` sidecar with object refs,
time, viewport, region, backend and pixel hash. No window or GUI startup occurs.

`--bend FRACTION` adds a public typed vertex-path effect before blur/tone.
The gate exports both ordinary and curved endpoints/middle/retarget pixels:
the endpoints must be identical, middle pixels must differ, and both retarget
cases satisfy the same existing boundary-only continuity tolerance. JSON records
the actual process stage, coordinate frame and bend separately from image effects.

The gate checks original/midpoint/target pixels differ, output crop dimensions
and PNG round-trip pixels, real retarget continuity, and invalid-region rejection.
This is a fixed triangle/circle/diamond consumer fixture, not a general asset
tool, complete Engine validation, generic remote provider, sanitizer or GPU-performance
claim. Source compilation may still prepare the optional GPU backend shipped by
the vector package; runtime selects CPU and creates no graphics device/window.

`--agent-review` is a validation host: it explicitly creates a trusted development
session even in its release binary. It does not accept session/grant claims from
JSON, automatically renew revoked sessions, or represent the production default
policy. It writes `.agent-000.png` through `.agent-060.png` beside the requested
output, then verifies replay and refusal. `--agent-deny-capture` restricts this
session to seeking, returns structured failure/exit 3, and writes no images.
The ordinary requested PNG after a successful review is a local host export;
all Agent review frames pass through the shared authorization gate. A successful
offline process review is not a real-time timing or human aesthetic acceptance.
