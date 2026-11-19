# omui.accessibility.bridge

The platform's assistive-technology service for
[`omui.accessibility`](..), through AccessKit: UI Automation on Windows
x86_64, NSAccessibility on macOS arm64, AT-SPI over D-Bus on Linux x86_64.
Elsewhere (Linux arm64, x86_64 macOS, the Web, Android) AccessKit has no
library and `install` installs nothing.

```cpp
import omui.accessibility.bridge;
omui::accessibility::bridge::install();   // once, before a host attaches
```

A package of its own (B6): `omui.accessibility` holds the mapping and the
seam (`Service`), and only a program's root names this package. `omui.sdk`
installs it by default; an application that depends on the SDK with
`default-features = false, features = ["no-accessibility-bridge"]` does not
carry AccessKit, and accessibility is then not served, as on a platform
without a service.

AccessKit's C bindings (`compat.accesskit`, module
`mcpplibs.mwrap.accesskit`) appear only in `src/native/accesskit/`.

Nothing is read while no assistive technology listens; its activation reads
the mirror and wakes the runtime. On Linux an update is built on the
runtime's thread and AccessKit's work for it (its tree's difference, the
events it raises over D-Bus) runs on the bridge's own thread, in order, with
focus and bounds: a frame does not wait for AccessKit's event thread (a 10k
list's scroll waited 15.7 ms at p95 for it, 2.2 ms now; C1). At most four steps wait;
one more waits for room.

Tested: `tests/bridge_test.cpp` — what AccessKit would be told of a tree, on
the platforms it has a library for.
