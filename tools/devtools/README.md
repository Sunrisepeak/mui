# omui-devtools

The contributors' tool (release design §6.6): the workspace's own checks that are more than a line of shell.

## ci

A CI run read row by row: each failing member, program and case with its first lines, set against the [red list](../../.agents/docs/STATUS.md#ci-red-list) (R12).

```sh
omui-devtools ci run 43685505783          # from GitHub (gh run view)
omui-devtools ci log run.log --jobs jobs.json   # a saved log (gh run view RUN --log-failed, or one job's from the API while the run goes on)
omui-devtools ci --json run 43685505783   # the same as JSON
```

Each failure lists the red rows that name it (its package, the package a `.cts` member checks, its suite's label, its case's words, its program); one no row names is marked `NEW`. A timeout shows the case that passed last before it; a member whose package did not build shows the build's errors. Exit 0 when every failure is in the red list, 1 when one is new, 2 when the run could not be read.

**Which lanes a run tests** (D-248, D-254). [`.github/ci.lanes.xdef.json`](../../.github/ci.lanes.xdef.json) lists CI's lanes — a row of the matrix, a job of `ci.yml`, or a workflow of its own — with what each checks and about how many runner minutes it takes, the groups (`all`, `elsewhere`: what this machine cannot run, `desktop`), and what each event runs when given nothing. A selection is names separated by spaces or commas, read left to right: a lane or a group adds, `-name` takes away, `none` empties.

```sh
omui-devtools ci plan                         # what pushing HEAD runs: the lint alone, or what its CI: trailer names
omui-devtools ci plan --lanes "all -android"  # what a run given these lanes tests, and its minutes
omui-devtools ci request elsewhere pages      # request them on this branch: ci.yml's lanes as one run, each workflow its own
```

A pull request's push runs the lint alone. A commit that needs more says so in its message's last paragraph — `CI: macos windows`, `CI: all -android`, `CI: pages` — and the lint job requests those as a run of their own, on the path of a run asked by hand; a later push cancels neither that run nor the lint that requests it. `[skip ci]` skips the lint too. In `ci.yml` the plan is the lint job's first step (`ci plan --dispatch --github-output …`): the matrix and each job follow its outputs, whether or not the rules after it pass. A name the catalogue does not have fails the plan with the names there are.

## govern

A command held within a share of the machine (AGENTS.md §5): at most `--jobs` compilers, assemblers and linkers of all its descendants run at once, whatever the command plans (mcpp builds workspace groups and host tools side by side, each with its own job count); while available memory is under `--memory-floor` (10%) of the total, fewer. The newest beyond the limit are paused and continued as the oldest finish; test programs and the build tools are never paused, since a test may measure time. Interrupting or terminating it continues what is paused and passes the request to the whole command.

```sh
mcpp run omui-devtools -p tools/devtools -- govern -- mcpp test --workspace
```

Every local build and test runs through it. Without `--jobs` it takes the machine's share (90% of the cores, and of the memory at about 2.25 GB a compile; a lower `MCPP_JOBS` is kept), sets `MCPP_JOBS` to it for the command, and runs the command under `ionice -c 3` and `nice -n 10` where they exist (`--normal-priority`: not). What runs is read from the system's process table (`ps`) and paused and continued with `kill`, both run through mcpplibs.os, which does not yet list or pause programs it did not start; on Windows the command runs with its job count alone.

## incremental

The same build twice: the second, unchanged, must leave every compilation output its databases name untouched (a generated source rewritten with the same bytes would recompile every importer). Wall time is reported, never judged: it is the runner's. The report, both builds' events and logs land beside `--report` (`target/incremental.report.xdef.json`). CI's desktop rows run it after building every member.

```sh
omui-devtools incremental --workspace --profile ci --exclude omui.reflect
omui-devtools incremental -p mlibs.math
```

## atspi

Examples 03 (the tally) and 07 (the notes) read and operated through AT-SPI, as a screen reader does (D-21): the accessibility bus turned on as Orca turns it on, each example on SDL's offscreen driver found in the registry by its window's name, its tree walked (names, roles, states, values), the +1 button pressed through Action, the goal slider read and set through Value, the draft field's text read through Text and written through EditableText, its caret and a word's selection moved, and the note added. It speaks D-Bus through mpal's own client (no libdbus, no busctl); mpal is preview, so `atspi` is the one part of this tool that rests on a preview package. A JSON report of every check; exit 1 when one failed.

With `--screen MONITOR` (RM2) the same walk runs on this machine's desktop: its own accessibility bus and registry (every other application in it; ours found by its program's name), each example in a real window of the machine's X server, opened on that monitor. What AT-SPI says is held to the window: a node's place on the screen is the window's corner plus its place in the window, the window's own pixels there are drawn, a press through Action is drawn in the window. The desktop's accessibility switch is its person's and is left alone: the examples find the switch on on a session bus of the check's (an AT-SPI bus launcher there, its settings in memory) and are told the desktop's accessibility bus (`AT_SPI_BUS_ADDRESS`). With `--ime` too (RM3), a fresh notes window composes in its field through the desktop's input method over XIM: `n` and `i` typed (XTEST, only while the window has the keyboard), the composition drawn in the window, the candidates opened at the field — read as what is drawn over the window on the screen inside its rectangle against the window's own pixels (where the window's own were the same before and after, two readings agreeing), nothing outside it — and the first candidate taken by the space (read back through Text: the field holds the composition and no space, so nothing was committed before it; AT-SPI's Text gives a field with its composition in it). Where the engine begins a window's input in direct mode (a first letter committed at once), the engine's own switch (Shift) turns composition on for the case and off after. The window's picture while composing goes to `target/atspi/composing.png`.

