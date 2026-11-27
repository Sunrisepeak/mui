// mlibs.version — versions compared (the one implementation): "0.1.10" after "0.1.9", a pre-release ("0.1.0-dev.3")
// before its release. What a launcher choosing the newest installed runtime and a release tool ordering its packs ask.
//
//     if (mlibs::version::compare(installed, wanted) >= 0) …
export module mlibs.version;

import std;

export namespace mlibs::version {

// Part by part ('.'), as numbers where both parts are (a missing part is 0), else as text; then the pre-release
// (after '-'): none is after any, two compared as the version is.
[[nodiscard]] std::strong_ordering compare(std::string_view left, std::string_view right);

}  // namespace mlibs::version
