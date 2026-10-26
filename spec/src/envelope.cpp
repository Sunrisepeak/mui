module openmui.spec;

import std;

namespace omui::spec {

std::string Record::json() const {
    std::string out = "{\"kind\":";
    append_json_string(out, name(kind));
    if (const Value::Map* m = body.map()) {
        for (const auto& [k, v] : *m) {
            out += ',';
            append_json_string(out, k);
            out += ':';
            out += v.json();
        }
    }
    out += '}';
    return out;
}

Envelope& Envelope::phase(std::string_view name) {
    Value b;
    b.set("name", name);
    records_.push_back({RecordKind::phase, std::move(b)});
    return *this;
}

Envelope& Envelope::progress(double fraction, std::string_view message) {
    Value b;
    b.set("fraction", fraction);
    if (!message.empty()) b.set("message", message);
    records_.push_back({RecordKind::progress, std::move(b)});
    return *this;
}

Envelope& Envelope::log(std::string_view message) {
    Value b;
    b.set("message", message);
    records_.push_back({RecordKind::log, std::move(b)});
    return *this;
}

Envelope& Envelope::notice(std::string_view message) {
    Value b;
    b.set("message", message);
    records_.push_back({RecordKind::notice, std::move(b)});
    return *this;
}

Envelope& Envelope::error(std::string_view code, std::string_view message, Value extra) {
    Value b;
    b.set("code", code);
    b.set("message", message);
    if (const Value::Map* m = extra.map())
        for (const auto& [k, v] : *m) b.set(k, v);
    records_.push_back({RecordKind::error, std::move(b)});
    return *this;
}

Envelope& Envelope::data(Value data) {
    Value b;
    b.set("data", std::move(data));
    records_.push_back({RecordKind::data, std::move(b)});
    return *this;
}

Envelope& Envelope::result(int exit_code) {
    Value b;
    b.set("exitCode", exit_code);
    records_.push_back({RecordKind::result, std::move(b)});
    return *this;
}

std::optional<int> Envelope::exit_code() const noexcept {
    for (auto it = records_.rbegin(); it != records_.rend(); ++it)
        if (it->kind == RecordKind::result)
            if (const Value* c = it->body.get("exitCode")) return static_cast<int>(c->as_int());
    return std::nullopt;
}

const Value* Envelope::data_value() const noexcept {
    for (auto it = records_.rbegin(); it != records_.rend(); ++it)
        if (it->kind == RecordKind::data) return it->body.get("data");
    return nullptr;
}

std::string Envelope::ndjson() const {
    std::string out;
    for (const Record& r : records_) {
        out += r.json();
        out += '\n';
    }
    return out;
}

std::optional<Actor> Actor::parse(std::string_view text) {
    auto part = text.substr(0, text.find(';'));
    auto colon = part.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == part.size()) return std::nullopt;
    Actor a{std::string(part.substr(0, colon)), std::string(part.substr(colon + 1)), {}};
    text.remove_prefix(part.size());
    while (!text.empty()) {
        text.remove_prefix(1);  // ';'
        auto item = text.substr(0, text.find(';'));
        text.remove_prefix(item.size());
        auto eq = item.find('=');
        if (eq == std::string_view::npos) return std::nullopt;
        if (item.substr(0, eq) == "via") {
            auto v = item.substr(eq + 1);
            if (v.find(':') == std::string_view::npos) return std::nullopt;
            a.via.emplace_back(v);
        }
        // Other keys are reserved for later versions and skipped (append-only).
    }
    return a;
}

std::string Actor::text() const {
    std::string out = kind + ":" + name;
    for (const auto& v : via) out += ";via=" + v;
    return out;
}

}  // namespace omui::spec
