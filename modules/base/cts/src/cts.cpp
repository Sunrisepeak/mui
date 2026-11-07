module mlibs.cts;

import std;
import nlohmann.json;
import mlibs.hash;

namespace mlibs::cts {

namespace {

std::string where(const std::source_location& at) {
    std::string_view file = at.file_name();
    if (auto slash = file.find_last_of("/\\"); slash != std::string_view::npos) file.remove_prefix(slash + 1);
    return std::format("{}:{}", file, at.line());
}

}  // namespace

bool Check::that(bool ok, std::string_view why, std::source_location at) {
    if (!ok) failures_.push_back({where(at), std::string(why)});
    return ok;
}

void Check::evidence(std::string_view kind, std::string_view value) { evidence_.emplace_back(std::string(kind), std::string(value)); }

std::size_t Report::count(Status s) const noexcept {
    return static_cast<std::size_t>(std::ranges::count(outcomes, s, &Outcome::status));
}

std::string Report::text() const {
    std::string out;
    for (const auto& o : outcomes) {
        out += std::format("  {:<11} {} [{}] {}", name(o.status), o.id, name(o.level), o.contract);
        if (!o.missing.empty()) {
            out += " — lacks";
            for (const auto& m : o.missing) out += " " + m;
        }
        out += "\n";
        for (const auto& f : o.failures) out += std::format("      {}{}{}\n", f.where, f.where.empty() ? "" : ": ", f.why);
    }
    out += std::format("{} {} on {}: {} passed, {} failed, {} unsupported\n", suite, version, environment.subject.empty() ? "?" : environment.subject,
                       count(Status::passed), count(Status::failed), count(Status::unsupported));
    return out;
}

std::string Report::json() const {
    nlohmann::json j;
    j["format"] = "mlibs.cts@0.1";
    j["suite"] = suite;
    j["version"] = version;
    j["environment"] = {{"subject", environment.subject}, {"platform", environment.platform}, {"toolchain", environment.toolchain}};
    j["totals"] = {{"passed", count(Status::passed)}, {"failed", count(Status::failed)}, {"unsupported", count(Status::unsupported)}};
    j["cases"] = nlohmann::json::array();
    for (const auto& o : outcomes) {
        nlohmann::json c{{"id", o.id}, {"contract", o.contract}, {"level", name(o.level)}, {"status", name(o.status)}, {"seconds", o.seconds}};
        if (!o.missing.empty()) c["missing"] = o.missing;
        if (!o.failures.empty()) {
            c["failures"] = nlohmann::json::array();
            for (const auto& f : o.failures) c["failures"].push_back({{"where", f.where}, {"why", f.why}});
        }
        if (!o.evidence.empty()) {
            c["evidence"] = nlohmann::json::array();
            for (const auto& [kind, value] : o.evidence) c["evidence"].push_back({{"kind", kind}, {"value", value}});
        }
        j["cases"].push_back(std::move(c));
    }
    return j.dump(2);
}

// ── comparisons ──

std::string Difference::text() const {
    if (same) return "the same";
    return std::format("differ from byte {} (largest difference {}, {} beyond the tolerance)", first, largest, beyond);
}

Difference exact(std::span<const std::byte> got, std::span<const std::byte> want) noexcept { return within(got, want, 0, 0); }

Difference within(std::span<const std::byte> got, std::span<const std::byte> want, int tolerance, std::size_t budget) noexcept {
    Difference d;
    if (got.size() != want.size()) {
        d.same = false;
        d.first = std::min(got.size(), want.size());
        d.beyond = std::max(got.size(), want.size()) - d.first;
        return d;
    }
    bool seen = false;
    for (std::size_t i = 0; i < got.size(); ++i) {
        const int delta = std::abs(static_cast<int>(got[i]) - static_cast<int>(want[i]));
        d.largest = std::max(d.largest, delta);
        if (delta > tolerance) {
            if (!seen) d.first = i, seen = true;
            ++d.beyond;
        }
    }
    d.same = d.beyond <= budget;
    return d;
}

std::string PictureDifference::text() const {
    if (same) return "the same picture";
    if (x < 0) return "pictures of different sizes";
    return std::format("{} pixels beyond the tolerance, the first at ({}, {}); largest difference {}", beyond, x, y, largest);
}

PictureDifference image(const Picture& got, const Picture& want, int tolerance, std::size_t budget) {
    PictureDifference d;
    if (got.width != want.width || got.height != want.height) {
        d.same = false;
        return d;
    }
    for (int y = 0; y < got.height; ++y) {
        const auto* a = got.bytes.data() + static_cast<std::size_t>(y) * got.stride;
        const auto* b = want.bytes.data() + static_cast<std::size_t>(y) * want.stride;
        for (int x = 0; x < got.width; ++x) {
            bool over = false;
            for (int c = 0; c < 4; ++c) {
                const int delta = std::abs(static_cast<int>(a[x * 4 + c]) - static_cast<int>(b[x * 4 + c]));
                d.largest = std::max(d.largest, delta);
                over = over || delta > tolerance;
            }
            if (over) {
                if (d.x < 0) d.x = x, d.y = y;
                ++d.beyond;
            }
        }
    }
    d.same = d.beyond <= budget;
    return d;
}

std::string hash(std::span<const std::byte> bytes) { return mlibs::hash::Fnv1a().bytes(bytes).hex(); }

bool pinned(Check& c, std::span<const std::byte> bytes, std::string_view hex, std::string_view what, std::source_location at) {
    const std::string got = hash(bytes);
    c.evidence(std::string(what), got);
    return c.that(got == hex, std::format("{}: pinned {}, got {}", what, hex, got), at);
}

bool sampled(Check& c, std::span<const double> times, const std::function<std::vector<std::byte>(double)>& frame,
             std::span<const std::string_view> pins, std::string_view what, std::source_location at) {
    if (!c.that(times.size() == pins.size(), std::format("{}: {} times, {} pins", what, times.size(), pins.size()), at)) return false;
    bool ok = true;
    for (std::size_t i = 0; i < times.size(); ++i) {
        const std::string got = hash(frame(times[i]));
        c.evidence(std::format("{} at {} s", what, times[i]), got);
        ok = c.that(got == pins[i], std::format("{} at {} s: pinned {}, got {}", what, times[i], pins[i], got), at) && ok;
    }
    return ok;
}

}  // namespace mlibs::cts
