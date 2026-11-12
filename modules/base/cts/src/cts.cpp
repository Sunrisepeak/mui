module mlibs.cts;

import std;
import mlibs.hash;
import mcpplibs.mtest;
import mcpplibs.mtest.cts;

namespace mlibs::cts {

namespace mtest = mcpplibs::mtest;

namespace {

std::string where(const std::source_location& at) {
    std::string_view file = at.file_name();
    if (auto slash = file.find_last_of("/\\"); slash != std::string_view::npos) file.remove_prefix(slash + 1);
    return std::format("{}:{}", file, at.line());
}

// What `tally` counts into, and the subject of the suite being run there; none: the cases are mtest's.
struct Counting {
    Tally*         tally = nullptr;
    const Against* who = nullptr;
};
thread_local Counting counting;

bool lacks(const Against& who, std::string_view capability) { return who.has && !who.has(capability); }

}  // namespace

bool Check::that(bool ok, std::string_view why, std::source_location at) {
    if (!ok) failures_.push_back({where(at), std::string(why)});
    if (reported_) mtest::expect(ok, at) << why;
    return ok;
}

void Check::evidence(std::string_view kind, std::string_view value) {
    evidence_.emplace_back(std::string(kind), std::string(value));
    if (reported_) std::println("evidence {}: {}", kind, value);
}

void against(Against who, const std::function<void()>& cases, std::source_location at) {
    if (!counting.tally) {
        mtest::cts::against({.name = who.subject, .has = who.has, .required = who.required}, cases, at);
        return;
    }
    const Against* outer = std::exchange(counting.who, &who);
    // What this run requires and the subject lacks: a failure, stated once each.
    for (const std::string& capability : who.required)
        if (lacks(who, capability))
            counting.tally->cases.push_back({.subject = who.subject, .id = "requires " + capability, .status = Status::failed,
                                             .failures = {{"", std::format("{} lacks `{}`, which this run requires", who.subject, capability)}},
                                             .missing = {capability}});
    if (cases) cases();
    counting.who = outer;
}

void declare(Case c, std::source_location at) {
    if (!counting.tally) {
        mtest::test(c.id)[mtest::kind::cts][mtest::clause{c.contract}][mtest::tag{std::format("level:{}", name(c.level))}][mtest::cts::needs{c.needs}] =
            mtest::body(
                [body = std::move(c.body)] {
                    Check check = Check::reported();
                    body(check);
                },
                at);
        return;
    }
    Tally::Entry entry{.subject = counting.who ? counting.who->subject : std::string{}, .id = std::move(c.id), .contract = std::move(c.contract), .level = c.level};
    if (counting.who)
        for (const std::string& need : c.needs)
            if (lacks(*counting.who, need)) entry.missing.push_back(need);
    if (!entry.missing.empty()) {
        entry.status = Status::unsupported;
    } else {
        Check check;
        c.body(check);
        entry.status = check.failed() ? Status::failed : Status::passed;
        entry.failures = check.failures();
        entry.evidence = check.evidence();
    }
    counting.tally->cases.push_back(std::move(entry));
}

Tally tally(const std::function<void()>& suites) {
    Tally out;
    const Counting outer = std::exchange(counting, Counting{&out, nullptr});
    if (suites) suites();
    counting = outer;
    return out;
}

std::size_t Tally::count(Status s) const noexcept { return static_cast<std::size_t>(std::ranges::count(cases, s, &Entry::status)); }

std::vector<std::string> Tally::ids(Status s) const {
    std::vector<std::string> out;
    for (const Entry& entry : cases)
        if (entry.status == s) out.push_back(entry.id);
    return out;
}

const Tally::Entry* Tally::find(std::string_view id) const noexcept {
    const auto found = std::ranges::find(cases, id, &Entry::id);
    return found == cases.end() ? nullptr : &*found;
}

std::string Tally::text() const {
    std::string out;
    for (const Entry& entry : cases) {
        out += std::format("  {:<11} {} [{}] {}", name(entry.status), entry.id, name(entry.level), entry.contract);
        if (!entry.missing.empty()) {
            out += " — lacks";
            for (const std::string& missing : entry.missing) out += " " + missing;
        }
        out += "\n";
        for (const Failure& failure : entry.failures) out += std::format("      {}{}{}\n", failure.where, failure.where.empty() ? "" : ": ", failure.why);
    }
    out += std::format("{}: {} passed, {} failed, {} unsupported\n", cases.empty() || cases.front().subject.empty() ? "?" : cases.front().subject,
                       count(Status::passed), count(Status::failed), count(Status::unsupported));
    return out;
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
