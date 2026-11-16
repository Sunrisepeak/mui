// What the acceptance steps share (omui.devtools.check's own partition): reading a file, saying a failure, mcpp
// with its narration captured, a line looked for, a scratch directory removed however a step ends, and the
// expectations an end-to-end step holds a command line to (its exit status, then patterns over its answer).
module omui.devtools.check:support;

import std;
import omui.devtools.run;

namespace omui::devtools::check::support {

namespace fs = std::filesystem;

inline int failed(std::string_view message) {
    std::cout << "::error::" << message << '\n';
    return 1;
}

// `mcpp ARGS…` in `cwd`, standard error with standard output: its status and what it said.
inline std::pair<int, std::string> mcpp(const fs::path& root, std::vector<std::string> args, const fs::path& cwd = {}) {
    auto done = run::program(root, "mcpp", std::move(args), {.cwd = cwd, .merged = true});
    return done ? std::pair{done->status, std::move(done->out)} : std::pair{127, done.error()};
}

inline bool has_line(std::string_view text, std::string_view wanted, bool prefix) {
    for (auto part : std::views::split(text, '\n')) {
        const std::string_view line(part.begin(), part.end());
        if (prefix ? line.starts_with(wanted) : line == wanted) return true;
    }
    return false;
}

inline std::string kib(std::uint64_t bytes) { return std::format("{:.0f} KiB", static_cast<double>(bytes) / 1024.0); }

// A scratch directory removed, however the step ends.
struct Removed {
    fs::path path;
    ~Removed() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

// A command line held to what it answers: each call runs one, checks its status, keeps its output for the
// patterns that follow; the first that fails ends the step (its output shown).
class Expect {
public:
    explicit Expect(fs::path root) : root_(std::move(root)) {}

    bool status(int wanted, std::string program, std::vector<std::string> args, const run::How& how = {}) {
        if (!ok_) return false;
        std::string said = program;
        for (const auto& arg : args) said += " " + arg;
        auto done = run::program(root_, std::move(program), std::move(args), how);
        last_ = done ? done->out : done.error();
        const int got = done ? done->status : 127;
        if (got != wanted) return fail(std::format("FAIL ({}, wanted {}): {}", got, wanted, said));
        std::cout << "ok   " << said << '\n' << std::flush;
        return true;
    }

    // An ECMAScript pattern found in the last command's answer.
    bool has(std::string_view pattern) {
        if (!ok_) return false;
        if (std::regex_search(last_, std::regex(std::string(pattern)))) return true;
        return fail(std::format("FAIL: the answer has no {}", pattern));
    }

    bool holds(bool fact, std::string_view what) {
        if (!ok_) return false;
        return fact || fail(std::format("FAIL: {}", what));
    }

    [[nodiscard]] const std::string& last() const { return last_; }
    [[nodiscard]] int result(std::string_view done) const {
        if (ok_) std::cout << done << '\n';
        return ok_ ? 0 : 1;
    }

private:
    bool fail(std::string message) {
        std::cout << message << '\n' << last_.substr(0, 2000) << '\n';
        ok_ = false;
        return false;
    }

    fs::path root_;
    std::string last_;
    bool ok_ = true;
};

}  // namespace omui::devtools::check::support
