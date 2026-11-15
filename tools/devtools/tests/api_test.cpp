// omui-devtools api: a unit's exported surface read from its text — what is exported and what is not, a class's
// public members and not its private ones, bodies and initialisers left out, re-exports kept — and every
// `api.omui.txt` committed in the workspace the surface its package has now (release plan §2.1: a stable
// package's API is v1; a change to it is a change to that file in the same commit).
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import omui.devtools.api;
import omui.devtools.members;

using namespace mcpplibs::mtest;
namespace api = omui::devtools::api;

namespace {

constexpr std::string_view unit = R"cpp(// a unit
module;
#include <cstdio>
export module demo.shapes;

import std;
export import demo.base;  // re-exported: part of the surface
import demo.hidden;

namespace detail {
int helper() { return 1; }
}

export namespace demo {

// A shape.
struct Point {
    float x = 0, y = 0;
    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y); }
};

class Path {
public:
    Path() = default;
    void move_to(Point p);
    template <class F>
    void each(F&& f) const { for (auto& p : points_) f(p); }

private:
    std::vector<Point> points_;
    int hidden_ = 0;
};

enum class Fill : std::uint8_t { nonzero, even_odd };

inline constexpr std::array<int, 3> sizes{{1, 2, 3}};
[[nodiscard]] std::expected<Path, std::string> parse(std::string_view text, std::function<void(int)> each = [](int) { /* {} */ });
template <class T>
concept Drawable = requires(T t) { t.draw(); };

}  // namespace demo

export [[nodiscard]] int version() noexcept { return 3; }
)cpp";

}  // namespace

UTest cases{"devtools.api", [] {
    "what a unit exports, a class's public members, bodies and initialisers out, re-exports kept"_test = [] {
        const auto lines = api::exported(unit);
        std::string text;
        for (const auto& line : lines) text += line + "\n";
        std::println("{}", text);
        const std::vector<std::string> want{
            "export import demo.base;",
            "namespace demo {",
            "  struct Point {",
            "    float x = 0, y = 0;",
            "    [[nodiscard]] float length() const noexcept;",
            "  };",
            "  class Path {",
            "    Path() = default;",
            "    void move_to(Point p);",
            "    template <class F> void each(F&& f) const;",
            "  };",
            "  enum class Fill : std::uint8_t { nonzero, even_odd };",
            "  inline constexpr std::array<int, 3> sizes{…};",
            "  [[nodiscard]] std::expected<Path, std::string> parse(std::string_view text, std::function<void(int)> each = [](int) {…});",
            "  template <class T> concept Drawable = requires(T t) {…};",
            "}",
            "[[nodiscard]] int version() noexcept;",
        };
        expect(lines == want) << text;
        expect(!text.contains("hidden") && !text.contains("helper") && !text.contains("points_")) << "private and unexported left out";
    };

    "a manifest's interface globs"_test = [] {
        const auto globs = api::interface_globs(R"([build]
sources = ["src/*.cppm", "src/*.cpp", "src/kit/*.cppm"]

[target.'cfg(os = "emscripten")'.build]
sources = ["src/native/web.cppm", "src/native/web.cpp"]
)");
        expect(globs == std::vector<std::string>{"src/*.cppm", "src/kit/*.cppm", "src/native/web.cppm"});
    };

    "every committed api.omui.txt is its package's surface now"_test = [] {
        auto root = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path();
        std::ifstream manifest(root / "mcpp.toml", std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(manifest), {}};
        std::size_t held = 0;
        for (const auto& member : omui::devtools::members::listed(text)) {
            const auto directory = root / mcpplibs::os::fs::from_utf8(member);
            std::ifstream committed(directory / "api.omui.txt", std::ios::binary);
            if (!committed) continue;
            ++held;
            const std::string want{std::istreambuf_iterator<char>(committed), {}};
            std::ifstream own(directory / "mcpp.toml", std::ios::binary);
            const std::string member_manifest{std::istreambuf_iterator<char>(own), {}};
            const auto globs = api::interface_globs(member_manifest);
            expect(api::surface(directory, globs) == want) << member << ": its surface changed — `omui-devtools api -p " << member << " --write` if it is meant to";
        }
        std::println("    {} packages' surfaces held", held);
        expect(gt(held, std::size_t{0}));
    };
}};