```sh
dbus-run-session -- omui-devtools atspi <tally> <notes-agent>    # the client alone
omui-devtools check atspi                                         # CI's Linux x64 rows: both built, a private bus
omui-devtools check atspi --screen HDMI-0                         # the desktop's buses, real windows on that monitor
omui-devtools check atspi --screen HDMI-0 --ime                   # and a composition through its input method
```

## api

A package's exported surface as text: the declarations its module interface units export, a class's public members, re-exported modules; bodies, initialisers and raw strings left out, one declaration a line. Committed beside a stable package as `api.omui.txt` (release plan §2.1: its API is v1), a change to the surface is a change to that file in the same commit; the tool's test holds every committed file to its package. It reads text, not the compiler's model. mrender's 2D packages (raster, vector, text, graph, frame, effect) hold theirs.

```sh
omui-devtools api -p modules/projects/mrender/vector            # print
omui-devtools api -p modules/projects/mrender/vector --write    # after a change meant to be one
```

## split

A project as a repository of its own (RC3.13, D-55): its members and every member they reach by `path` (their tests' too), with the repository's root files, copied to a tree whose root manifest lists only them. Building and testing that tree is the drill: whatever the project needs and does not declare is missing there. A tier (D-231) is split the same way: `foundation` (mlibs' groups, the foundation's projects, these tools), `framework` (and omui), `engine` (and the engine's projects); a tier whose members reach one of a tier above is refused, naming the member. CI's `split` rows do the three tiers.

```sh
omui-devtools split foundation --list                       # the members and how many files
omui-devtools split framework --out /tmp/framework          # the tree; then mcpp test --workspace there
omui-devtools split modules/projects/omui --list            # one project and what it reaches
```

## release

The release channel (RC3.1): the packages a new project needs published — the templates' dependencies and every member they reach by `path` through any table but the development ones, on every platform and feature — with the external versions missing from mcpp-index; then `mcpp publish --dry-run` over the closure (mcpp 2026.11.17.2: each member's archive by its full identity, its siblings at their own versions, SHA256SUMS; mcpp#947) and its descriptors laid out as the index is. Nothing is published; the descriptors name the release's URLs only (a CN mirror's table is the index maintainers', or mcpp's to emit). `check templates` uses the same table of members to point a template's published versions at the checkout.

```sh
omui-devtools release index --list                      # the closure, by project and maturity, and its blockers
omui-devtools release index                             # mcpp publish --dry-run: descriptors, SHA256SUMS and closure.json under target/release/index
omui-devtools release try [--template app-cpp]          # the release as a new user meets it (E1): clean home, new project, build, first window
omui-devtools release runtime [--version V] [--check [--omui FILE]]   # the shared runtime packed (E3b), its xlings descriptor; --check: two versions side by side
```

**`release runtime`** (E3b, D-250) packs the shared runtime as a release ships it: the player without the designer and the launcher (`omui-launch`, preview: `omui.entry.launcher` is the one part of this tool it rests on, for comparing versions as the launcher does), self-contained, with `runtime.json` (the player's `--runtime`: its version, the openmui specification it implements, its capabilities) as `omui-runtime-<version>-<platform>/` and its archive under `target/release/runtime`, and writes the xlings descriptor of every version there (`omui-runtime.lua`: installed, each program's loader and libraries are the package's own, through xlings' elfpatch). `--check`, in an xlings home of its own: every version installed side by side through the descriptor (each archive seeded where xlings downloads to: nothing is published); two applications from the app-ts template, `first` pinned to the oldest version and `second` taking the newest, each one's `omui-launch --which` and the player it becomes (`-- --runtime`: where it runs from); both running at the same time; each drawing its text; `first` refused once its version is removed, the command the refusal gives run as written, and started again; each application's package ≤ 100 KB. With `--omui FILE`, that `omui` also runs `omui run` in `second`, the newest version's launcher on PATH, and it must start on that version's runtime. Its report: `target/release/runtime/check.json`.

**`release try`** (E1) takes the release on this machine the way a new user would, before anything is published: `release index` first; then a clean mcpp home (`MCPP_HOME` under `target/release/try`) whose default index is this machine's copy of mcpp-index with the closure's descriptors added (a git repository), each archive seeded into the home's store where mcpp looks for that version (mcpp docs 11); `mcpp new hello --template omui.sdk:<template>` as `omui new` runs it — and a project from every other template beside it, each manifest read by a strict TOML reader (toml++, as the player reads its own; mcpp's parser takes more, mcpp#964); `mcpp build` — the toolchain, the system libraries and every outside package fetched into the clean home; and the first window drawn with SDL's offscreen driver and read back (not one colour). Each step's time and the home's and the project's bytes are in `target/release/try/try.json`.

The stand-in carries one workaround for what mcpp does with a published member, named where it is applied and removed when mcpp fixes it: the paths publish leaves in `[feature-deps.*]` and under a key with the package's own name pinned to the members' versions (mcpp#962). The descriptors are publish's own: since mcpp 2026.11.19.1 it writes the build field as the package's manifest (`mcpp = "*/mcpp.toml"`, mcpp#960), named and filed as mcpp-index files them (a package of the default namespace `mcpplibs` by its own name: `pkgs/m/mpal.lua`).

## lint

The repository's rules (AGENTS.md; D-206: the lint in C++, a subcommand here rather than a script), 45 of them: the sources' text (macros, headers, platform, naming, the capability owners…), the manifests (layering, project-dag, layout, licences, platforms, pins, closures, and `compose`: slots, providers and roots as the manifests declare them, D-238), what the sources import (imports, module-closure), and the data and documents (view documents against their schema through `mlibs.json`'s check, operations and skills, the agent surface, matter, STATUS, maturity, the platform table, the lead ledger, the user guides' code (`docs-code`: each block a region of a file a member holds, marked `<!-- code: file#region -->`, or `sh` commands; en and zh with the same headings), and declared data's container through mlibs.xdef.tool's checker, `data-format`: mlibs.xdef.tool is preview, so that rule rests on a preview package). Sources are read in parallel; the whole repository takes a few seconds. `--self-test` plants each rule's violation in a small workspace of its own and shows the rule catches it and nothing else complains. CI's lint row runs both; `devtools.lint` holds the repository to every rule in each test row.

```sh
omui-devtools lint                 # every rule
omui-devtools lint --rule layering # one
omui-devtools lint --list          # the rules and what each checks
omui-devtools lint --self-test
```

## docs

The user documents' generated pages (plan §6.3, RC3.5): `docs/{en,zh}/maturity.md` from every package's label (`[package.metadata.openmui] maturity`) with what each label promises, by project; `docs/{en,zh}/experimental.md` from the experimental packages' README sections (target, now, gap, limits, graduation). `devtools.docs` holds the committed pages to them; `platforms` does the same for the platform table.

```sh
omui-devtools docs          # write the four pages
omui-devtools docs --check  # exit 1 when one is not current
```

## lead

The lead ledger (Part 2 plan §2, D-135): our side against the strongest competitor, measured on one machine in the same job, judged by ratio.

- `sample` runs a benchmark and tags each JSON line it prints with the series (or the line's `--series-field`), the round and the key; a vector comparison's line also gives an error sample for each renderer against the reference.
- `verdict` pairs our series with each competitor's round by round, takes the ratio a round (theirs ÷ ours where lower is better), its median and a seeded 95 % bootstrap interval, takes the strongest competitor, and judges from the interval's lower end (behind < 0.95×, par, ahead ≥ 1.2×, far ahead ≥ 10×; an interval across a threshold takes the worse tier).
- `capability` records a capability against competitors (none, partial, app-defined, full).
- `docs` writes `docs/{en,zh}/lead.md` and the evaluation summary, each dated by the ledger's newest record, so the same ledger gives the same text.
- `check` is lint `lead`: the signature features against the ledger, and the pages current.

`bench competitors` drives the comparisons (tests/bench/competitors): our side built natively and for the Web, the competitors' own runtimes installed at their pinned versions (node's packages; ThorVG and rlottie through their Python bindings, the allowlist's second kind), every comparison run; with `--ledger`, `--rounds` rounds alternating, sampled, judged, and the ledger and its pages written (`--dry-run`: the verdicts only).

`bench apps` measures whole applications from outside their processes (tests/bench/competitors/apps): on an X server of its own (Xvfb), or with `--screen MONITOR` on this machine's display (RM1) — its window manager, compositor and GPU, every window opened on that monitor and only each program's own window watched (its damage, its pixels: GetImage on the window) and pressed (input only while it is under the pointer, the window kept above the person's); every side drawn at scale 1 whatever the desktop's `Xft.dpi`; omui's programs given the machine's NVIDIA driver as `gpu --real` gives it (the toolchain's C library does not search the system's); each ledger record says the display, what each side drew with and the machine's power setting and load (`display`, `conditions`).

```text
omui-devtools bench apps --screen HDMI-0 --no-build --rounds 10
omui-devtools bench competitors --ledger --rounds 15
omui-devtools lead capability --project omui --feature C --about "…" --test modules/projects/omui/tools/mcp/tests/mcp_test.cpp --competitor webmcp=app-defined
omui-devtools lead docs
```

## gpu

A GPU for the device tests, with `OMUI_REQUIRE_GPU=1` so that a missing device fails rather than being skipped. Without `--real` it is CI's software device: lavapipe (Mesa's Vulkan on the CPU) with the Vulkan loader on Linux and Windows, both from xlings (on Windows the driver manifest is registered under `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`, since an elevated runner's loader ignores the variables), and the machine's Metal device through SDL's cocoa video driver on macOS. With `--real` it is this machine's NVIDIA GPU through xlings's `nvidia-gl-host-link`; performance numbers come only from that (AGENTS §2.10). Without a command, the variables are written to `$GITHUB_ENV` (CI's GPU rows) and printed; with one, it runs with them (and SDL's offscreen video driver).

```text
omui-devtools gpu                                          # CI's rows: lavapipe or Metal, into $GITHUB_ENV
omui-devtools gpu -- mcpp test -p mrender.rhi.sdl          # CI's device on this machine
omui-devtools gpu --real -- mcpp test -p mrender.rhi.sdl   # this machine's GPU
```

## site

The website's page tools (moved from `tools/ci/site-split.sh`, `site-profile.mjs` and `drive-page.mjs`, D-206). The site is one page (`site/`, G5): `omui build --web` in `site/` packs it.

```sh
omui-devtools site split [DIR] [--routes R,…] [--names MAP]   # the program cut in two by what its first screen runs (wasm-split; a profile at each route, default ""); --names: the first half by owner
omui-devtools site profile DIR OUT [--route R] [--idle MS]    # an instrumented page's first screen used; the functions that ran written
omui-devtools site drive URL OUT.png [SECONDS] [WIDTH] [HEIGHT] [SCRIPT] [--flag SWITCH]… [--click X,Y]
```

`split` gives up with a notice where there is no wasm-split or headless Chromium, and fails instead under `OMUI_REQUIRE_BROWSER=1` (CI).

## check site, dist-toy-loading

The site's checks in a real browser (moved from `tools/ci/site-browser.sh`, the page driver and `dist-toy-loading.mjs`). Chromium is headless and driven over the DevTools protocol by `omui.devtools.cdp`: the built site served on 127.0.0.1 as Pages serves it (gzip for the kinds a host compresses — `mlibs.delivery`'s, the same a site's delivery is measured by — made before the first request), a WebSocket client (masked frames, fragments, ping and pong, payloads of several MB), a deadline on every call (`OMUI_CDP_TIMEOUT_S`, default 120 s; after one stall later calls fail at once), events delivered in order on the caller's thread. The JavaScript a check evaluates in the page stays JavaScript (the allowlist's browser host JS); gzip is `mlibs.delivery`'s (preview: the site's server and its measures are the parts of this tool that rest on it).

```sh
omui-devtools check site [delivery] [path] [--page DIR] [--omui PATH]   # the site (G5): site/'s omui build --web unless given
omui-devtools check dist-toy-loading [--fixture FILE]                  # omui.gen's dist_toy tests first unless a fixture is given
```

`site` holds the site to `site/delivery.budget.xdef.json` (a cold visit, frames timed while its design plays; `target/site-perf.json`), then walks the visitor's path with real clicks (`target/site.json`, pictures in `target/site-pictures/`): the Live Design key, Copy with a pairing code on the clipboard and the circle breathing, the command tab, `omui connect <code>` pairing, the page connected with the agent's name, omui and platform, `omui page describe` and `snapshot`, the bridge stopped, reduced motion. `dist-toy-loading` runs the generated loader in a frame of its own in the page (node's vm context before), against the browser's own fetch, streams and WebAssembly.

`omui-devtools bench material3 OUT` captures Material 3's controls in four states from their page (`tests/bench/competitors/compare/material3`, once bundled), the competitor's side of the controls' review.

## check, browser, media, ci local

The CI steps that were shell scripts (D-206), each a subcommand whose verdict is what the script's was:

- `check features | size | atspi | templates | reflect | sweep | toy | tools-e2e | agent-e2e | android | web | browser`: a feature chosen alone builds; a page's WebAssembly within its budget; examples read and operated as a screen reader does; the templates built from the release; the reflection canary; a sweep's agents; Toy's package and its doctor; the tools and the agent end to end; the Android row; the Web row's members; every wasm test program in Chromium.
- `browser PROGRAM.js [--preload DIR:AT]…`: a wasm test program in a real page of headless Chromium (its server on the loopback with mlibs.http's framing, the program's lines relayed, its summary line judged); `--headed` under `gpu --real` for the full browser on this machine's GPU.
- `media [--check] [--showcases] [SHOW…]`: the showcases' and the site's README pictures, made by their tests (scratch `readme-<name>`), copied in or held to them.
- `ci local [MEMBER…]`: CI's matrix on this one Linux machine, for when hosted runners are unavailable.

## consumer

The out-of-tree consumer probes (`hanim-core`, `hanim-api`, `hanim-play`, `omui-hanim [--web]`, `hanim-features`): a fresh project outside the repository made from its fixture under `tools/ci/`, built and run through mcpp as a user's project is (its own lock, then `--locked`), and what mcpp resolved and compiled audited — the closure a product brings, the sources it compiles, nothing of the UI or the test harness where it must not be. The repository's own lock must not change. A report and the commands' logs stay under `target/probe/<name>/`.

```sh
omui-devtools consumer hanim-api
omui-devtools consumer omui-hanim --web
```

## eval

The evaluations, each using what it judges: a real agent (a fresh `claude -p` session, or codex) in an empty directory of its own, the outcome checked on the subject's own state or numbers ([tools/eval](../eval/README.md)): `compare` (side-by-side, blinded, D-135), `tasks` (goals in words through `omui mcp --way`), `animate` (briefs graded by hanim's samples), `duel` (two agents play the fighting game), `materials controls` (the comparison's pictures), `hot-reload` (save to screen).
