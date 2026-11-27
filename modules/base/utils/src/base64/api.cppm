// mlibs.base64.api — RFC 4648 base64 (the standard alphabet, padded), once
// for the whole repository: pictures carried in JSON (ui.capture), images
// inside SVG, and what a command line reads back.
//
//     const std::string text = mlibs::base64::encode(png);
//     const auto bytes = mlibs::base64::decode(text);   // or an error naming the place
export module mlibs.base64.api;

import std;

export namespace mlibs::base64 {

[[nodiscard]] std::string encode(std::span<const std::uint8_t> bytes);

// Whitespace between groups is skipped (wrapped text); padding is optional at
// the end. Anything else outside the alphabet is an error naming its offset.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> decode(std::string_view text);

}  // namespace mlibs::base64
