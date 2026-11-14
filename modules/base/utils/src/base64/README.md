# mlibs.base64

RFC 4648 base64 (the standard alphabet, padded), once for the whole repository: pictures carried in JSON (`ui.capture`), images inside SVG (`xrender.vector.svg`), and what the `omui` command reads back. An independent component (D-55): the standard library only.

```cpp
import mlibs.base64;

const std::string text = mlibs::base64::encode(png);
const auto bytes = mlibs::base64::decode(text);  // std::expected: an error names the offset
```

Decoding skips whitespace (wrapped text) and takes the padding as optional; anything else outside the alphabet is an error. RFC 4648's own test vectors are checked in `tests/base64/base64_test.cpp`.
