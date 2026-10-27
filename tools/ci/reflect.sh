#!/usr/bin/env bash
# openmui.reflect (C++26 reflection, D-39), gated on a toolchain that can build
# it inside openmui's graph (V-08).
#
# libstdc++'s `std` module exports <meta> only when `std` is built with
# -freflection. GCC 16.1 then cannot merge the C library's unnamed-struct
# typedefs (__mbstate_t first) between that `std` and any unit that includes a
# C header in its global module fragment — libpng, FreeType and SDL's units,
# and every third-party module wrapper, are such units — and a unit built
# without -freflection cannot read that `std` at all. Neither keeping
# -freflection to openmui.reflect's own units nor wrapping <meta> in a module
# of its own avoids it (measured). Clang 23 has no -freflection.
#
# So a canary first: the smallest such graph, built in a temporary directory.
# While it fails with that conflict, the reflect suite cannot be built and
# this says so (a warning, the canary's error beside it); once a toolchain
# builds the canary, the suite runs and must pass. Any other canary failure
# fails the step.
#
#   tools/ci/reflect.sh <profile>
set -uo pipefail
cd "$(dirname "$0")/../.."
root=$(pwd)
profile=${1:-dev}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/src"
cat > "$work/mcpp.toml" <<'EOF'
[package]
name     = "reflect-canary"
version  = "0.1.0"
standard = "c++26"

[targets.reflect-canary]
kind = "bin"
main = "src/main.cpp"

[build]
sources  = ["src/*.cppm", "src/*.cpp"]
cxxflags = ["-freflection"]
EOF
cat > "$work/src/codec.cppm" <<'EOF'
export module canary;
import std;
export std::expected<std::string, std::string> encode(int n);
EOF
# A unit the way a native one is: a C header in the global module fragment.
cat > "$work/src/codec.cpp" <<'EOF'
module;
#include <stdio.h>
module canary;
import std;
std::expected<std::string, std::string> encode(int n) {
    if (n < 0) return std::unexpected(std::string("negative"));
    ::printf("%d\n", n);
    return std::string(static_cast<std::size_t>(n), 'x');
}
EOF
cat > "$work/src/main.cpp" <<'EOF'
import std;
import canary;
struct Settings { int volume; std::string name; };
int main() {
    std::string names;
    template for (constexpr auto m : std::define_static_array(
                      std::meta::nonstatic_data_members_of(^^Settings, std::meta::access_context::unchecked())))
        names += std::string(std::meta::identifier_of(m)) + " ";
    return names == "volume name " && encode(3) == "xxx" ? 0 : 1;
}
EOF

if (cd "$work" && mcpp run --profile "$profile") > "$root/reflect-canary.log" 2>&1; then
    echo "the toolchain builds reflection beside C headers: running openmui.reflect's suite"
    mcpp test -p openmui.reflect --profile "$profile" 2>&1 | tee reflect.log
    grep -q "test result ok" reflect.log || { echo "::error::openmui.reflect failed"; exit 1; }
    exit 0
fi
if grep -q "conflicting imported declaration" "$root/reflect-canary.log"; then
    grep -m1 "conflicting imported declaration" "$root/reflect-canary.log"
    echo "::warning::this GCC cannot build C++26 reflection in a graph with C headers (V-08: libstdc++'s std built with -freflection does not merge __mbstate_t); openmui.reflect's suite waits for a toolchain that can"
    exit 0
fi
tail -20 "$root/reflect-canary.log"
echo "::error::the reflection canary failed for another reason than V-08"
exit 1
