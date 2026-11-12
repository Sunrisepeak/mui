// mlibs.names: the nearest name for a typo — a typo, a swap, none for a single letter.
import std;
import mlibs.names;

namespace {
int failures = 0;
void check(bool ok, std::string_view what) {
    if (!ok) {
        ++failures;
        std::println("  FAIL  {}", what);
    }
}
}  // namespace

int main() {
    namespace names = mlibs::names;
    constexpr std::array<std::string_view, 3> keys{"tempo", "sections", "key"};
    check(names::nearest("temop", keys) == "did you mean `tempo`?", "a swap of two neighbours");
    check(names::nearest("sectoins", keys) == "did you mean `sections`?", "a swap inside a longer name");
    check(names::nearest("tempa", keys) == "did you mean `tempo`?", "a letter changed");
    check(names::nearest("x", keys) == "one of `tempo`, `sections`, `key`", "never a whole one-letter name");
    check(names::nearest("harmony", keys) == "one of `tempo`, `sections`, `key`", "nothing near: the options");
    check(names::distance("ab", "ba") == 1 && names::distance("", "abc") == 3 && names::distance("kitten", "sitting") == 3, "edits: a swap is one");
    std::println("mlibs.names: {} failure(s)", failures);
    return failures ? 1 : 0;
}
