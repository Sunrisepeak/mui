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

Tested: `tests/bridge_test.cpp` — what AccessKit would be told of a tree, on
the platforms it has a library for.
