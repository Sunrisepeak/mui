# openmui.image

Images in memory (8-bit RGBA, straight or premultiplied) and PNG encoding and decoding through libpng's simplified API (`compat.libpng`, `compat.zlib` from mcpp-index). Errors are returned as messages; nothing longjmps through C++ frames.

Animated PNG (`encode_apng`, `save_apng`): every frame after the first is stored as the rectangle that changed from the one before, and an unchanged frame lengthens the one before, so a loop over a still background costs what moves; a viewer without APNG shows the first frame. The headless host records into it (`Headless::record`), which is how the showcases' README loops are made.
