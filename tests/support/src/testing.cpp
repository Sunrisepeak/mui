module openmui.testing;

import std;

namespace omui::testing {

namespace {

struct Running {
    std::string_view suite;
    std::string_view name;
    int              failures = 0;
};

Running* running = nullptr;

}  // namespace

Suite::Suite(std::string name) : name_(std::move(name)) {}

Suite& Suite::test(std::string name, std::function<void()> body) {
    cases_.push_back({std::move(name), std::move(body)});
    return *this;
}

int Suite::run(std::string_view filter) {
    int passed = 0;
    int failed = 0;
    for (auto& c : cases_) {
        if (!filter.empty() && c.name.find(filter) == std::string::npos) continue;
        Running r{name_, c.name};
        running = &r;
        auto start = std::chrono::steady_clock::now();
        c.body();
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        running = nullptr;
        if (r.failures == 0) {
            ++passed;
            std::println("  ok    {}/{} ({:.1f} ms)", name_, c.name, ms);
        } else {
            ++failed;
            std::println("  FAIL  {}/{} ({} failed checks)", name_, c.name, r.failures);
        }
    }
    std::println("{}: {} passed, {} failed", name_, passed, failed);
    if (passed + failed == 0) {
        std::println("{}: no case matched '{}'", name_, filter);
        return 1;
    }
    return failed == 0 ? 0 : 1;
}

int Suite::run(int argc, char** argv) {
    return run(argc > 1 ? std::string_view(argv[1]) : std::string_view{});
}

void fail(std::string_view what, std::source_location where) {
    std::println(std::cerr, "    {}:{}: check failed{}{}", where.file_name(), where.line(),
                 what.empty() ? "" : ": ", what);
    if (running) ++running->failures;
    else std::exit(1);  // a check outside any case is a broken test program
}

void check(bool ok, std::string_view what, std::source_location where) {
    if (!ok) fail(what, where);
}

void check_bits(double actual, double expected, std::source_location where) {
    auto a = std::bit_cast<std::uint64_t>(actual);
    auto e = std::bit_cast<std::uint64_t>(expected);
    if (a != e)
        fail(std::format("expected {:.17g} ({:#018x}), got {:.17g} ({:#018x})", expected, e, actual, a),
             where);
}

void check_near(double actual, double expected, double tolerance, std::source_location where) {
    if (!(std::abs(actual - expected) <= tolerance))
        fail(std::format("expected {:.17g} ± {:g}, got {:.17g}", expected, tolerance, actual), where);
}

int failures() noexcept { return running ? running->failures : 0; }

void Hash::bytes(std::span<const std::byte> data) noexcept {
    for (auto b : data) {
        h_ ^= static_cast<std::uint64_t>(b);
        h_ *= 0x100000001b3ull;
    }
}

void Hash::u64(std::uint64_t v) noexcept {
    std::array<std::byte, 8> le{};
    for (int i = 0; i < 8; ++i) le[i] = static_cast<std::byte>((v >> (8 * i)) & 0xff);
    bytes(le);
}

void Hash::f64(double v) noexcept { u64(std::bit_cast<std::uint64_t>(v)); }
void Hash::f32(float v) noexcept { u64(std::bit_cast<std::uint32_t>(v)); }

void Hash::str(std::string_view s) noexcept {
    bytes(std::as_bytes(std::span(s.data(), s.size())));
    u64(s.size());
}

std::string Hash::hex() const { return std::format("{:016x}", h_); }

std::filesystem::path scratch_dir(std::string_view test_name) {
    auto dir = std::filesystem::temp_directory_path() / "openmui-tests" / std::string(test_name);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

}  // namespace omui::testing
