# External CLI playback consumer

`run.py` creates a fresh project outside this repository, uses absolute `path`
dependencies on public Engine and vector packages, bootstraps only that project's
resolution, then builds/runs dev and release with `--locked`. It checks actual
`resolution.json` and compilation commands for UI, spec, testing, site, host or SDK
dependencies. The source repository's lock must stay unchanged.

```sh
python3 tools/ci/hanim-play-consumer/run.py --mcpp /path/to/mcpp \
  --output /tmp/hanim-play-consumer.report.omui.json --keep-project
```

The retained project's executable accepts native typed command-line controls:

```sh
mcpp run --locked --profile release -- --time 0.5 --region 80,40,160,160 --out /tmp/frame.png
mcpp run --locked --profile release -- --time 0.5 --retarget --out /tmp/retarget.png
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

The gate checks original/midpoint/target pixels differ, output crop dimensions
and PNG round-trip pixels, real retarget continuity, and invalid-region rejection.
This is a fixed triangle/circle/diamond consumer fixture, not a general asset
tool, complete Engine validation, Agent provider, sanitizer or GPU-performance
claim. Source compilation may still prepare the optional GPU backend shipped by
the vector package; runtime selects CPU and creates no graphics device/window.
