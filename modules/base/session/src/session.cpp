module openmui.session;

import std;
import nlohmann.json;
import mcpplibs.os;

namespace omui::session {

namespace {

// UTF-8, not the code page (std::filesystem::path::string() throws on
// Windows for a name the ANSI code page cannot spell).
std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace

namespace {

using json = nlohmann::ordered_json;

// Times are written with the shortest text that reads back to the same double.
std::string time_text(double t) {
    std::array<char, 32> buf{};
    auto r = std::to_chars(buf.data(), buf.data() + buf.size(), t);
    return std::string(buf.data(), r.ptr);
}

std::expected<json, std::string> parse_object(std::string_view text, std::string_view what) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return std::unexpected(std::format("{} is not valid JSON", what));
    if (!j.is_object()) return std::unexpected(std::format("{} is not a JSON object", what));
    return j;
}

}  // namespace

std::expected<void, std::string> Session::add(double time, std::string_view kind, std::string_view body_json) {
    if (!std::isfinite(time)) return std::unexpected("an entry's time is a finite number of seconds");
    if (kind.empty()) return std::unexpected("an entry has a kind");
    auto body = parse_object(body_json.empty() ? std::string_view("{}") : body_json, "the entry body");
    if (!body) return std::unexpected(body.error());
    Entry e{time, std::string(kind), body->dump()};
    auto at = std::upper_bound(entries_.begin(), entries_.end(), time, [](double t, const Entry& x) { return t < x.time; });
    entries_.insert(at, std::move(e));
    return {};
}

std::size_t Session::seek(double time) const noexcept {
    auto it = std::lower_bound(entries_.begin(), entries_.end(), time, [](const Entry& x, double t) { return x.time < t; });
    return static_cast<std::size_t>(it - entries_.begin());
}

std::span<const Entry> Session::between(double from, double to) const noexcept {
    const std::size_t a = seek(from), b = std::max(a, seek(to));
    return std::span<const Entry>(entries_).subspan(a, b - a);
}

std::vector<const Entry*> Session::of(std::string_view kind) const {
    std::vector<const Entry*> out;
    for (const Entry& e : entries_)
        if (e.kind == kind) out.push_back(&e);
    return out;
}

void Session::merge(const Session& other, double offset) {
    std::vector<Entry> shifted;
    shifted.reserve(other.entries_.size());
    for (const Entry& e : other.entries_) shifted.push_back({e.time + offset, e.kind, e.body});
    std::vector<Entry> out;
    out.reserve(entries_.size() + shifted.size());
    // Stable: at equal times this session's entries come first.
    std::ranges::merge(entries_, shifted, std::back_inserter(out), [](const Entry& a, const Entry& b) { return a.time < b.time; });
    entries_ = std::move(out);
}

Session Session::slice(double from, double to) const {
    Session s;
    s.header = header;
    for (const Entry& e : between(from, to)) s.entries_.push_back({e.time - from, e.kind, e.body});
    return s;
}

std::string Session::ndjson() const {
    json h = json::object();
    h["format"] = format_name;
    if (!header.app.empty()) h["app"] = header.app;
    if (!header.spec.empty()) h["spec"] = header.spec;
    h["size"] = json::array({header.width, header.height});
    h["scale"] = header.scale;
    h["clock"] = header.clock;
    if (header.fps > 0) h["fps"] = header.fps;
    if (header.seed) h["seed"] = header.seed;
    if (!header.meta.empty()) {
        json m = json::object();
        for (const auto& [k, v] : header.meta) m[k] = v;
        h["meta"] = std::move(m);
    }
    std::string out = h.dump();
    out += '\n';
    for (const Entry& e : entries_) {
        out += std::format("{{\"t\":{},\"kind\":{},\"body\":{}}}\n", time_text(e.time), json(e.kind).dump(), e.body);
    }
    return out;
}

std::expected<Session, std::string> Session::parse(std::string_view text) {
    Session s;
    std::size_t line_no = 0;
    bool have_header = false;
    while (!text.empty()) {
        auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.find_first_not_of(" \t") == std::string_view::npos) continue;
        auto j = parse_object(line, std::format("line {}", line_no));
        if (!j) return std::unexpected(j.error());
        if (!have_header) {
            if (j->value("format", "") != format_name)
                return std::unexpected(std::format("line {}: the first line is the header, {{\"format\":\"{}\"}}", line_no, format_name));
            Header& h = s.header;
            h.app = j->value("app", "");
            h.spec = j->value("spec", "");
            if (auto it = j->find("size"); it != j->end() && it->is_array() && it->size() == 2) {
                h.width = (*it)[0].get<double>();
                h.height = (*it)[1].get<double>();
            }
            h.scale = j->value("scale", 1.0);
            h.clock = j->value("clock", "virtual");
            h.fps = j->value("fps", 0.0);
            h.seed = j->value("seed", std::uint64_t{0});
            if (auto it = j->find("meta"); it != j->end() && it->is_object())
                for (auto item = it->begin(); item != it->end(); ++item)
                    h.meta.emplace_back(item.key(), item->is_string() ? item->get<std::string>() : item->dump());
            have_header = true;
            continue;
        }
        auto t = j->find("t");
        auto kind = j->find("kind");
        if (t == j->end() || !t->is_number() || kind == j->end() || !kind->is_string())
            return std::unexpected(std::format("line {}: an entry is {{\"t\": seconds, \"kind\": \"...\", \"body\": {{...}}}}", line_no));
        auto body = j->find("body");
        std::string body_text = body == j->end() ? std::string("{}") : body->dump();
        if (body != j->end() && !body->is_object()) return std::unexpected(std::format("line {}: `body` is an object", line_no));
        const double time = t->get<double>();
        if (!s.entries_.empty() && time < s.entries_.back().time)
            return std::unexpected(std::format("line {}: entries are ordered by time ({} after {})", line_no, time, s.entries_.back().time));
        s.entries_.push_back({time, kind->get<std::string>(), std::move(body_text)});
    }
    if (!have_header) return std::unexpected("an empty session: the first line is the header");
    return s;
}

std::expected<void, std::string> Session::save(const std::filesystem::path& file) const {
    auto r = mcpplibs::os::fs::write_atomic(file, ndjson());
    if (!r) return std::unexpected(r.error().message());
    return {};
}

std::expected<Session, std::string> Session::load(const std::filesystem::path& file) {
    auto text = mcpplibs::os::fs::read(file);
    if (!text) return std::unexpected(text.error().message());
    auto s = parse(*text);
    if (!s) return std::unexpected(std::format("{}: {}", utf8(file), s.error()));
    return s;
}

}  // namespace omui::session
